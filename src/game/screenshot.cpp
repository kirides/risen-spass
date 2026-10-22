#include <windows.h>

#include <d3d9.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
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
#include "game/symbols.h"
#include "hook/hook_manager.h"

// risen.debug.screenshot (docs/RE.md section 42): the D3D9 window cannot be captured from outside (PrintWindow / BitBlt return black), so
// the layer hooks IDirect3DDevice9::Present of the game's own device and copies the back buffer into a PNG.
//
// The game's device is not known at start-up, so a throw-away device is created on a hidden window: every device of the same d3d9.dll
// shares its vtable, which gives the address of Present (slot 17). The hook is installed on the first request only (nothing changes for
// a game that never asks for a screenshot).
//
// Two backends (docs/RE.md section 58), chosen once at the first request:
//   "rvk"   the d3d9.dll in <Risen>\bin is the Vulkan renderer risen-enhanced-graphics and exports rvk_screenshot: it captures the back
//           buffer itself (no D3D9 device exists in its Vulkan mode, so a Present hook would never fire). It refuses requests (ticket 0)
//           outside its Vulkan mode; then the D3D9 backend is used. findPresent() is never called while "rvk" is in use (it would create a
//           device through the proxy for nothing).
//   "d3d9"  the Present hook above.
// Both capture the finished back buffer (scene + post + GUI) of the frame being presented, before it is shown.

