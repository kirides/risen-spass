#include <windows.h>

#include <d3d9.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include <spdlog/spdlog.h>

#include "common/paths.h"
#include "game/abi.h"
#include "game/bindings.h"
#include "hook/hook_manager.h"

// risen.debug.screenshot (docs/RE.md section 42): the D3D9 window cannot be captured from outside (PrintWindow / BitBlt return black), so
// the layer hooks IDirect3DDevice9::Present of the game's own device and copies the back buffer into a PNG.
//
// The game's device is not known at start-up, so a throw-away device is created on a hidden window: every device of the same d3d9.dll
// shares its vtable, which gives the address of Present (slot 17). The hook is installed on the first request only (nothing changes for
// a game that never asks for a screenshot).

namespace spass::game {
namespace {

using PresentFn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA*);
Hook<PresentFn> g_present;

struct Request {
    int wait = 0;  // frames (Present calls) to let pass first
    std::string name;
    std::filesystem::path path;
};

std::mutex g_mutex;
std::vector<Request> g_pending;
std::map<std::string, std::string> g_status;  // name -> "pending" | "done" | "failed: reason"
std::atomic<bool> g_busy{false};
bool g_installAttempted = false;
bool g_installed = false;

// --- PNG (stored deflate blocks: no compression, but valid and tiny code) ---------------------------------------------------------

uint32_t crc32(uint32_t crc, const uint8_t* data, size_t n) {
    static uint32_t table[256];
    static bool init = false;
    if (!init) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) {
                c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            }
            table[i] = c;
        }
        init = true;
    }
    crc = ~crc;
    for (size_t i = 0; i < n; ++i) {
        crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    return ~crc;
}

void put32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(static_cast<uint8_t>(x >> 24));
    v.push_back(static_cast<uint8_t>(x >> 16));
    v.push_back(static_cast<uint8_t>(x >> 8));
    v.push_back(static_cast<uint8_t>(x));
}

void chunk(std::vector<uint8_t>& out, const char* type, const std::vector<uint8_t>& data) {
    put32(out, static_cast<uint32_t>(data.size()));
    std::vector<uint8_t> body(type, type + 4);
    body.insert(body.end(), data.begin(), data.end());
    out.insert(out.end(), body.begin(), body.end());
    put32(out, crc32(0, body.data(), body.size()));
}

// rgb: width*height*3 bytes (top-down).
std::vector<uint8_t> encodePng(const std::vector<uint8_t>& rgb, uint32_t w, uint32_t h) {
    std::vector<uint8_t> raw;
    raw.reserve(static_cast<size_t>(h) * (w * 3 + 1));
    for (uint32_t y = 0; y < h; ++y) {
        raw.push_back(0);  // filter: none
        raw.insert(raw.end(), rgb.begin() + static_cast<size_t>(y) * w * 3, rgb.begin() + static_cast<size_t>(y + 1) * w * 3);
    }
    std::vector<uint8_t> z = {0x78, 0x01};
    uint32_t a = 1, b = 0;
    for (uint8_t c : raw) {
        a = (a + c) % 65521;
        b = (b + a) % 65521;
    }
    size_t pos = 0;
    while (pos < raw.size()) {
        const size_t n = std::min<size_t>(65535, raw.size() - pos);
        z.push_back(pos + n == raw.size() ? 1 : 0);
        z.push_back(static_cast<uint8_t>(n & 0xFF));
        z.push_back(static_cast<uint8_t>(n >> 8));
        z.push_back(static_cast<uint8_t>(~n & 0xFF));
        z.push_back(static_cast<uint8_t>((~n >> 8) & 0xFF));
        z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + n);
        pos += n;
    }
    put32(z, (b << 16) | a);

    std::vector<uint8_t> out = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    std::vector<uint8_t> ihdr;
    put32(ihdr, w);
    put32(ihdr, h);
    ihdr.insert(ihdr.end(), {8, 2, 0, 0, 0});  // 8 bit, truecolour
    chunk(out, "IHDR", ihdr);
    chunk(out, "IDAT", z);
    chunk(out, "IEND", {});
    return out;
}

