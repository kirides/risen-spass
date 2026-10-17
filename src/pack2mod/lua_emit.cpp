#include "pack2mod/lua_emit.h"

#include <algorithm>
#include <cmath>
#include <set>

#include "pack2mod/util.h"

namespace p2m {

LVal& LVal::set(const std::string& key, LVal v) {
    for (auto& f : fields)
        if (f.first == key) {
            f.second = std::move(v);
            return f.second;
        }
    fields.emplace_back(key, std::move(v));
    return fields.back().second;
}
LVal* LVal::get(const std::string& key) {
    for (auto& f : fields)
        if (f.first == key) return &f.second;
    return nullptr;
}
const LVal* LVal::get(const std::string& key) const {
    for (const auto& f : fields)
        if (f.first == key) return &f.second;
    return nullptr;
}
LVal& LVal::sub(const std::string& key, bool ordered_keys) {
    if (LVal* e = get(key)) return *e;
    return set(key, LVal::dict(ordered_keys));
}

std::string lua_key(const std::string& k) {
    static const std::set<std::string> kw = {"end", "and", "or", "not", "if", "then", "else", "for", "in", "do", "nil", "true", "false",
                                             "function", "local", "return", "repeat", "until", "while", "break", "elseif"};
    bool ident = !k.empty() && (std::isalpha(static_cast<unsigned char>(k[0])) || k[0] == '_');
    for (char c : k)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_')) ident = false;
    if (ident && !kw.count(k)) return k;
    return "[\"" + k + "\"]";
}

std::string lua_quote(const std::string& s) {
    std::string o = "\"";
    for (char c : s) {
        switch (c) {
            case '\\': o += "\\\\"; break;
            case '"': o += "\\\""; break;
            case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break;
            default: o += c;
        }
    }
    return o + "\"";
}

namespace {

bool scalar(const LVal& v) { return v.kind != LVal::List && v.kind != LVal::Dict; }

std::vector<const std::pair<std::string, LVal>*> keys_of(const LVal& v) {
    std::vector<const std::pair<std::string, LVal>*> k;
    for (const auto& f : v.fields) k.push_back(&f);
    if (!v.ordered)
        std::stable_sort(k.begin(), k.end(), [](auto* a, auto* b) { return a->first < b->first; });
    return k;
}

std::string number_text(const LVal& v) {
    if (v.kind == LVal::Int) return std::to_string(v.i);
    if (v.d == std::floor(v.d) && std::fabs(v.d) < 1e15) return std::to_string(static_cast<long long>(v.d));
    return fmt_g9(v.d);
}

}  // namespace

std::string to_lua(const LVal& v, int ind) {
    const std::string pad(static_cast<size_t>(ind) * 4, ' ');
    switch (v.kind) {
        case LVal::Nil: return "nil";
        case LVal::Bool: return v.b ? "true" : "false";
        case LVal::Int:
        case LVal::Float: return number_text(v);
        case LVal::Str: return lua_quote(v.s);
        case LVal::List: {
            if (v.items.empty()) return "{}";
            bool all_scalar = true;
            for (const LVal& x : v.items) all_scalar = all_scalar && scalar(x);
            if (all_scalar) {
                std::string o = "{ ";
                for (size_t i = 0; i < v.items.size(); ++i) o += (i ? ", " : "") + to_lua(v.items[i], 0);
                return o + " }";
            }
            std::string o = "{\n";
            for (const LVal& x : v.items) o += pad + "    " + to_lua(x, ind + 1) + ",\n";
            return o + pad + "}";
        }
        case LVal::Dict: {
            if (v.fields.empty()) return "{}";
            bool simple = true;
            for (const auto& f : v.fields) simple = simple && scalar(f.second);
            const auto ks = keys_of(v);
            if (simple && ks.size() <= 6) {
                std::string o = "{ ";
                for (size_t i = 0; i < ks.size(); ++i) o += (i ? ", " : "") + lua_key(ks[i]->first) + " = " + to_lua(ks[i]->second, 0);
                return o + " }";
            }
            std::string o = "{\n";
            for (const auto* f : ks) o += pad + "    " + lua_key(f->first) + " = " + to_lua(f->second, ind + 1) + ",\n";
            return o + pad + "}";
        }
    }
    return "nil";
}

namespace {
std::string json_quote(const std::string& s) {
    std::string o = "\"";
    for (unsigned char c : s) {
        switch (c) {
            case '\\': o += "\\\\"; break;
            case '"': o += "\\\""; break;
            case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break;
            case '\t': o += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\u%04x", c);
                    o += buf;
                } else {
                    o += static_cast<char>(c);
                }
        }
    }
    return o + "\"";
}
}  // namespace

std::string to_json(const LVal& v, int ind) {
    const std::string pad(static_cast<size_t>(ind + 1), ' ');
    const std::string pad0(static_cast<size_t>(ind), ' ');
    switch (v.kind) {
        case LVal::Nil: return "null";
        case LVal::Bool: return v.b ? "true" : "false";
        case LVal::Int:
        case LVal::Float: return number_text(v);
        case LVal::Str: return json_quote(v.s);
        case LVal::List: {
            if (v.items.empty()) return "[]";
            std::string o = "[\n";
            for (size_t i = 0; i < v.items.size(); ++i) o += pad + to_json(v.items[i], ind + 1) + (i + 1 < v.items.size() ? ",\n" : "\n");
            return o + pad0 + "]";
        }
        case LVal::Dict: {
            if (v.fields.empty()) return "{}";
            std::string o = "{\n";
            const auto ks = keys_of(v);
            for (size_t i = 0; i < ks.size(); ++i)
                o += pad + json_quote(ks[i]->first) + ": " + to_json(ks[i]->second, ind + 1) + (i + 1 < ks.size() ? ",\n" : "\n");
            return o + pad0 + "}";
        }
    }
    return "null";
}

}  // namespace p2m
