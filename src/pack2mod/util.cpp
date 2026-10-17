#include "pack2mod/util.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace p2m {

std::string lower(std::string_view s) {
    std::string o(s);
    for (char& c : o)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
    return o;
}
bool starts_with(std::string_view s, std::string_view p) { return s.size() >= p.size() && s.compare(0, p.size(), p) == 0; }
bool ends_with(std::string_view s, std::string_view p) { return s.size() >= p.size() && s.compare(s.size() - p.size(), p.size(), p) == 0; }
bool contains(std::string_view s, std::string_view p) { return s.find(p) != std::string_view::npos; }
std::string bare_name(std::string_view path) {
    const size_t k = path.find_last_of("/\\");
    return std::string(k == std::string_view::npos ? path : path.substr(k + 1));
}

std::string utf8_from_cp(uint32_t cp) {
    std::string o;
    if (cp < 0x80) {
        o += static_cast<char>(cp);
    } else if (cp < 0x800) {
        o += static_cast<char>(0xC0 | (cp >> 6));
        o += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        o += static_cast<char>(0xE0 | (cp >> 12));
        o += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        o += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        o += static_cast<char>(0xF0 | (cp >> 18));
        o += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        o += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        o += static_cast<char>(0x80 | (cp & 0x3F));
    }
    return o;
}

std::string latin1_to_utf8(std::string_view s) {
    std::string o;
    o.reserve(s.size());
    for (unsigned char c : s) {
        if (c < 0x80) o += static_cast<char>(c);
        else o += utf8_from_cp(c);
    }
    return o;
}

std::string utf16le_to_utf8(const char* data, size_t units) {
    std::string o;
    o.reserve(units);
    for (size_t i = 0; i < units; ++i) {
        uint32_t u = static_cast<uint8_t>(data[2 * i]) | (static_cast<uint8_t>(data[2 * i + 1]) << 8);
        if (u >= 0xD800 && u < 0xDC00 && i + 1 < units) {
            const uint32_t lo = static_cast<uint8_t>(data[2 * i + 2]) | (static_cast<uint8_t>(data[2 * i + 3]) << 8);
            if (lo >= 0xDC00 && lo < 0xE000) {
                u = 0x10000 + ((u - 0xD800) << 10) + (lo - 0xDC00);
                ++i;
            }
        }
        o += utf8_from_cp(u);
    }
    return o;
}

std::vector<uint32_t> utf8_decode(std::string_view s) {
    std::vector<uint32_t> out;
    for (size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        uint32_t cp;
        int n;
        if (c < 0x80) { cp = c; n = 1; }
        else if ((c >> 5) == 6) { cp = c & 0x1F; n = 2; }
        else if ((c >> 4) == 14) { cp = c & 0x0F; n = 3; }
        else if ((c >> 3) == 30) { cp = c & 0x07; n = 4; }
        else { cp = c; n = 1; }
        if (i + n > s.size()) { cp = c; n = 1; }
        for (int k = 1; k < n; ++k) cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
        out.push_back(cp);
        i += n;
    }
    return out;
}
std::string utf8_encode(const std::vector<uint32_t>& cps) {
    std::string o;
    for (uint32_t c : cps) o += utf8_from_cp(c);
    return o;
}

static bool py_space(uint32_t c) {
    return (c >= 9 && c <= 13) || (c >= 0x1C && c <= 0x20) || c == 0x85 || c == 0xA0 || c == 0x1680 || (c >= 0x2000 && c <= 0x200A) ||
           c == 0x2028 || c == 0x2029 || c == 0x202F || c == 0x205F || c == 0x3000;
}
std::string py_strip(std::string_view s) {
    const auto cps = utf8_decode(s);
    size_t a = 0, b = cps.size();
    while (a < b && py_space(cps[a])) ++a;
    while (b > a && py_space(cps[b - 1])) --b;
    return utf8_encode(std::vector<uint32_t>(cps.begin() + a, cps.begin() + b));
}

std::string replace_all(std::string s, std::string_view from, std::string_view to) {
    if (from.empty()) return s;
    size_t p = 0;
    while ((p = s.find(from, p)) != std::string::npos) {
        s.replace(p, from.size(), to);
        p += to.size();
    }
    return s;
}

double py_round(double x, int digits) {
    char buf[512];
    std::snprintf(buf, sizeof buf, "%.*f", digits, x);
    return std::strtod(buf, nullptr);
}
std::string fmt_g(double x) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%g", x);
    return buf;
}
std::string fmt_g9(double x) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.9g", x);
    return buf;
}
std::string hex_of(std::string_view s) {
    static const char* d = "0123456789abcdef";
    std::string o;
    o.reserve(s.size() * 2);
    for (unsigned char c : s) {
        o += d[c >> 4];
        o += d[c & 15];
    }
    return o;
}

Bytes read_file(const std::string& path) {
    std::ifstream f(std::filesystem::u8path(path), std::ios::binary);
    if (!f) throw Error("cannot open " + path);
    f.seekg(0, std::ios::end);
    const auto n = static_cast<size_t>(f.tellg());
    f.seekg(0);
    Bytes b(n, '\0');
    if (n && !f.read(b.data(), static_cast<std::streamsize>(n))) throw Error("cannot read " + path);
    return b;
}
void write_file(const std::string& path, const Bytes& d) {
    const auto p = std::filesystem::u8path(path);
    std::error_code ec;
    if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path(), ec);
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f) throw Error("cannot write " + path);
    f.write(d.data(), static_cast<std::streamsize>(d.size()));
    if (!f) throw Error("cannot write " + path);
}

}  // namespace p2m