// --- capture --------------------------------------------------------------------------------------------------------------------------

template <class T>
void release(T*& p) {
    if (p != nullptr) {
        p->Release();
        p = nullptr;
    }
}

std::string capture(IDirect3DDevice9* dev, const std::filesystem::path& path) {
    IDirect3DSurface9* back = nullptr;
    IDirect3DSurface9* copy = nullptr;
    std::string error;
    do {
        if (FAILED(dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back)) || back == nullptr) {
            error = "GetBackBuffer failed";
            break;
        }
        D3DSURFACE_DESC desc{};
        back->GetDesc(&desc);
        if (FAILED(dev->CreateOffscreenPlainSurface(desc.Width, desc.Height, desc.Format, D3DPOOL_SYSTEMMEM, &copy, nullptr))) {
            error = "CreateOffscreenPlainSurface failed";
            break;
        }
        if (FAILED(dev->GetRenderTargetData(back, copy))) {
            error = "GetRenderTargetData failed (multisampled back buffer?)";
            break;
        }
        D3DLOCKED_RECT lr{};
        if (FAILED(copy->LockRect(&lr, nullptr, D3DLOCK_READONLY))) {
            error = "LockRect failed";
            break;
        }
        const bool bgra = desc.Format == D3DFMT_X8R8G8B8 || desc.Format == D3DFMT_A8R8G8B8;
        std::vector<uint8_t> rgb(static_cast<size_t>(desc.Width) * desc.Height * 3);
        if (bgra) {
            for (UINT y = 0; y < desc.Height; ++y) {
                const uint8_t* row = static_cast<const uint8_t*>(lr.pBits) + static_cast<size_t>(y) * lr.Pitch;
                uint8_t* out = rgb.data() + static_cast<size_t>(y) * desc.Width * 3;
                for (UINT x = 0; x < desc.Width; ++x) {
                    out[x * 3 + 0] = row[x * 4 + 2];
                    out[x * 3 + 1] = row[x * 4 + 1];
                    out[x * 3 + 2] = row[x * 4 + 0];
                }
            }
        } else {
            error = "unsupported back buffer format " + std::to_string(static_cast<int>(desc.Format));
        }
        copy->UnlockRect();
        if (!error.empty()) {
            break;
        }
        const std::vector<uint8_t> png = encodePng(rgb, desc.Width, desc.Height);
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        std::ofstream f(path, std::ios::binary);
        if (!f) {
            error = "cannot write " + path.string();
            break;
        }
        f.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
        spdlog::info("screenshot: {} ({}x{})", path.string(), desc.Width, desc.Height);
    } while (false);
    release(copy);
    release(back);
    return error;
}

HRESULT STDMETHODCALLTYPE detourPresent(IDirect3DDevice9* dev, const RECT* a, const RECT* b, HWND w, const RGNDATA* r) {
    std::vector<Request> todo;
    {
        std::lock_guard lock(g_mutex);
        todo.swap(g_pending);
    }
    for (const Request& req : todo) {
        std::string error;
        if (!guarded([&] { error = capture(dev, req.path); })) {
            error = "exception while capturing";
        }
        std::lock_guard lock(g_mutex);
        g_status[req.name] = error.empty() ? "done" : "failed: " + error;
    }
    return g_present.original(dev, a, b, w, r);
}