namespace spass::game {
namespace {

using PresentFn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA*);
Hook<PresentFn> g_present;

// One capture of the D3D9 backend: let `wait` Present calls pass, capture at the next one; a burst requeues itself `interval` frames later.
struct Request {
    int wait = 0;
    int remaining = 1;  // captures still to take, this one included
    int interval = 1;
    int index = 0;      // burst index (file <name>_b<index>.png)
    std::uint64_t job = 0;
    std::string name;
    bool burst = false;
};

// What screenshot_status(name) reports: every file of the request (a burst has several).
struct Job {
    std::uint64_t id = 0;
    int total = 1;
    int done = 0;
    std::string error;
    std::vector<std::uint32_t> tickets;  // rvk: the renderer's tickets still to poll
};

enum class Backend { None, Rvk, D3d9 };

// The exports of risen-enhanced-graphics' d3d9.dll (its include/rvk_api.h); all optional.
using RvkScreenshotFn = std::uint32_t (*)(const wchar_t* path, std::uint32_t waitFrames, std::uint32_t flags);
using RvkStatusFn = std::int32_t (*)(std::uint32_t ticket, char* msg, std::uint32_t msgSize);
using RvkFrameFn = std::uint64_t (*)();
using RvkCapsFn = std::uint32_t (*)();
constexpr std::uint32_t kRvkCapScreenshot = 0x1;  // RVK_CAP_SCREENSHOT: set only while the renderer's Vulkan mode draws
struct Rvk {
    bool looked = false;
    RvkScreenshotFn screenshot = nullptr;
    RvkStatusFn status = nullptr;
    RvkFrameFn frame = nullptr;
    RvkCapsFn caps = nullptr;
};

std::mutex g_mutex;
std::vector<Request> g_pending;
std::map<std::string, Job> g_jobs;
std::uint64_t g_nextJob = 1;
std::atomic<std::uint64_t> g_presents{0};  // Present calls seen by the hook
Backend g_backend = Backend::None;
Rvk g_rvk;
bool g_installAttempted = false;
bool g_installed = false;

void lookupRvk() {
    if (g_rvk.looked) {
        return;
    }
    g_rvk.looked = true;
    // the proxy in <Risen>\bin by its full path first: in passthrough mode the renderer has the system d3d9.dll loaded as well
    HMODULE d3d = GetModuleHandleW((paths::binDir() / L"d3d9.dll").c_str());
    if (d3d == nullptr) {
        d3d = GetModuleHandleW(L"d3d9.dll");
    }
    if (d3d == nullptr) {
        return;
    }
    g_rvk.screenshot = reinterpret_cast<RvkScreenshotFn>(GetProcAddress(d3d, "rvk_screenshot"));
    g_rvk.status = reinterpret_cast<RvkStatusFn>(GetProcAddress(d3d, "rvk_screenshot_status"));
    g_rvk.frame = reinterpret_cast<RvkFrameFn>(GetProcAddress(d3d, "rvk_frame_index"));
    g_rvk.caps = reinterpret_cast<RvkCapsFn>(GetProcAddress(d3d, "rvk_capabilities"));
    if (g_rvk.status == nullptr) {
        g_rvk.screenshot = nullptr;  // a ticket nobody can poll is useless
    }
    spdlog::info("screenshot: d3d9.dll {} rvk_screenshot", g_rvk.screenshot != nullptr ? "exports" : "does not export");
}

std::string fileName(const std::string& name, bool burst, int index) {
    return burst ? name + "_b" + std::to_string(index) + ".png" : name + ".png";
}

std::filesystem::path shotDir() { return paths::dataDir() / "screenshots"; }

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
    g_presents.fetch_add(1);
    std::vector<Request> due;
    {
        std::lock_guard lock(g_mutex);
        for (auto it = g_pending.begin(); it != g_pending.end();) {
            if (it->wait > 0) {
                --it->wait;  // this frame is one of those to let pass
                ++it;
            } else {
                due.push_back(std::move(*it));
                it = g_pending.erase(it);
            }
        }
    }
    for (Request& req : due) {
        std::string error;
        const std::filesystem::path path = shotDir() / fileName(req.name, req.burst, req.index);
        if (!guarded([&] { error = capture(dev, path); })) {
            error = "exception while capturing";
        }
        std::lock_guard lock(g_mutex);
        auto it = g_jobs.find(req.name);
        if (it == g_jobs.end() || it->second.id != req.job) {
            continue;  // asked for again under the same name meanwhile
        }
        Job& job = it->second;
        if (!error.empty()) {
            job.error = error;
            continue;  // the rest of a failed burst is dropped
        }
        ++job.done;
        if (req.remaining > 1) {
            Request next = std::move(req);
            next.remaining -= 1;
            next.index += 1;
            next.wait = next.interval - 1;  // the next capture `interval` presents after this one
            g_pending.push_back(std::move(next));
        }
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

// opts: nil | wait_frames | { wait_frames = 0..600, burst = 1..32, interval = 1..600 } (numbers are clamped to the range)
bool parseOptions(const sol::object& opts, int& wait, int& burst, int& interval, std::string& why) {
    const auto number = [&](const sol::object& o, const char* key, int lo, int hi, int& out) {
        if (o.get_type() == sol::type::lua_nil || o.get_type() == sol::type::none) {
            return true;
        }
        if (o.get_type() != sol::type::number) {
            why = std::string(key) + " must be a number";
            return false;
        }
        const double d = o.as<double>();
        if (!(d == d)) {
            why = std::string(key) + " is NaN";
            return false;
        }
        out = static_cast<int>(std::clamp(d, static_cast<double>(lo), static_cast<double>(hi)));
        return true;
    };
    switch (opts.get_type()) {
        case sol::type::lua_nil:
        case sol::type::none:
            return true;
        case sol::type::number:
            return number(opts, "wait_frames", 0, 600, wait);
        case sol::type::table: {
            const sol::table t = opts.as<sol::table>();
            return number(t["wait_frames"], "wait_frames", 0, 600, wait) && number(t["burst"], "burst", 1, 32, burst) &&
                   number(t["interval"], "interval", 1, 600, interval);
        }
        default:
            why = "options: expected wait_frames or { wait_frames, burst, interval }";
            return false;
    }
}

// rvk: asks the renderer for the state of the job's tickets (called with g_mutex held)
void pollRvk(Job& job) {
    for (auto it = job.tickets.begin(); it != job.tickets.end();) {
        char msg[256] = {};
        std::int32_t st = -1;
        if (!guarded([&] { st = g_rvk.status(*it, msg, sizeof(msg)); })) {
            st = -1;
            std::snprintf(msg, sizeof(msg), "rvk_screenshot_status raised an exception");
        }
        msg[sizeof(msg) - 1] = 0;
        if (st == 0) {
            ++it;
            continue;
        }
        if (st == 1) {
            ++job.done;
            spdlog::info("screenshot: rvk ticket {} done ({})", *it, msg);
        } else if (job.error.empty()) {
            job.error = msg[0] != 0 ? msg : "the renderer reported a failure";
        }
        it = job.tickets.erase(it);
    }
}

std::uint32_t rvkRequest(const std::filesystem::path& path, int wait) {
    std::uint32_t ticket = 0;
    const std::wstring w = path.wstring();
    if (!guarded([&] { ticket = g_rvk.screenshot(w.c_str(), static_cast<std::uint32_t>(wait), 0); })) {
        ticket = 0;
    }
    return ticket;
}

const char* backendName() {
    switch (g_backend) {
        case Backend::Rvk:
            return "rvk";
        case Backend::D3d9:
            return "d3d9";
        default:
            return nullptr;
    }
}

}  // namespace

void bindScreenshot(sol::state_view, sol::table risen) {
    sol::table dbg = risen["debug"];
    using Two = std::tuple<sol::object, sol::object>;
    // risen.debug.screenshot(name [, opts]) -> true | nil, reason: asks for a PNG of the game's back buffer at the next presented frame (or after `wait_frames` frames, 0..600):
    // <spass>/screenshots/<name>.png (name: letters, digits, _ - .). opts = wait_frames | { wait_frames, burst = 1..32, interval = 1..600 }: a burst writes <name>_b0.png ..
    // <name>_b<n-1>.png, `interval` presented frames apart. Poll risen.debug.screenshot_status(name). The backend (rvk / d3d9) is chosen at the first request.
    dbg.set_function("screenshot", [](sol::this_state ts, const std::string& name, sol::object opts) -> Two {
        sol::state_view L(ts);
        const auto fail = [&](const std::string& why) -> Two { return {sol::make_object(L, sol::lua_nil), sol::make_object(L, why)}; };
        if (!validName(name)) {
            return fail("name: 1-64 characters of letters, digits, _ - .");
        }
        int wait = 0, burst = 1, interval = 1;
        std::string why;
        if (!parseOptions(opts, wait, burst, interval, why)) {
            return fail(why);
        }
        const bool isBurst = burst > 1;
        std::error_code ec;
        std::filesystem::create_directories(shotDir(), ec);
        for (int k = 0; k < burst; ++k) {
            std::filesystem::remove(shotDir() / fileName(name, isBurst, k), ec);  // a stale file must not pass for the new one
        }

        std::uint32_t firstTicket = 0;
        if (g_backend == Backend::None) {
            lookupRvk();
            if (g_rvk.screenshot != nullptr && g_rvk.caps != nullptr) {
                // rvk_capabilities says whether the Vulkan renderer draws: then every request goes to it (a refused one fails instead of creating a D3D9 device)
                std::uint32_t caps = 0;
                guarded([&] { caps = g_rvk.caps(); });
                g_backend = (caps & kRvkCapScreenshot) != 0 ? Backend::Rvk : Backend::D3d9;
                spdlog::info("screenshot: backend {} (rvk_capabilities 0x{:x})", backendName(), caps);
            } else if (g_rvk.screenshot != nullptr) {
                // no rvk_capabilities: the first request decides (the renderer refuses outside its Vulkan mode)
                firstTicket = rvkRequest(shotDir() / fileName(name, isBurst, 0), wait);
                g_backend = firstTicket > 0 ? Backend::Rvk : Backend::D3d9;
                spdlog::info("screenshot: backend {} ({})", backendName(), firstTicket > 0 ? "rvk_screenshot accepted the request" : "rvk_screenshot refused: not in Vulkan mode");
            } else {
                g_backend = Backend::D3d9;
                spdlog::info("screenshot: backend d3d9");
            }
        }
        if (g_backend == Backend::D3d9 && !ensureHook(why)) {
            return fail(why);
        }

        std::lock_guard lock(g_mutex);
        Job& job = g_jobs[name];
        job = Job{};
        job.id = g_nextJob++;
        job.total = burst;
        if (g_backend == Backend::Rvk) {
            for (int k = 0; k < burst; ++k) {
                const std::uint32_t ticket = (k == 0 && firstTicket > 0) ? firstTicket : rvkRequest(shotDir() / fileName(name, isBurst, k), wait + k * interval);
                if (ticket == 0) {
                    job.error = "the renderer refused the request (rvk_screenshot returned 0)";
                    return fail(job.error);
                }
                job.tickets.push_back(ticket);
            }
        } else {
            Request req;
            req.wait = wait;
            req.remaining = burst;
            req.interval = interval;
            req.job = job.id;
            req.name = name;
            req.burst = isBurst;
            g_pending.push_back(std::move(req));
        }
        return {sol::make_object(L, true), sol::make_object(L, sol::lua_nil)};
    });
    // risen.debug.screenshot_status(name) -> "pending" | "done" | "failed: <reason>" | nil (never asked); a burst is "done" when every file is written
    dbg.set_function("screenshot_status", [](sol::this_state ts, const std::string& name) -> sol::object {
        sol::state_view L(ts);
        std::lock_guard lock(g_mutex);
        auto it = g_jobs.find(name);
        if (it == g_jobs.end()) {
            return sol::make_object(L, sol::lua_nil);
        }
        Job& job = it->second;
        if (!job.tickets.empty() && g_rvk.status != nullptr) {
            pollRvk(job);
        }
        if (!job.error.empty()) {
            return sol::make_object(L, "failed: " + job.error);
        }
        return sol::make_object(L, job.done >= job.total ? "done" : "pending");
    });
    // risen.debug.screenshot_backend() -> "rvk" | "d3d9" | nil (no screenshot asked for yet)
    dbg.set_function("screenshot_backend", [](sol::this_state ts) -> sol::object {
        const char* n = backendName();
        return n != nullptr ? sol::make_object(ts, n) : sol::make_object(ts, sol::lua_nil);
    });
    // risen.debug.frame() -> frame, source | nil: presented frames of the renderer ("rvk"), Present calls seen by the hook ("d3d9"), or
    // eCTimer::GetMasterFrameCounter ("engine") before a backend is chosen
    dbg.set_function("frame", [](sol::this_state ts) -> Two {
        sol::state_view L(ts);
        if (g_backend == Backend::Rvk && g_rvk.frame != nullptr) {
            std::uint64_t n = 0;
            if (guarded([&] { n = g_rvk.frame(); })) {
                return {sol::make_object(L, static_cast<double>(n)), sol::make_object(L, "rvk")};
            }
        } else if (g_backend == Backend::D3d9 && g_installed) {
            return {sol::make_object(L, static_cast<double>(g_presents.load())), sol::make_object(L, "d3d9")};
        }
        const Symbols& s = sym();
        if (s.timer_instance != nullptr && s.timer_master_frame != nullptr) {
            unsigned long n = 0;
            if (guarded([&] { n = s.timer_master_frame(s.timer_instance()); })) {
                return {sol::make_object(L, static_cast<double>(n)), sol::make_object(L, "engine")};
            }
        }
        return {sol::make_object(L, sol::lua_nil), sol::make_object(L, sol::lua_nil)};
    });
}

}  // namespace spass::game
