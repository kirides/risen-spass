#include "core/paths.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace mk {

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)  // u8path is deprecated in C++20
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
fs::path u8(const std::string& s) { return fs::u8path(s); }
#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

std::string u8str(const fs::path& p) {
    const auto s = p.u8string();
    return std::string(s.begin(), s.end());
}

fs::path config_dir() {
    fs::path base;
#ifdef _WIN32
    if (const wchar_t* a = _wgetenv(L"APPDATA")) base = fs::path(a);
#elif defined(__APPLE__)
    if (const char* h = std::getenv("HOME")) base = fs::path(h) / "Library" / "Application Support";
#else
    if (const char* x = std::getenv("XDG_CONFIG_HOME"); x && *x) base = fs::path(x);
    else if (const char* h = std::getenv("HOME")) base = fs::path(h) / ".config";
#endif
    if (base.empty()) base = fs::current_path();
    fs::path dir = base / "risen-modkit";
    std::error_code ec;
    fs::create_directories(dir, ec);
    return dir;
}

bool path_within(const fs::path& inner, const fs::path& outer) {
    std::error_code ec;
    auto a = fs::weakly_canonical(fs::absolute(inner, ec), ec).lexically_normal();
    auto b = fs::weakly_canonical(fs::absolute(outer, ec), ec).lexically_normal();
    auto norm = [](std::string s) {
#ifdef _WIN32
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return c == '\\' ? '/' : static_cast<char>(std::tolower(c)); });
#endif
        while (s.size() > 1 && s.back() == '/') s.pop_back();
        return s;
    };
    const std::string sa = norm(u8str(a)), sb = norm(u8str(b));
    if (sb.empty()) return false;
    return sa == sb || (sa.size() > sb.size() && sa.compare(0, sb.size(), sb) == 0 && sa[sb.size()] == '/');
}

std::string read_text(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) throw std::runtime_error("cannot open " + u8str(p));
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

bool write_text_if_changed(const fs::path& p, const std::string& text) {
    std::error_code ec;
    if (fs::exists(p, ec)) {
        try {
            if (read_text(p) == text) return false;
        } catch (...) {
        }
    }
    if (p.has_parent_path()) fs::create_directories(p.parent_path(), ec);
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f) throw std::runtime_error("cannot write " + u8str(p));
    f.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!f) throw std::runtime_error("cannot write " + u8str(p));
    return true;
}

void write_text(const fs::path& p, const std::string& text) { write_text_if_changed(p, text); }

}  // namespace mk
