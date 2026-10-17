#include "pack2mod/json_in.h"

#include <cstdlib>

#include "pack2mod/util.h"

namespace p2m {

namespace {

struct P {
    const std::string& s;
    size_t i = 0;

    [[noreturn]] void fail(const char* what) const { throw Error(std::string("JSON: ") + what + " at offset " + std::to_string(i)); }
    void ws() {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) ++i;
    }
    bool lit(const char* w) {
        const size_t n = std::char_traits<char>::length(w);
        if (s.compare(i, n, w) == 0) { i += n; return true; }
        return false;
    }
    std::string str() {
        if (s[i] != '"') fail("string expected");
        ++i;
        std::string o;
        while (i < s.size() && s[i] != '"') {
            char c = s[i++];
            if (c != '\\') { o += c; continue; }
            if (i >= s.size()) fail("bad escape");
            c = s[i++];
            switch (c) {
                case 'n': o += '\n'; break;
                case 't': o += '\t'; break;
                case 'r': o += '\r'; break;
                case 'b': o += '\b'; break;
                case 'f': o += '\f'; break;
                case 'u': {
                    if (i + 4 > s.size()) fail("bad \\u escape");
                    uint32_t cp = static_cast<uint32_t>(std::strtoul(s.substr(i, 4).c_str(), nullptr, 16));
                    i += 4;
                    if (cp >= 0xD800 && cp < 0xDC00 && s.compare(i, 2, "\\u") == 0) {
                        const uint32_t lo = static_cast<uint32_t>(std::strtoul(s.substr(i + 2, 4).c_str(), nullptr, 16));
                        i += 6;
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    }
                    o += utf8_from_cp(cp);
                    break;
                }
                default: o += c;
            }
        }
        if (i >= s.size()) fail("unterminated string");
        ++i;
        return o;
    }
    LVal value() {
        ws();
        if (i >= s.size()) fail("unexpected end");
        const char c = s[i];
        if (c == '{') {
            ++i;
            LVal d = LVal::dict(true);
            ws();
            if (s[i] == '}') { ++i; return d; }
            for (;;) {
                ws();
                std::string k = str();
                ws();
                if (s[i] != ':') fail("':' expected");
                ++i;
                d.set(k, value());
                ws();
                if (s[i] == ',') { ++i; continue; }
                if (s[i] == '}') { ++i; return d; }
                fail("',' or '}' expected");
            }
        }
        if (c == '[') {
            ++i;
            LVal l = LVal::list();
            ws();
            if (s[i] == ']') { ++i; return l; }
            for (;;) {
                l.push(value());
                ws();
                if (s[i] == ',') { ++i; continue; }
                if (s[i] == ']') { ++i; return l; }
                fail("',' or ']' expected");
            }
        }
        if (c == '"') return LVal::str(str());
        if (lit("true")) return LVal::boolean(true);
        if (lit("false")) return LVal::boolean(false);
        if (lit("null")) return LVal::nil();
        const size_t st = i;
        while (i < s.size() && (std::isdigit(static_cast<unsigned char>(s[i])) || s[i] == '-' || s[i] == '+' || s[i] == '.' || s[i] == 'e' || s[i] == 'E')) ++i;
        if (st == i) fail("value expected");
        const std::string num = s.substr(st, i - st);
        if (num.find_first_of(".eE") == std::string::npos) return LVal::integer(std::strtoll(num.c_str(), nullptr, 10));
        return LVal::real(std::strtod(num.c_str(), nullptr));
    }
};

}  // namespace

LVal parse_json(const std::string& text) {
    P p{text};
    // skip a UTF-8 BOM
    if (text.compare(0, 3, "\xEF\xBB\xBF") == 0) p.i = 3;
    LVal v = p.value();
    p.ws();
    if (p.i != text.size()) p.fail("trailing data");
    return v;
}

}  // namespace p2m
