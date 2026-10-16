#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <vector>

#include <spdlog/spdlog.h>

#include "game/abi.h"
#include "game/bindings.h"
#include "game/properties.h"
#include "game/script_bridge.h"
#include "lua/runtime.h"

// risen.gui (docs/RE.md section 42, docs/LUA_API.md "GUI windows"): the engine's own GUI as a tree of eCGuiWindow2 objects.
//   GUI2::FindPage(gEHudPage) -> gCHudPage2*  (a page IS an eCGuiWindow2; the pages are created from compiled_windows.bin at start-up)
//   eCGuiWindow2::GetFirstChild / GetNextSibling / GetParent, GetName / GetID / GetText / GetPosition / GetSize / GetVisible, Clone(parent) ...
// Windows are native objects that the engine may delete (a page that is rebuilt, a cloned window removed). A GuiWindow handle therefore is
// only used after it was found again in the live page trees (or in the set of windows this layer cloned and still tracks), and every engine
// call is SEH-guarded.

namespace spass::game {
namespace {

constexpr int kMaxPages = 40;
constexpr int kMaxNodes = 4000;

struct GuiWindow {
    void* ptr = nullptr;
};

// What the page controller scripts get as their argument (the game message the page system sends): { message, ?, parameter, pointer to an out byte }.
struct PageMessage {
    int msg;
    int pad;
    std::int64_t param;
    unsigned char* out;
};

std::set<void*> g_owned;  // roots of windows that risen.gui cloned and that are not (yet) below a page

const char* pageNameC(int id) {
    const Symbols& s = sym();
    if (s.gui_page_names == nullptr || id < 0 || id >= kMaxPages) {
        return nullptr;
    }
    return s.bstring_get_text(s.gui_page_names + static_cast<size_t>(id) * 8);
}

std::string pageName(int id) {
    std::string out;
    guarded([&] {
        if (const char* n = pageNameC(id)) {
            out = n;
        }
    });
    return out;
}

bool pageVisible(int id) {
    const Symbols& s = sym();
    bool v = false;
    if (s.gui2_object != nullptr && s.gui_is_visible != nullptr) {
        guarded([&] { v = s.gui_is_visible(s.gui2_object, id); });
    }
    return v;
}

void* findPagePtr(int id) {
    const Symbols& s = sym();
    void* page = nullptr;
    if (s.gui2_object != nullptr && s.gui_find_page != nullptr && id >= 0 && id < kMaxPages) {
        guarded([&] { page = s.gui_find_page(s.gui2_object, id); });
    }
    return page;
}

// How many page ids there are: the name table ends with a null / unreadable entry (a page without a name is not used).
int pageCount() {
    int n = 0;
    for (int i = 0; i < kMaxPages; ++i) {
        if (pageName(i).empty()) {
            break;
        }
        n = i + 1;
    }
    return n;
}

void* firstChild(void* w) {
    void* c = nullptr;
    guarded([&] { c = sym().win_first_child(w); });
    return c;
}
void* nextSibling(void* w) {
    void* c = nullptr;
    guarded([&] { c = sym().win_next_sibling(w); });
    return c;
}

// Every window reachable from the pages and from the tracked clones.
void collect(void* root, std::set<void*>& out) {
    std::vector<void*> stack{root};
    while (!stack.empty() && out.size() < static_cast<size_t>(kMaxNodes) * 8) {
        void* w = stack.back();
        stack.pop_back();
        if (w == nullptr || !out.insert(w).second) {
            continue;
        }
        for (void* c = firstChild(w); c != nullptr && out.find(c) == out.end(); c = nextSibling(c)) {
            stack.push_back(c);
        }
    }
}

std::set<void*> liveWindows() {
    std::set<void*> out;
    for (int i = 0; i < kMaxPages; ++i) {
        if (void* p = findPagePtr(i)) {
            collect(p, out);
        }
    }
    for (void* o : g_owned) {
        collect(o, out);
    }
    return out;
}

// Walking every page for each call would make a page of 100 operations slow: a handle that was found in a walk of the last 100 ms is still taken as live (a window the
// engine deletes in that time and a script then uses is the only case this lets through; the pages themselves live as long as the game).
bool isLive(void* w) {
    static std::set<void*> cache;
    static ULONGLONG cachedAt = 0;
    if (w == nullptr || sym().win_first_child == nullptr) {
        return false;
    }
    const ULONGLONG now = GetTickCount64();
    if (now - cachedAt < 100 && cache.count(w) != 0) {
        return true;
    }
    cache = liveWindows();
    cachedAt = now;
    return cache.count(w) != 0;
}

HWND FindGameWindow() {
    struct Ctx {
        DWORD pid;
        HWND found;
    } ctx{GetCurrentProcessId(), nullptr};
    EnumWindows(
        [](HWND h, LPARAM lp) -> BOOL {
            auto* c = reinterpret_cast<Ctx*>(lp);
            DWORD pid = 0;
            GetWindowThreadProcessId(h, &pid);
            if (pid == c->pid && IsWindowVisible(h) && GetWindow(h, GW_OWNER) == nullptr) {
                RECT r{};
                if (GetClientRect(h, &r) && r.right > 200) {
                    c->found = h;
                    return FALSE;
                }
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&ctx));
    return ctx.found;
}

// The GUI module maps the client pixels of mouse messages to its virtual resolution (1365 x 768): virtual = (client - offset) / scale (eCGuiModule2 +0x20/+0x24 floats,
// +0x2c/+0x30 ints, active when +0x3c and +0x3d are set; decompiled OnHandleMessage). The inverse for a point in virtual coordinates.
bool virtualToClient(int vx, int vy, int& cx, int& cy) {
    const Symbols& s = sym();
    bool done = false;
    guarded([&] {
        auto* mod = static_cast<unsigned char*>(s.gui_module_instance());
        if (mod == nullptr) {
            return;
        }
        if (mod[0x3d] != 0 && mod[0x3c] != 0) {
            const float sxf = *reinterpret_cast<float*>(mod + 0x20), syf = *reinterpret_cast<float*>(mod + 0x24);
            const int ox = *reinterpret_cast<int*>(mod + 0x2c), oy = *reinterpret_cast<int*>(mod + 0x30);
            cx = static_cast<int>(vx * sxf + 0.5f) + ox;
            cy = static_cast<int>(vy * syf + 0.5f) + oy;
        } else {
            cx = vx;
            cy = vy;
        }
        done = true;
    });
    return done;
}

// A window message as the window procedure delivers it: eCApplication::OnHandleMessage (console, the HUD's PreHandleMessage that opens the menu on ESC, the input admin, the GUI).
bool handleMessage(unsigned msg, unsigned wparam, long lparam) {
    const Symbols& s = sym();
    if (s.app_instance != nullptr && s.app_handle_message != nullptr) {
        return guarded([&] {
            long long result = 0;
            s.app_handle_message(s.app_instance(), msg, wparam, lparam, &result);
        });
    }
    return guarded([&] { s.gui_handle_message(s.gui_module_instance(), msg, wparam, lparam); });
}

bool sendClick(int cx, int cy) {
    const long lparam = static_cast<long>((static_cast<unsigned>(cy) << 16) | (static_cast<unsigned>(cx) & 0xffff));
    return handleMessage(0x200, 0, lparam) && handleMessage(0x201, 1, lparam) && handleMessage(0x202, 0, lparam);
}

bool sendKey(int vk) {
    return handleMessage(0x100, static_cast<unsigned>(vk), 0) && handleMessage(0x101, static_cast<unsigned>(vk), 0);
}

std::string className(void* w) {
    std::string out;
    const Symbols& s = sym();
    if (s.type_class_name == nullptr) {
        return out;
    }
    guarded([&] {
        void** vtable = *reinterpret_cast<void***>(w);
        using GetType = const void* (*)(void*);
        const void* type = reinterpret_cast<GetType>(vtable[0])(w);
        if (type != nullptr) {
            out = s.bstring_get_text(s.type_class_name(type));
        }
    });
    return out;
}

std::string winName(void* w) {
    std::string out;
    guarded([&] { out = sym().bstring_get_text(sym().win_get_name(w)); });
    return out;
}

std::string winText(void* w) {
    std::string out;
    guarded([&] {
        OutUString t;
        sym().win_get_text(w, t.ptr());
        out = t.take();
    });
    return out;
}

bool getPoint(void* w, bool size, int& x, int& y) {
    int p[2] = {0, 0};
    const bool ok = guarded([&] { (size ? sym().win_get_size : sym().win_get_position)(w, p); });
    x = p[0];
    y = p[1];
    return ok;
}

sol::table describe(sol::state_view L, void* w) {
    const Symbols& s = sym();
    sol::table t = L.create_table();
    t["class"] = className(w);
    t["name"] = winName(w);
    unsigned short id = 0;
    guarded([&] { id = s.win_get_id(w); });
    t["id"] = static_cast<int>(id);
    t["text"] = winText(w);
    int x = 0, y = 0, cx = 0, cy = 0;
    getPoint(w, false, x, y);
    getPoint(w, true, cx, cy);
    t["x"] = x;
    t["y"] = y;
    t["w"] = cx;
    t["h"] = cy;
    bool vis = false, dis = false;
    guarded([&] {
        vis = s.win_get_visible(w);
        dis = s.win_get_disabled(w);
    });
    t["visible"] = vis;
    t["disabled"] = dis;
    return t;
}

using Two = std::tuple<sol::object, sol::object>;

}  // namespace

void bindGui(sol::state_view lua, sol::table risen) {
    sol::table gui = lua.create_table();
    risen["gui"] = gui;

    const auto fail = [](sol::state_view L, const std::string& reason) -> Two { return {sol::make_object(L, sol::lua_nil), sol::make_object(L, reason)}; };
    const auto ok = [](sol::state_view L) -> Two { return {sol::make_object(L, true), sol::make_object(L, sol::lua_nil)}; };
    const auto available = [](const Symbols& s) { return s.win_first_child != nullptr && s.gui_find_page != nullptr && s.gui2_object != nullptr && s.gui_page_names != nullptr; };

    sol::usertype<GuiWindow> ut = lua.new_usertype<GuiWindow>("GuiWindow", sol::no_constructor);

    // Resolves a handle: the window must still be part of a page tree (or a tracked clone).
    const auto resolve = [](GuiWindow& w, std::string& why) -> void* {
        if (!isLive(w.ptr)) {
            why = "the window is gone";
            return nullptr;
        }
        return w.ptr;
    };

    ut.set_function("valid", [](GuiWindow& w) { return isLive(w.ptr); });
    ut.set_function("class", [resolve](sol::this_state ts, GuiWindow& w) -> sol::object {
        std::string why;
        void* p = resolve(w, why);
        return p == nullptr ? sol::make_object(sol::state_view(ts), sol::lua_nil) : sol::make_object(sol::state_view(ts), className(p));
    });
    ut.set_function("describe", [resolve](sol::this_state ts, GuiWindow& w) -> sol::object {
        sol::state_view L(ts);
        std::string why;
        void* p = resolve(w, why);
        return p == nullptr ? sol::make_object(L, sol::lua_nil) : sol::make_object(L, describe(L, p));
    });
    ut.set_function("name", [resolve](sol::this_state ts, GuiWindow& w) -> sol::object {
        std::string why;
        void* p = resolve(w, why);
        return p == nullptr ? sol::make_object(sol::state_view(ts), sol::lua_nil) : sol::make_object(sol::state_view(ts), winName(p));
    });
    ut.set_function("id", [resolve](sol::this_state ts, GuiWindow& w) -> sol::object {
        std::string why;
        void* p = resolve(w, why);
        if (p == nullptr) {
            return sol::make_object(sol::state_view(ts), sol::lua_nil);
        }
        unsigned short id = 0;
        guarded([&] { id = sym().win_get_id(p); });
        return sol::make_object(sol::state_view(ts), static_cast<int>(id));
    });
    ut.set_function("text", [resolve](sol::this_state ts, GuiWindow& w) -> sol::object {
        std::string why;
        void* p = resolve(w, why);
        return p == nullptr ? sol::make_object(sol::state_view(ts), sol::lua_nil) : sol::make_object(sol::state_view(ts), winText(p));
    });
    ut.set_function("set_text", [resolve, fail, ok](sol::this_state ts, GuiWindow& w, const std::string& text) -> Two {
        sol::state_view L(ts);
        std::string why;
        void* p = resolve(w, why);
        if (p == nullptr) {
            return fail(L, why);
        }
        if (text.size() > 2000) {
            return fail(L, "the text is too long (2000 bytes at most)");
        }
        if (!guarded([&] {
                const UString u(text);
                sym().win_set_text(p, u.get());
            })) {
            return fail(L, "the engine raised an exception");
        }
        return ok(L);
    });
    ut.set_function("position", [resolve](sol::this_state ts, GuiWindow& w) -> std::tuple<sol::object, sol::object> {
        sol::state_view L(ts);
        std::string why;
        void* p = resolve(w, why);
        int x = 0, y = 0;
        if (p == nullptr || !getPoint(p, false, x, y)) {
            return {sol::make_object(L, sol::lua_nil), sol::make_object(L, sol::lua_nil)};
        }
        return {sol::make_object(L, x), sol::make_object(L, y)};
    });
    ut.set_function("size", [resolve](sol::this_state ts, GuiWindow& w) -> std::tuple<sol::object, sol::object> {
        sol::state_view L(ts);
        std::string why;
        void* p = resolve(w, why);
        int x = 0, y = 0;
        if (p == nullptr || !getPoint(p, true, x, y)) {
            return {sol::make_object(L, sol::lua_nil), sol::make_object(L, sol::lua_nil)};
        }
        return {sol::make_object(L, x), sol::make_object(L, y)};
    });
    ut.set_function("set_position", [resolve, fail, ok](sol::this_state ts, GuiWindow& w, int x, int y) -> Two {
        sol::state_view L(ts);
        std::string why;
        void* p = resolve(w, why);
        if (p == nullptr) {
            return fail(L, why);
        }
        if (std::abs(x) > 20000 || std::abs(y) > 20000) {
            return fail(L, "position out of range");
        }
        const int pt[2] = {x, y};
        return guarded([&] { sym().win_set_position(p, pt); }) ? ok(L) : fail(L, "the engine raised an exception");
    });
    ut.set_function("set_size", [resolve, fail, ok](sol::this_state ts, GuiWindow& w, int cx, int cy) -> Two {
        sol::state_view L(ts);
        std::string why;
        void* p = resolve(w, why);
        if (p == nullptr) {
            return fail(L, why);
        }
        if (cx < 0 || cy < 0 || cx > 20000 || cy > 20000) {
            return fail(L, "size out of range");
        }
        const int pt[2] = {cx, cy};
        return guarded([&] { sym().win_set_size(p, pt); }) ? ok(L) : fail(L, "the engine raised an exception");
    });
    ut.set_function("visible", [resolve](sol::this_state ts, GuiWindow& w) -> sol::object {
        std::string why;
        void* p = resolve(w, why);
        if (p == nullptr) {
            return sol::make_object(sol::state_view(ts), sol::lua_nil);
        }
        bool v = false;
        guarded([&] { v = sym().win_get_visible(p); });
        return sol::make_object(sol::state_view(ts), v);
    });
    ut.set_function("set_visible", [resolve, fail, ok](sol::this_state ts, GuiWindow& w, bool v) -> Two {
        sol::state_view L(ts);
        std::string why;
        void* p = resolve(w, why);
        if (p == nullptr) {
            return fail(L, why);
        }
        return guarded([&] { sym().win_set_visible(p, &v); }) ? ok(L) : fail(L, "the engine raised an exception");
    });
    ut.set_function("disabled", [resolve](sol::this_state ts, GuiWindow& w) -> sol::object {
        std::string why;
        void* p = resolve(w, why);
        if (p == nullptr) {
            return sol::make_object(sol::state_view(ts), sol::lua_nil);
        }
        bool v = false;
        guarded([&] { v = sym().win_get_disabled(p); });
        return sol::make_object(sol::state_view(ts), v);
    });
    ut.set_function("set_disabled", [resolve, fail, ok](sol::this_state ts, GuiWindow& w, bool v) -> Two {
        sol::state_view L(ts);
        std::string why;
        void* p = resolve(w, why);
        if (p == nullptr) {
            return fail(L, why);
        }
        return guarded([&] { sym().win_set_disabled(p, &v); }) ? ok(L) : fail(L, "the engine raised an exception");
    });
    ut.set_function("parent", [resolve](sol::this_state ts, GuiWindow& w) -> sol::object {
        sol::state_view L(ts);
        std::string why;
        void* p = resolve(w, why);
        void* par = nullptr;
        if (p != nullptr) {
            guarded([&] { par = sym().win_parent(p); });
        }
        return par == nullptr ? sol::make_object(L, sol::lua_nil) : sol::make_object(L, GuiWindow{par});
    });
    ut.set_function("children", [resolve](sol::this_state ts, GuiWindow& w) -> sol::object {
        sol::state_view L(ts);
        std::string why;
        void* p = resolve(w, why);
        if (p == nullptr) {
            return sol::make_object(L, sol::lua_nil);
        }
        sol::table out = L.create_table();
        int n = 0;
        for (void* c = firstChild(p); c != nullptr && n < kMaxNodes; c = nextSibling(c)) {
            out[++n] = GuiWindow{c};
        }
        return sol::make_object(L, out);
    });
    // w:find(name | id [, recursive = true]) -> GuiWindow | nil: the first descendant with that name (string) or ID (number)
    ut.set_function("find", [resolve](sol::this_state ts, GuiWindow& w, sol::object key) -> sol::object {
        sol::state_view L(ts);
        std::string why;
        void* p = resolve(w, why);
        if (p == nullptr || (key.get_type() != sol::type::string && key.get_type() != sol::type::number) ||
            (key.get_type() == sol::type::string && key.as<std::string>().size() > 100)) {
            return sol::make_object(L, sol::lua_nil);
        }
        std::vector<void*> stack{p};
        std::set<void*> seen;
        while (!stack.empty() && seen.size() < static_cast<size_t>(kMaxNodes)) {
            void* cur = stack.back();
            stack.pop_back();
            if (!seen.insert(cur).second) {
                continue;
            }
            if (cur != p) {
                bool match = false;
                if (key.get_type() == sol::type::string) {
                    match = winName(cur) == key.as<std::string>();
                } else {
                    unsigned short id = 0;
                    guarded([&] { id = sym().win_get_id(cur); });
                    match = static_cast<int>(id) == key.as<int>();
                }
                if (match) {
                    return sol::make_object(L, GuiWindow{cur});
                }
            }
            std::vector<void*> kids;
            for (void* c = firstChild(cur); c != nullptr && kids.size() < 1000; c = nextSibling(c)) {
                kids.push_back(c);
            }
            for (auto it = kids.rbegin(); it != kids.rend(); ++it) {
                stack.push_back(*it);
            }
        }
        return sol::make_object(L, sol::lua_nil);
    });
    // w:clone([parent]) -> GuiWindow | nil, reason: a copy of the window with all its children, created below `parent` (default: the window's own parent). The copy is tracked.
    ut.set_function("clone", [resolve, fail](sol::this_state ts, GuiWindow& w, sol::optional<GuiWindow> parent) -> Two {
        sol::state_view L(ts);
        std::string why;
        void* p = resolve(w, why);
        if (p == nullptr) {
            return fail(L, why);
        }
        void* par = nullptr;
        if (parent) {
            par = resolve(*parent, why);
            if (par == nullptr) {
                return fail(L, "parent: " + why);
            }
        } else {
            guarded([&] { par = sym().win_parent(p); });
        }
        void* copy = nullptr;
        if (!guarded([&] { copy = sym().win_clone_into(p, par); }) || copy == nullptr) {
            return fail(L, "the engine could not clone the window");
        }
        g_owned.insert(copy);
        return {sol::make_object(L, GuiWindow{copy}), sol::make_object(L, sol::lua_nil)};
    });
    ut.set_function("set_parent", [resolve, fail, ok](sol::this_state ts, GuiWindow& w, GuiWindow& parent) -> Two {
        sol::state_view L(ts);
        std::string why;
        void* p = resolve(w, why);
        void* par = p == nullptr ? nullptr : resolve(parent, why);
        if (p == nullptr || par == nullptr) {
            return fail(L, why);
        }
        bool done = false;
        if (!guarded([&] { done = sym().win_set_parent(p, par); }) || !done) {
            return fail(L, "the engine refused");
        }
        return ok(L);
    });
    ut.set_function("focus", [resolve, fail, ok](sol::this_state ts, GuiWindow& w) -> Two {
        sol::state_view L(ts);
        std::string why;
        void* p = resolve(w, why);
        if (p == nullptr) {
            return fail(L, why);
        }
        return guarded([&] {
                   sym().win_set_focus(p);
                   sym().win_set_hot(p);
               })
                   ? ok(L)
                   : fail(L, "the engine raised an exception");
    });
    // w:click() -> true | nil, reason: the window's centre gets a mouse move + left button down + up through the GUI module's message entry (eCGuiModule2::OnHandleMessage, what the OS
    // messages of the real mouse end in); a test aid, and a way for a mod to press a button of the engine's menus. The window must be visible.
    ut.set_function("click", [resolve, fail, ok](sol::this_state ts, GuiWindow& w) -> Two {
        sol::state_view L(ts);
        std::string why;
        void* p = resolve(w, why);
        if (p == nullptr) {
            return fail(L, why);
        }
        const Symbols& s = sym();
        if (s.gui_module_instance == nullptr || s.gui_handle_message == nullptr || s.win_abs_position == nullptr) {
            return fail(L, "GUI message symbols are unavailable");
        }
        bool visible = false;
        guarded([&] { visible = s.win_get_visible(p); });
        if (!visible) {
            return fail(L, "the window is not visible");
        }
        int ax = 0, ay = 0, sx = 0, sy = 0;
        int abs[2] = {0, 0};
        if (!guarded([&] { s.win_abs_position(p, abs); }) || !getPoint(p, true, sx, sy)) {
            return fail(L, "the engine raised an exception");
        }
        ax = abs[0] + sx / 2;
        ay = abs[1] + sy / 2;
        int cx = 0, cy = 0;
        if (!virtualToClient(ax, ay, cx, cy)) {
            return fail(L, "the GUI module is not available");
        }
        return sendClick(cx, cy) ? ok(L) : fail(L, "the engine raised an exception");
    });
    // w:center() -> x, y | nil: the window's centre in client pixels of the game window (where a mouse click on it goes)
    ut.set_function("center", [resolve](sol::this_state ts, GuiWindow& w) -> std::tuple<sol::object, sol::object> {
        sol::state_view L(ts);
        const std::tuple<sol::object, sol::object> none{sol::make_object(L, sol::lua_nil), sol::make_object(L, sol::lua_nil)};
        std::string why;
        void* p = resolve(w, why);
        const Symbols& s = sym();
        if (p == nullptr || s.gui_module_instance == nullptr || s.win_abs_position == nullptr) {
            return none;
        }
        int abs[2] = {0, 0}, sx = 0, sy = 0, cx = 0, cy = 0;
        if (!guarded([&] { s.win_abs_position(p, abs); }) || !getPoint(p, true, sx, sy) || !virtualToClient(abs[0] + sx / 2, abs[1] + sy / 2, cx, cy)) {
            return none;
        }
        return {sol::make_object(L, cx), sol::make_object(L, cy)};
    });
    ut.set_function("layout", [resolve, fail, ok](sol::this_state ts, GuiWindow& w) -> Two {
        sol::state_view L(ts);
        std::string why;
        void* p = resolve(w, why);
        if (p == nullptr) {
            return fail(L, why);
        }
        return guarded([&] { sym().win_perform_layout(p); }) ? ok(L) : fail(L, "the engine raised an exception");
    });
    // Reflection on the window (the editor's property mechanism): the property names depend on the class (`Name`, `ID`, `Text`, `OnClick`, ...).
    ut.set_function("properties", [resolve](sol::this_state ts, GuiWindow& w) -> sol::object {
        sol::state_view L(ts);
        std::string why;
        void* p = resolve(w, why);
        return p == nullptr ? sol::make_object(L, sol::lua_nil) : sol::make_object(L, propertyListTable(L, p));
    });
    ut.set_function("get", [resolve](sol::this_state ts, GuiWindow& w, const std::string& name) -> sol::object {
        sol::state_view L(ts);
        std::string why;
        void* p = resolve(w, why);
        if (p == nullptr || name.empty() || name.size() > 64) {
            return sol::make_object(L, sol::lua_nil);
        }
        auto v = propertyGet(p, name);
        return v ? sol::make_object(L, *v) : sol::make_object(L, sol::lua_nil);
    });
    ut.set_function("set", [resolve, fail, ok](sol::this_state ts, GuiWindow& w, const std::string& name, const std::string& value) -> Two {
        sol::state_view L(ts);
        std::string why;
        void* p = resolve(w, why);
        if (p == nullptr) {
            return fail(L, why);
        }
        if (name.empty() || name.size() > 64 || value.size() > 1000) {
            return fail(L, "the property name (64 bytes at most) or value (1000 bytes at most) is not valid");
        }
        return propertySet(p, name, value) ? ok(L) : fail(L, "no such property: " + name.substr(0, 40));
    });
    ut.set_function("__tostring", [](GuiWindow& w) {
        char buf[48];
        snprintf(buf, sizeof buf, "GuiWindow(%p)", w.ptr);
        return std::string(buf);
    });
    ut.set_function("__eq", [](GuiWindow& a, GuiWindow& b) { return a.ptr == b.ptr; });

    // risen.gui.pages() -> { { id, name, visible, window = GuiWindow | nil }, ... } | nil, reason: the engine's page list (gEHudPage)
    gui.set_function("pages", [available, fail](sol::this_state ts) -> Two {
        sol::state_view L(ts);
        const Symbols& s = sym();
        if (!available(s)) {
            return fail(L, "GUI symbols are unavailable");
        }
        sol::table out = L.create_table();
        int n = 0;
        const int count = pageCount();
        for (int i = 0; i < count; ++i) {
            sol::table e = L.create_table();
            e["id"] = i;
            e["name"] = pageName(i);
            e["visible"] = pageVisible(i);
            if (void* p = findPagePtr(i)) {
                e["window"] = GuiWindow{p};
            }
            out[++n] = e;
        }
        return {sol::make_object(L, out), sol::make_object(L, sol::lua_nil)};
    });
    // risen.gui.page(id | "Menu_Main") -> GuiWindow | nil
    gui.set_function("page", [available](sol::this_state ts, sol::object key) -> sol::object {
        sol::state_view L(ts);
        if (!available(sym())) {
            return sol::make_object(L, sol::lua_nil);
        }
        int id = -1;
        if (key.get_type() == sol::type::number) {
            id = key.as<int>();
        } else if (key.get_type() == sol::type::string) {
            std::string want = key.as<std::string>();
            if (want.rfind("gEHudPage_", 0) == 0) {
                want = want.substr(10);  // the enum's own spelling is accepted
            }
            const int count = pageCount();
            for (int i = 0; i < count; ++i) {
                const std::string n = pageName(i);
                if (n == want) {
                    id = i;
                    break;
                }
            }
        }
        if (id < 0 || id >= pageCount()) {
            return sol::make_object(L, sol::lua_nil);
        }
        void* p = findPagePtr(id);
        return p == nullptr ? sol::make_object(L, sol::lua_nil) : sol::make_object(L, GuiWindow{p});
    });
    // risen.gui.open(id) / close(id) -> true | nil, reason: gCHudSystem2::Open/Close through the page scripts (they may deny the request)
    const auto pageAction = [available, fail, ok](bool open) {
        return [=](sol::this_state ts, int id) -> Two {
            sol::state_view L(ts);
            const Symbols& s = sym();
            if (!available(s) || s.gui_open_page == nullptr || s.gui_close_page == nullptr) {
                return fail(L, "GUI symbols are unavailable");
            }
            if (id < 0 || id >= pageCount()) {
                return fail(L, "no such page");
            }
            bool result = false;
            if (!guarded([&] { result = open ? s.gui_open_page(s.gui2_object, id) : s.gui_close_page(s.gui2_object, id); })) {
                return fail(L, "the engine raised an exception");
            }
            return result ? ok(L) : fail(L, "the request was denied");
        };
    };
    gui.set_function("open", pageAction(true));
    gui.set_function("close", pageAction(false));
    // risen.gui.show(id, visible) -> true | nil, reason: shows / hides a page window without the page scripts (GUI2::Show: what the menu pages do for the background page)
    gui.set_function("show", [available, fail, ok](sol::this_state ts, int id, bool visible) -> Two {
        sol::state_view L(ts);
        const Symbols& s = sym();
        if (!available(s) || s.gui_show_page == nullptr) {
            return fail(L, "GUI symbols are unavailable");
        }
        if (id < 0 || id >= pageCount()) {
            return fail(L, "no such page");
        }
        return guarded([&] { s.gui_show_page(s.gui2_object, id, visible); }) ? ok(L) : fail(L, "the engine raised an exception");
    });
    // risen.gui.leaf_page(id [, parent = 13]) -> true | nil, reason: gives the page the controller of the game's menu leaf pages (Credits, Load, ...): ESC / back opens `parent`,
    // the page hides when any other page opens, and showing it also shows the menu background (page 27). The controller is a native script ("Spass_Leaf_<id>") set as the page's
    // `PageControl`; it raises the Lua event `ui.page({ page, kind = "opened" | "closed" | "back" })`. Meant for a page nothing else uses (the retail game never opens `Menu_Cheats`, 25).
    gui.set_function("leaf_page", [available, fail, ok](sol::this_state ts, int id, sol::optional<int> parentArg) -> Two {
        sol::state_view L(ts);
        const Symbols& s = sym();
        if (!available(s) || s.gui_open_page == nullptr || s.gui_show_page == nullptr || s.gui_is_visible == nullptr) {
            return fail(L, "GUI symbols are unavailable");
        }
        const int count = pageCount();
        const int parent = parentArg.value_or(13);
        if (id < 1 || id >= count || parent < 1 || parent >= count || id == parent) {
            return fail(L, "no such page");
        }
        void* page = findPagePtr(id);
        if (page == nullptr) {
            return fail(L, "the page does not exist");
        }
        const std::string name = "Spass_Leaf_" + std::to_string(id);
        const scripts::NativeScript fn = [id, parent](void*, void*, void*, std::int64_t arg, std::int64_t (*)(void*, void*, void*, std::int64_t)) -> std::int64_t {
            const Symbols& sy = sym();
            PageMessage m{};
            if (arg == 0 || !guarded([&] { m = *reinterpret_cast<const PageMessage*>(arg); })) {
                return 0;
            }
            const auto emit = [&](const char* kind) {
                auto& rt = lua::Runtime::instance();
                if (!rt.running() || !rt.hasHandlers("ui.page")) {
                    return;
                }
                auto lock = rt.lock();
                sol::state_view V = rt.lua();
                sol::table ev = V.create_table();
                ev["page"] = id;
                ev["kind"] = kind;
                rt.emit("ui.page", ev);
            };
            std::int64_t result = 0;
            if (m.msg == 0x802d || m.msg == 0x802e) {  // back / ESC
                bool visible = false;
                guarded([&] { visible = sy.gui_is_visible(sy.gui2_object, id); });
                if (visible) {
                    emit("back");
                    guarded([&] { sy.gui_open_page(sy.gui2_object, parent); });
                    result = 1;
                }
            } else if (m.msg == 0x8037) {  // another page opens: this one hides
                if (m.out != nullptr && guarded([&] { *m.out = 0; })) {
                    result = 1;
                }
            } else if (m.msg == 0x8039) {  // shown
                guarded([&] { sy.gui_show_page(sy.gui2_object, 27, true); });
                emit("opened");
            } else if (m.msg == 0x803a) {  // hidden
                emit("closed");
            }
            return result;
        };
        if (const std::string err = scripts::installNative(name, fn, false); !err.empty()) {
            return fail(L, err);
        }
        if (!propertySet(page, "PageControl", name)) {
            return fail(L, "the page has no PageControl property");
        }
        return ok(L);
    });
    // risen.gui.hide_page_on(host, other) -> true | nil, reason: the game's controller of `host` (a menu page such as the main menu, 13) keeps its page visible when `other` opens;
    // this makes it hide, like it does for the pages it knows (needed for a leaf page that was added: the host is the page that opens it).
    gui.set_function("hide_page_on", [available, fail, ok](sol::this_state ts, int host, int other) -> Two {
        sol::state_view L(ts);
        const Symbols& s = sym();
        if (!available(s)) {
            return fail(L, "GUI symbols are unavailable");
        }
        const int count = pageCount();
        if (host < 1 || host >= count || other < 1 || other >= count) {
            return fail(L, "no such page");
        }
        void* page = findPagePtr(host);
        if (page == nullptr) {
            return fail(L, "the page does not exist");
        }
        const auto control = propertyGet(page, "PageControl");
        if (!control || control->empty()) {
            return fail(L, "the page has no controller script");
        }
        const std::string script = *control;
        static std::map<std::string, std::set<int>> extra;  // controller script -> pages that must hide the host
        const bool first = extra.find(script) == extra.end();
        extra[script].insert(other);
        if (!first) {
            return ok(L);
        }
        const scripts::NativeScript fn = [script](void* unit, void* a, void* b, std::int64_t arg, std::int64_t (*original)(void*, void*, void*, std::int64_t)) -> std::int64_t {
            PageMessage m{};
            if (arg != 0 && guarded([&] { m = *reinterpret_cast<const PageMessage*>(arg); }) && m.msg == 0x8037 && m.out != nullptr &&
                extra[script].count(static_cast<int>(m.param)) != 0) {
                if (guarded([&] { *m.out = 0; })) {
                    return 1;
                }
            }
            return original != nullptr ? original(unit, a, b, arg) : 0;
        };
        if (const std::string err = scripts::installNative(script, fn, true); !err.empty()) {
            extra.erase(script);
            return fail(L, err);
        }
        return ok(L);
    });
    // risen.gui.click(x, y) -> true | nil, reason: a left click at the client pixel (x, y) of the game window (mouse move, button down, up through the GUI module's message entry).
    gui.set_function("click", [fail, ok](sol::this_state ts, int x, int y) -> Two {
        sol::state_view L(ts);
        const Symbols& s = sym();
        if (s.gui_module_instance == nullptr || s.gui_handle_message == nullptr) {
            return fail(L, "GUI message symbols are unavailable");
        }
        if (x < 0 || y < 0 || x > 16000 || y > 16000) {
            return fail(L, "position out of range");
        }
        return sendClick(x, y) ? ok(L) : fail(L, "the engine raised an exception");
    });
    // risen.gui.mouse(action, x, y) -> true | nil, reason: one mouse message at the client pixel (x, y): action = "move", "down" or "up" (left button).
    gui.set_function("mouse", [fail, ok](sol::this_state ts, const std::string& action, int x, int y) -> Two {
        sol::state_view L(ts);
        const Symbols& s = sym();
        if (s.gui_module_instance == nullptr || s.gui_handle_message == nullptr) {
            return fail(L, "GUI message symbols are unavailable");
        }
        if (x < 0 || y < 0 || x > 16000 || y > 16000) {
            return fail(L, "position out of range");
        }
        unsigned msg = 0, wparam = 0;
        if (action == "move") {
            msg = 0x200;
        } else if (action == "down") {
            msg = 0x201;
            wparam = 1;
        } else if (action == "up") {
            msg = 0x202;
        } else {
            return fail(L, "action must be \"move\", \"down\" or \"up\"");
        }
        const long lparam = static_cast<long>((static_cast<unsigned>(y) << 16) | (static_cast<unsigned>(x) & 0xffff));
        return handleMessage(msg, wparam, lparam) ? ok(L) : fail(L, "the engine raised an exception");
    });
    // risen.gui.key(vk) -> true | nil, reason: a key press and release (Windows virtual-key code, e.g. 0x26 = Up, 0x28 = Down, 0x0D = Enter, 0x1B = Escape) as the GUI sees OS key messages.
    gui.set_function("key", [fail, ok](sol::this_state ts, int vk) -> Two {
        sol::state_view L(ts);
        const Symbols& s = sym();
        if (s.gui_module_instance == nullptr || s.gui_handle_message == nullptr) {
            return fail(L, "GUI message symbols are unavailable");
        }
        if (vk < 1 || vk > 254) {
            return fail(L, "virtual-key code out of range");
        }
        return sendKey(vk) ? ok(L) : fail(L, "the engine raised an exception");
    });
    // risen.gui.client_size() -> width, height | nil: the game window's client size in pixels
    gui.set_function("client_size", [](sol::this_state ts) -> std::tuple<sol::object, sol::object> {
        sol::state_view L(ts);
        HWND hwnd = FindGameWindow();
        RECT r{};
        if (hwnd == nullptr || !GetClientRect(hwnd, &r)) {
            return {sol::make_object(L, sol::lua_nil), sol::make_object(L, sol::lua_nil)};
        }
        return {sol::make_object(L, static_cast<int>(r.right)), sol::make_object(L, static_cast<int>(r.bottom))};
    });
    // risen.gui.close_menu(): GUI2::CloseMenu (closes the menu pages, back to the game view)
    gui.set_function("close_menu", [available, fail, ok](sol::this_state ts) -> Two {
        sol::state_view L(ts);
        const Symbols& s = sym();
        if (!available(s) || s.gui_close_menu == nullptr) {
            return fail(L, "GUI symbols are unavailable");
        }
        return guarded([&] { s.gui_close_menu(s.gui2_object); }) ? ok(L) : fail(L, "the engine raised an exception");
    });
}

}  // namespace spass::game
