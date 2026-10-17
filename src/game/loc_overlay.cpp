#include "game/loc_overlay.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <unordered_set>

#include <windows.h>

#include <spdlog/spdlog.h>

#include "game/abi.h"
#include "hook/hook_manager.h"

// String ids look like "<prefix>:<key>" (e.g. "info:Diego_Hello"); the prefix selects one of the tables
// declared in data/ini/loc.ini. CFXTableMap::LookupValue(lang, key, out) / LookupValue(key, out) is the single
// place where text is read, so overriding it overrides every consumer (HUD, dialogue, quest log, ...).

namespace spass::game {
namespace {

struct Entry {
    std::wstring text;
    std::string language;  // empty = every language
};

std::mutex g_mutex;
std::unordered_map<std::string, std::vector<Entry>> g_overlay;  // key: folded id
std::atomic<bool> g_trace{false};
std::unordered_set<std::string> g_traced;

std::string fold(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string toUtf8(const void* ustring) {
    return ustring != nullptr ? UString::narrow(sym().ustring_get_text(ustring)) : std::string{};
}

// The overlay entry for `id`; a prefixed id ("focus:FO_Bob") also finds an entry stored without the table prefix ("FO_Bob"): the engine
// asks for both spellings (eCLocString::ExistsID / GetText use the prefixed id, the HUD the bare key), a mod sets whichever it knows.
// Call with g_mutex held.
std::unordered_map<std::string, std::vector<Entry>>::iterator findEntry(const std::string& id) {
    auto it = g_overlay.find(fold(id));
    if (it == g_overlay.end()) {
        const auto colon = id.find(':');
        if (colon != std::string::npos && colon + 1 < id.size()) {
            it = g_overlay.find(fold(id.substr(colon + 1)));
        }
    }
    return it;
}

std::optional<std::wstring> find(const std::string& id, const std::string& lang) {
    std::lock_guard lock(g_mutex);
    const auto it = findEntry(id);
    if (it == g_overlay.end()) {
        return std::nullopt;
    }
    const std::string wanted = fold(lang);
    const Entry* any = nullptr;
    for (const Entry& e : it->second) {
        if (e.language.empty()) {
            any = &e;
        } else if (fold(e.language) == wanted) {
            return e.text;  // a language-specific entry wins
        }
    }
    if (any != nullptr) {
        return any->text;
    }
    return std::nullopt;
}

std::string currentLanguage() {
    std::string name;
    guarded([&] {
        OutString out;
        sym().loc_get_text_language(sym().loc_instance(), out.ptr());
        name = out.take();
    });
    return name;
}

void trace(const char* what, const std::string& key, const std::string& lang, bool found) {
    if (!g_trace) {
        return;
    }
    std::lock_guard lock(g_mutex);
    if (g_traced.size() < 5000 && g_traced.insert(key).second) {
        spdlog::info("loc: {} key='{}' lang='{}'{}", what, key, lang, found ? " -> overridden" : "");
    }
}

Hook<bool (*)(const void*, const void*, const void*, void*)> g_lookup3;
Hook<bool (*)(const void*, const void*, void*)> g_lookup2;
Hook<bool (*)(const void*, const void*)> g_hasKey;

bool detourLookup3(const void* self, const void* lang, const void* key, void* out) {
    const std::string k = toUtf8(key);
    const std::string l = toUtf8(lang);
    if (auto text = find(k, l)) {
        sym().ustring_assign_wide(out, text->c_str());
        trace("lookup(override)", k, l, true);
        return true;
    }
    const bool found = g_lookup3.original(self, lang, key, out);
    trace("lookup", k, l, found);
    return found;
}

bool hasOverlay(const std::string& id) {
    std::lock_guard lock(g_mutex);
    return findEntry(id) != g_overlay.end();
}

bool detourLookup2(const void* self, const void* key, void* out) {
    const std::string k = toUtf8(key);
    // Only resolve the (comparatively expensive) current language when the key is actually overridden.
    if (hasOverlay(k)) {
        if (auto text = find(k, currentLanguage())) {
            sym().ustring_assign_wide(out, text->c_str());
            trace("lookup(override)", k, "", true);
            return true;
        }
    }
    const bool found = g_lookup2.original(self, key, out);
    trace("lookup", k, "", found);
    return found;
}

bool detourHasKey(const void* self, const void* key) {
    return g_hasKey.original(self, key) || hasOverlay(toUtf8(key));
}

// ---- CFXLocString / CFXLocStringEx accessors ----------------------------------------------------------
// Text objects (eCLocString, gCInfoLocString, ...) read their text through these accessors, so overriding
// them overrides what the player sees. They return their result through a hidden pointer (RDX) that the
// callee constructs. `Tag` only exists to give every hook its own trampoline.

std::atomic<std::size_t> g_overlayCount{0};

std::string keyOf(const void* self) {
    OutUString out;
    sym().locstr_get_key(self, out.ptr());
    return out.take();
}

// Looks up the overlay for the loc-string object `self`; `lang` is the explicit column/language if the
// accessor had one.
std::optional<std::wstring> overlayFor(const void* self, const std::string& lang) {
    if (g_overlayCount.load(std::memory_order_relaxed) == 0 && !g_trace) {
        return std::nullopt;
    }
    std::string key;
    const bool ok = guarded([&] { key = keyOf(self); });
    if (!ok || key.empty()) {
        return std::nullopt;
    }
    if (!hasOverlay(key)) {
        trace("text", key, lang, false);
        return std::nullopt;
    }
    auto text = find(key, lang.empty() ? currentLanguage() : lang);
    if (text) {
        trace("text(override)", key, lang, true);
    }
    return text;
}

std::string ansi(const std::wstring& w) {
    if (w.empty()) {
        return {};
    }
    const int n = WideCharToMultiByte(CP_ACP, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_ACP, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

// bCUnicodeString GetText()/GetPlainText()/operator bCUnicodeString()
template <class Tag>
struct UnicodeAccessor {
    static inline Hook<void* (*)(const void*, void*)> hook;
    static void* detour(const void* self, void* ret) {
        if (auto t = overlayFor(self, {})) {
            return sym().ustring_ctor_wide(ret, t->c_str());
        }
        return hook.original(self, ret);
    }
};

// same, with an explicit column: the argument is a bCString (isAnsi) or bCUnicodeString naming the language
template <class Tag, bool IsAnsi>
struct UnicodeAccessorCol {
    static inline Hook<void* (*)(const void*, void*, const void*)> hook;
    static void* detour(const void* self, void* ret, const void* col) {
        const std::string lang = IsAnsi ? std::string(sym().bstring_get_text(col)) : toUtf8(col);
        if (auto t = overlayFor(self, lang)) {
            return sym().ustring_ctor_wide(ret, t->c_str());
        }
        return hook.original(self, ret, col);
    }
};

// operator bCString()
template <class Tag>
struct AnsiAccessor {
    static inline Hook<void* (*)(const void*, void*)> hook;
    static void* detour(const void* self, void* ret) {
        if (auto t = overlayFor(self, {})) {
            return sym().bstring_ctor_cstr(ret, ansi(*t).c_str());
        }
        return hook.original(self, ret);
    }
};

struct TGetText {}; struct TGetPlain {}; struct TExGetText {}; struct TExGetPlain {}; struct TExToU {};
struct TToB {}; struct TExToB {};
struct TTextA {}; struct TTextW {}; struct TPlainA {}; struct TPlainW {};

}  // namespace

bool installLocHooks() {
    const Symbols& s = sym();
    bool ok = true;
    ok &= UnicodeAccessor<TGetText>::hook.install("CFXLocString::GetText()", s.locstr_get_text, &UnicodeAccessor<TGetText>::detour);
    ok &= UnicodeAccessor<TGetPlain>::hook.install("CFXLocString::GetPlainText()", s.locstr_get_plain, &UnicodeAccessor<TGetPlain>::detour);
    ok &= UnicodeAccessor<TExGetText>::hook.install("CFXLocStringEx::GetText()", s.locstrex_get_text, &UnicodeAccessor<TExGetText>::detour);
    ok &= UnicodeAccessor<TExGetPlain>::hook.install("CFXLocStringEx::GetPlainText()", s.locstrex_get_plain, &UnicodeAccessor<TExGetPlain>::detour);
    ok &= UnicodeAccessor<TExToU>::hook.install("CFXLocStringEx::operator bCUnicodeString", s.locstrex_to_ustring, &UnicodeAccessor<TExToU>::detour);
    ok &= UnicodeAccessorCol<TTextA, true>::hook.install("CFXLocString::GetText(bCString)", s.locstr_get_text_col_a, &UnicodeAccessorCol<TTextA, true>::detour);
    ok &= UnicodeAccessorCol<TTextW, false>::hook.install("CFXLocString::GetText(bCUnicodeString)", s.locstr_get_text_col_w, &UnicodeAccessorCol<TTextW, false>::detour);
    ok &= UnicodeAccessorCol<TPlainA, true>::hook.install("CFXLocString::GetPlainText(bCString)", s.locstr_get_plain_col_a, &UnicodeAccessorCol<TPlainA, true>::detour);
    ok &= UnicodeAccessorCol<TPlainW, false>::hook.install("CFXLocString::GetPlainText(bCUnicodeString)", s.locstr_get_plain_col_w, &UnicodeAccessorCol<TPlainW, false>::detour);
    ok &= AnsiAccessor<TToB>::hook.install("CFXLocString::operator bCString", s.locstr_to_bstring, &AnsiAccessor<TToB>::detour);
    ok &= AnsiAccessor<TExToB>::hook.install("CFXLocStringEx::operator bCString", s.locstrex_to_bstring, &AnsiAccessor<TExToB>::detour);
    ok &= g_lookup3.install("CFXTableMap::LookupValue(lang,key)", s.table_lookup_value3, &detourLookup3);
    ok &= g_lookup2.install("CFXTableMap::LookupValue(key)", s.table_lookup_value2, &detourLookup2);
    ok &= g_hasKey.install("CFXTableMap::HasKey", s.table_has_key, &detourHasKey);
    return ok;
}

bool setOverlayString(const std::string& id, const std::string& text, const std::string& language) {
    if (id.empty()) {
        return false;
    }
    std::lock_guard lock(g_mutex);
    auto& entries = g_overlay[fold(id)];
    std::erase_if(entries, [&](const Entry& e) { return fold(e.language) == fold(language); });
    entries.push_back(Entry{UString::widen(text), language});
    g_overlayCount = g_overlay.size();
    return true;
}

void bindLoc(sol::state_view lua, sol::table risen) {
    sol::table loc = lua.create_table();
    const Symbols& s = sym();

    // risen.loc.set(id, text [, language]): `text` is UTF-8. Without `language` it applies to every language.
    // Ids are "<prefix>:<key>" (prefixes: see data/ini/loc.ini), matched case-insensitively.
    loc.set_function("set", [](const std::string& id, const std::string& text, sol::optional<std::string> language) {
        return setOverlayString(id, text, language.value_or(""));
    });
    loc.set_function("clear", [](const std::string& id) {
        std::lock_guard lock(g_mutex);
        const bool removed = g_overlay.erase(fold(id)) > 0;
        g_overlayCount = g_overlay.size();
        return removed;
    });
    // risen.loc.get(id) -> overlay text, else the game's text, else nil.
    loc.set_function("get", [&s](const std::string& id) -> sol::optional<std::string> {
        if (auto t = find(id, currentLanguage())) {
            return UString::narrow(t->c_str());
        }
        std::string text;
        bool found = false;
        const BString key(id);
        guarded([&] {
            void* admin = s.loc_instance();
            if (!s.loc_exists_id(admin, key.get())) {
                return;
            }
            found = true;
            // The game's own text: the string table's LookupValue(language, id). (eCLocAdmin2::GetString(id) is a one-argument wrapper that forwards `this` as its
            // second string and returns nothing useful: every game string read through it was empty.)
            const UString name(id);
            const std::string language = currentLanguage();
            for (const std::string& lang : {language, language + "_Text"}) {
                const UString l(lang);
                UString out;
                if (s.table_lookup_value3(s.loctable_instance(), l.get(), name.get(), out.raw())) {
                    text = out.utf8();
                    break;
                }
            }
        });
        if (!found) {
            return sol::nullopt;
        }
        return text;
    });
    loc.set_function("exists", [&s](const std::string& id) {
        if (hasOverlay(id)) {
            return true;
        }
        bool r = false;
        const BString key(id);
        guarded([&] { r = s.loc_exists_id(s.loc_instance(), key.get()); });
        return r;
    });
    // risen.loc.language() -> name of the active text language.
    loc.set_function("language", []() { return currentLanguage(); });
    // risen.loc.trace(true): log every distinct string id the game asks for (discover ids to override).
    loc.set_function("trace", [](bool on) { g_trace = on; });
    risen["loc"] = loc;
}

}  // namespace spass::game
