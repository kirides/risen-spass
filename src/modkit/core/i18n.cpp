#include "core/i18n.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <map>
#include <mutex>
#include <unordered_map>

#include <toml++/toml.hpp>

#include "core/log.h"

#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

namespace mk::i18n {

namespace {

struct StringHash {
    using is_transparent = void;
    size_t operator()(std::string_view s) const { return std::hash<std::string_view>{}(s); }
    size_t operator()(const std::string& s) const { return std::hash<std::string_view>{}(s); }
    size_t operator()(const char* s) const { return std::hash<std::string_view>{}(s); }
};
using Table = std::unordered_map<std::string, std::string, StringHash, std::equal_to<>>;

struct State {
    std::mutex mutex;
    Table table;  // key -> translation; not modified between set_language calls, so returned pointers stay valid
    Table memo;   // "Label##id" -> "Beschriftung##id"
    std::string code = "en";
};

State& state() {
    static State s;
    return s;
}

std::string lower(std::string s) {
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

fs::path exe_dir() {
#ifdef _WIN32
    wchar_t buf[MAX_PATH * 4];
    const DWORD n = GetModuleFileNameW(nullptr, buf, (DWORD)std::size(buf));
    if (n > 0 && n < std::size(buf)) return fs::path(std::wstring(buf, n)).parent_path();
#elif defined(__APPLE__)
    char buf[4096];
    uint32_t size = sizeof(buf);
    if (_NSGetExecutablePath(buf, &size) == 0) {
        std::error_code ec;
        return fs::weakly_canonical(buf, ec).parent_path();
    }
#else
    std::error_code ec;
    const auto p = fs::read_symlink("/proc/self/exe", ec);
    if (!ec) return p.parent_path();
#endif
    return {};
}

std::string_view str_of(const toml::node* n) {
    if (n && n->is_string()) return n->as_string()->get();
    return {};
}

}  // namespace

bool parse_catalog(std::string_view text, Catalog& out, std::string* error) {
    out = {};
    toml::table t;
    try {
        t = toml::parse(text);
    } catch (const toml::parse_error& e) {
        if (error) *error = std::string(e.description()) + " (line " + std::to_string(e.source().begin.line) + ")";
        return false;
    }
    if (const auto* meta = t["meta"].as_table()) {
        out.info.code = lower(std::string(str_of(meta->get("code"))));
        out.info.name = std::string(str_of(meta->get("name")));
        out.info.english_name = std::string(str_of(meta->get("english_name")));
    }
    if (const auto* strings = t["strings"].as_table()) {
        for (const auto& [k, v] : *strings) {
            if (!v.is_string()) {
                if (error) *error = "[strings] \"" + std::string(k.str()) + "\" is not a string";
                return false;
            }
            out.entries.emplace_back(std::string(k.str()), v.as_string()->get());
        }
    }
    return true;
}

std::vector<fs::path> default_search_dirs() {
    std::vector<fs::path> dirs;
    if (const auto e = exe_dir(); !e.empty()) dirs.push_back(e / "lang");
    try {
        dirs.push_back(config_dir() / "lang");
    } catch (const std::exception&) {
    }
    return dirs;
}

std::vector<LanguageInfo> discover(const std::vector<fs::path>& dirs) {
    std::map<std::string, LanguageInfo> by_code;  // a later directory replaces an earlier file with the same code
    for (const auto& dir : dirs) {
        std::error_code ec;
        if (!fs::is_directory(dir, ec)) continue;
        std::vector<fs::path> files;
        for (const auto& e : fs::directory_iterator(dir, ec))
            if (e.is_regular_file(ec) && lower(u8str(e.path().extension())) == ".toml") files.push_back(e.path());
        std::sort(files.begin(), files.end());
        for (const auto& f : files) {
            Catalog c;
            std::string err;
            try {
                if (!parse_catalog(read_text(f), c, &err)) {
                    log_warn("language file " + u8str(f) + " ignored: " + err);
                    continue;
                }
            } catch (const std::exception& e) {
                log_warn("language file " + u8str(f) + " ignored: " + e.what());
                continue;
            }
            if (c.info.code.empty()) continue;  // e.g. the template
            if (c.info.name.empty()) c.info.name = c.info.code;
            c.info.path = u8str(f);
            by_code[c.info.code] = c.info;
        }
    }
    std::vector<LanguageInfo> v;
    for (auto& [code, info] : by_code) v.push_back(std::move(info));
    return v;
}

std::string system_language() {
    std::string tag;
#ifdef _WIN32
    wchar_t buf[LOCALE_NAME_MAX_LENGTH] = {};
    if (GetUserDefaultLocaleName(buf, LOCALE_NAME_MAX_LENGTH) > 0)
        for (wchar_t* c = buf; *c; ++c) tag += (*c < 128) ? (char)*c : '?';
#else
    for (const char* var : {"LC_ALL", "LC_MESSAGES", "LANG"}) {
        const char* v = std::getenv(var);
        if (v && *v) {
            tag = v;
            break;
        }
    }
#endif
    // "de-DE", "de_DE.UTF-8", "pt_BR@euro" -> "de", "pt-br"
    tag = lower(tag);
    tag = tag.substr(0, tag.find_first_of(".@"));
    std::replace(tag.begin(), tag.end(), '_', '-');
    if (tag.empty() || tag == "c" || tag == "posix") return "en";
    return tag;
}

namespace {

void clear_locked(State& s) {
    s.table.clear();
    s.memo.clear();
    s.code = "en";
}

bool activate(const Catalog& c, std::string* error) {
    State& s = state();
    std::lock_guard lock(s.mutex);
    clear_locked(s);
    if (c.info.code.empty()) {
        if (error) *error = "the [meta] table has no code";
        return false;
    }
    for (const auto& [k, v] : c.entries)
        if (!v.empty()) s.table[k] = v;  // an empty translation counts as "not translated yet"
    s.code = c.info.code;
    return true;
}

}  // namespace

bool set_language_from_toml(std::string_view text, std::string* error) {
    Catalog c;
    if (!parse_catalog(text, c, error)) {
        std::lock_guard lock(state().mutex);
        clear_locked(state());
        return false;
    }
    return activate(c, error);
}

bool set_language(const std::string& code_in, const std::vector<fs::path>& dirs, std::string* error) {
    std::string code = lower(code_in);
    const bool automatic = code == "auto";
    if (automatic) code = system_language();
    if (code.empty() || code == "en" || code.rfind("en-", 0) == 0) {
        std::lock_guard lock(state().mutex);
        clear_locked(state());
        return true;
    }
    const auto langs = discover(dirs);
    auto find_code = [&](const std::string& c) -> const LanguageInfo* {
        for (const auto& l : langs)
            if (l.code == c) return &l;
        return nullptr;
    };
    const LanguageInfo* found = find_code(code);  // "pt-br", then "pt"
    if (!found) found = find_code(code.substr(0, code.find('-')));
    if (!found) {
        std::lock_guard lock(state().mutex);
        clear_locked(state());
        if (automatic) return true;  // no file for the system language: English is the normal result, not an error
        if (error) *error = "no language file for \"" + code_in + "\"";
        return false;
    }
    Catalog c;
    std::string err;
    try {
        if (!parse_catalog(read_text(u8(found->path)), c, &err)) {
            if (error) *error = found->path + ": " + err;
            std::lock_guard lock(state().mutex);
            clear_locked(state());
            return false;
        }
    } catch (const std::exception& e) {
        if (error) *error = found->path + ": " + e.what();
        std::lock_guard lock(state().mutex);
        clear_locked(state());
        return false;
    }
    if (c.info.code.empty()) c.info.code = found->code;
    return activate(c, error);
}

const std::string& language() { return state().code; }

size_t entry_count() {
    State& s = state();
    std::lock_guard lock(s.mutex);
    return s.table.size();
}

const char* tr(const char* english) {
    if (!english || !*english) return english;
    State& s = state();
    std::lock_guard lock(s.mutex);
    if (s.table.empty()) return english;
    if (auto it = s.table.find(std::string_view(english)); it != s.table.end()) return it->second.c_str();
    const std::string_view full(english);
    const size_t h = full.find("##");  // also matches "###": the id part is kept as written
    if (h == std::string_view::npos || h == 0) return english;
    if (auto it = s.memo.find(full); it != s.memo.end()) return it->second.c_str();
    auto it = s.table.find(full.substr(0, h));
    if (it == s.table.end()) return english;
    return s.memo.emplace(std::string(full), it->second + std::string(full.substr(h))).first->second.c_str();
}

const char* trc(const char* context, const char* english) {
    if (!english || !*english) return english;
    {
        State& s = state();
        std::lock_guard lock(s.mutex);
        if (s.table.empty()) return english;
        thread_local std::string key;
        key.assign(context ? context : "");
        key += '|';
        key += english;
        if (auto it = s.table.find(std::string_view(key)); it != s.table.end()) return it->second.c_str();
    }
    return tr(english);
}

std::string tr(const std::string& english) { return tr(english.c_str()); }

std::string format(std::string_view text, std::initializer_list<std::string_view> args) {
    std::string out;
    out.reserve(text.size() + 16);
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if ((c == '{' || c == '}') && i + 1 < text.size() && text[i + 1] == c) {
            out += c;
            ++i;
        } else if (c == '{') {
            const size_t close = text.find('}', i);
            if (close != std::string_view::npos && close > i + 1 &&
                std::all_of(text.begin() + i + 1, text.begin() + close, [](char d) { return d >= '0' && d <= '9'; })) {
                const size_t n = (size_t)std::atoi(std::string(text.substr(i + 1, close - i - 1)).c_str());
                if (n < args.size()) out += *(args.begin() + n);
                i = close;
            } else {
                out += c;
            }
        } else {
            out += c;
        }
    }
    return out;
}

std::string trf(const char* english, std::initializer_list<std::string_view> args) { return format(tr(english), args); }

}  // namespace mk::i18n
