// Offline reader for Risen's entity templates and property streams (GENOMFLE container; docs/RE.md section 43). Port of tools/g3tple.py:
// a tolerant scanner that walks the stream, recognises property headers and resynchronises byte by byte.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "pack2mod/util.h"

namespace p2m {

// A decoded property value. Python's dynamic typing is mirrored: ints, floats and bools compare numerically across kinds,
// vectors are tuples of floats, anything undecodable is the hex text of its payload (a Str).
struct Val {
    enum Kind : uint8_t { None, Int, Float, Bool, Str, Vec };
    Kind k = None;
    int64_t i = 0;
    double d = 0;
    std::string s;
    std::vector<double> v;

    static Val of_int(int64_t x) { Val r; r.k = Int; r.i = x; return r; }
    static Val of_float(double x) { Val r; r.k = Float; r.d = x; return r; }
    static Val of_bool(bool x) { Val r; r.k = Bool; r.i = x; return r; }
    static Val of_str(std::string x) { Val r; r.k = Str; r.s = std::move(x); return r; }
    static Val of_vec(std::vector<double> x) { Val r; r.k = Vec; r.v = std::move(x); return r; }

    bool is_num() const { return k == Int || k == Float || k == Bool; }
    double num() const { return k == Float ? d : static_cast<double>(i); }
    bool is_str() const { return k == Str; }
    bool is_int() const { return k == Int; }
    // Python str(value) for the formats the tool needs in messages
    std::string repr() const;
};
bool val_eq(const Val& a, const Val& b);
inline bool operator==(const Val& a, const Val& b) { return val_eq(a, b); }
inline bool operator!=(const Val& a, const Val& b) { return !val_eq(a, b); }

struct Prop {
    std::string pset;  // property set, e.g. gCItem_PS
    std::string name;  // property name; array members are "Array[].Field"
    std::string type;
    Val value;
    size_t off = 0;
    size_t size = 0;
    std::string key() const { return pset + "." + name; }
};

struct Template {
    std::string name;                  // string 0, "It_1H_Sword.tple"
    std::vector<std::string> strings;  // UTF-8
    std::vector<std::string> guids;    // "@" + 32 hex, the records "db 00 <guid>" in the header
    std::string guid;                  // the entity GUID (second record), how other templates refer to this one
    std::vector<Prop> props;
    std::string base_name() const { return name.size() >= 5 ? name.substr(0, name.size() - 5) : std::string(); }
};

// throws Error ("no string table") when the data is no GENOMFLE file
Template parse_template(const Bytes& b);
// the string table only (offset of the table is in the header at 10)
std::vector<std::string> read_string_table(const Bytes& b, size_t* table_offset);

}  // namespace p2m
