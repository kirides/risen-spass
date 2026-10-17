// A small value tree that prints as a Lua table constructor (the data files of a converted mod, `return { ... }`) and as JSON (manifest.json).
// The Lua layout is the one of tools/extmod_gen.py `lua()` so that the two generators produce byte-identical files.
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace p2m {

struct LVal {
    enum Kind { Nil, Bool, Int, Float, Str, List, Dict } kind = Nil;
    bool b = false;
    int64_t i = 0;
    double d = 0;
    std::string s;
    std::vector<LVal> items;                           // List
    std::vector<std::pair<std::string, LVal>> fields;  // Dict: insertion ordered; `ordered` keeps that order, otherwise keys print sorted
    bool ordered = false;

    LVal() = default;
    static LVal nil() { return LVal(); }
    static LVal boolean(bool x) { LVal v; v.kind = Bool; v.b = x; return v; }
    static LVal integer(int64_t x) { LVal v; v.kind = Int; v.i = x; return v; }
    static LVal real(double x) { LVal v; v.kind = Float; v.d = x; return v; }
    static LVal str(std::string x) { LVal v; v.kind = Str; v.s = std::move(x); return v; }
    static LVal list() { LVal v; v.kind = List; return v; }
    static LVal dict(bool ordered_keys = false) { LVal v; v.kind = Dict; v.ordered = ordered_keys; return v; }

    // Dict assignment like Python's d[k] = v (an existing key keeps its position)
    LVal& set(const std::string& key, LVal v);
    LVal* get(const std::string& key);
    const LVal* get(const std::string& key) const;
    // d.setdefault(key, {}) for a plain dict
    LVal& sub(const std::string& key, bool ordered_keys = false);
    LVal& push(LVal v) { items.push_back(std::move(v)); return items.back(); }
    bool empty() const { return kind == List ? items.empty() : fields.empty(); }
};

// Python-layout Lua text of a value
std::string to_lua(const LVal& v, int indent = 0);
std::string lua_key(const std::string& k);
std::string lua_quote(const std::string& s);
// JSON text (indent 1 like json.dumps(indent=1), UTF-8 kept as is, keys in the Dict's order)
std::string to_json(const LVal& v, int indent = 0);

}  // namespace p2m
