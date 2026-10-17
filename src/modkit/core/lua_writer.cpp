#include "core/lua_writer.h"

#include <charconv>
#include <cmath>
#include <set>

namespace mk {

std::string lua_string(const std::string& s) {
    std::string o = "\"";
    for (unsigned char c : s) {
        switch (c) {
            case '"': o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break;
            case '\t': o += "\\t"; break;
            default:
                if (c < 0x20 || c == 0x7f) {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\%03d", c);
                    o += buf;
                } else {
                    o += static_cast<char>(c);  // UTF-8 bytes pass through
                }
        }
    }
    return o + "\"";
}

bool lua_identifier(const std::string& s) {
    static const std::set<std::string> kw = {"and", "break", "do", "else", "elseif", "end", "false", "for", "function", "goto", "if", "in",
                                             "local", "nil", "not", "or", "repeat", "return", "then", "true", "until", "while"};
    if (s.empty() || kw.count(s)) return false;
    if (std::isdigit(static_cast<unsigned char>(s[0]))) return false;
    for (unsigned char c : s)
        if (!(std::isalnum(c) && c < 0x80) && c != '_') return false;
    return true;
}

std::string lua_number(double d) {
    if (!std::isfinite(d)) return "0";
    if (d == std::floor(d) && std::fabs(d) < 1e15) {
        char buf[32];
        std::snprintf(buf, sizeof buf, "%lld", static_cast<long long>(d));
        return buf;
    }
    char buf[64];
    auto r = std::to_chars(buf, buf + sizeof buf, d);
    return std::string(buf, r.ptr);
}

namespace {

std::string scalar(const json& v) {
    if (v.is_null()) return "nil";
    if (v.is_boolean()) return v.get<bool>() ? "true" : "false";
    if (v.is_number_integer()) return std::to_string(v.get<long long>());
    if (v.is_number_unsigned()) return std::to_string(v.get<unsigned long long>());
    if (v.is_number_float()) return lua_number(v.get<double>());
    if (v.is_string()) return lua_string(v.get<std::string>());
    return "nil";
}

std::string key_text(const std::string& k) { return lua_identifier(k) ? k : "[" + lua_string(k) + "]"; }

}  // namespace

std::string lua_value(const json& v, int indent, size_t inline_width) {
    if (!v.is_object() && !v.is_array()) return scalar(v);
    if (v.empty()) return "{}";
    // one line first
    std::string line = "{ ";
    bool first = true;
    bool nested = false;
    for (auto it = v.begin(); it != v.end(); ++it) {
        if (!first) line += ", ";
        first = false;
        if (v.is_object()) line += key_text(it.key()) + " = ";
        if (it->is_object() || it->is_array()) {
            nested = true;
            line += lua_value(*it, indent + 1, inline_width);
        } else {
            line += scalar(*it);
        }
    }
    line += " }";
    if (line.size() + static_cast<size_t>(indent) * 4 <= inline_width && line.find('\n') == std::string::npos && (!nested || line.size() < 80)) return line;
    // one entry per line
    const std::string pad(static_cast<size_t>(indent + 1) * 4, ' ');
    std::string o = "{\n";
    for (auto it = v.begin(); it != v.end(); ++it) {
        o += pad;
        if (v.is_object()) o += key_text(it.key()) + " = ";
        o += lua_value(*it, indent + 1, inline_width) + ",\n";
    }
    o += std::string(static_cast<size_t>(indent) * 4, ' ') + "}";
    return o;
}

}  // namespace mk