// Address of IDirect3DDevice9::Present from a throw-away device on a hidden window.
void* findPresent() {
    HMODULE d3d = LoadLibraryW(L"d3d9.dll");
    if (d3d == nullptr) {
        return nullptr;
    }
    using Create = IDirect3D9*(WINAPI*)(UINT);
    auto create = reinterpret_cast<Create>(GetProcAddress(d3d, "Direct3DCreate9"));
    if (create == nullptr) {
        return nullptr;
    }
    WNDCLASSW wc{};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"SpassDummyD3D";
    RegisterClassW(&wc);
    HWND hwnd = CreateWindowW(wc.lpszClassName, L"", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, nullptr, nullptr, wc.hInstance, nullptr);
    void* present = nullptr;
    if (IDirect3D9* d3 = create(D3D_SDK_VERSION)) {
        D3DPRESENT_PARAMETERS pp{};
        pp.Windowed = TRUE;
        pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
        pp.BackBufferFormat = D3DFMT_UNKNOWN;
        pp.hDeviceWindow = hwnd;
        IDirect3DDevice9* dev = nullptr;
        if (SUCCEEDED(d3->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd, D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_DISABLE_DRIVER_MANAGEMENT, &pp, &dev)) &&
            dev != nullptr) {
            void** vtable = *reinterpret_cast<void***>(dev);
            present = vtable[17];  // IDirect3DDevice9::Present
            dev->Release();
        }
        d3->Release();
    }
    if (hwnd != nullptr) {
        DestroyWindow(hwnd);
    }
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    return present;
}

bool ensureHook(std::string& why) {
    if (g_installed) {
        return true;
    }
    if (g_installAttempted) {
        why = "the Present hook could not be installed";
        return false;
    }
    g_installAttempted = true;
    void* present = findPresent();
    if (present == nullptr) {
        why = "could not find IDirect3DDevice9::Present";
        return false;
    }
    if (!g_present.install("IDirect3DDevice9::Present", reinterpret_cast<PresentFn>(present), &detourPresent) || !hooks::enableAll()) {
        why = "could not hook IDirect3DDevice9::Present";
        return false;
    }
    g_installed = true;
    return true;
}

bool validName(const std::string& n) {
    if (n.empty() || n.size() > 64) {
        return false;
    }
    for (unsigned char c : n) {
        if (!(std::isalnum(c) || c == '_' || c == '-' || c == '.')) {
            return false;
        }
    }
    return n.find("..") == std::string::npos;
}

}  // namespace

void bindScreenshot(sol::state_view, sol::table risen) {
    sol::table dbg = risen["debug"];
    using Two = std::tuple<sol::object, sol::object>;
    // risen.debug.screenshot(name [, wait_frames]) -> true | nil, reason: asks for a PNG of the game's back buffer at the next frame (or after `wait_frames` rendered frames, 0..600):
    // <spass>/screenshots/<name>.png (name: letters, digits, _ - .). Poll risen.debug.screenshot_status(name).
    dbg.set_function("screenshot", [](sol::this_state ts, const std::string& name, sol::optional<int> wait) -> Two {
        sol::state_view L(ts);
        if (!validName(name)) {
            return {sol::make_object(L, sol::lua_nil), sol::make_object(L, "name: 1-64 characters of letters, digits, _ - .")};
        }
        std::string why;
        if (!ensureHook(why)) {
            return {sol::make_object(L, sol::lua_nil), sol::make_object(L, why)};
        }
        std::lock_guard lock(g_mutex);
        g_status[name] = "pending";
        g_pending.push_back({std::clamp(wait.value_or(0), 0, 600), name, paths::dataDir() / "screenshots" / (name + ".png")});
        return {sol::make_object(L, true), sol::make_object(L, sol::lua_nil)};
    });
    // risen.debug.screenshot_status(name) -> "pending" | "done" | "failed: <reason>" | nil (never asked)
    dbg.set_function("screenshot_status", [](sol::this_state ts, const std::string& name) -> sol::object {
        sol::state_view L(ts);
        std::lock_guard lock(g_mutex);
        auto it = g_status.find(name);
        return it == g_status.end() ? sol::make_object(L, sol::lua_nil) : sol::make_object(L, it->second);
    });
}

}  // namespace spass::game
