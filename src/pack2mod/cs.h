// Lossless reader/writer for compiled_combatspecies.bin (docs/RE.md sections 43 and 45: the engine's eCDocArchive "V001" in the GENOMFLE
// container). Port of tools/g3cs.py; parse -> write reproduces the file byte for byte.
#pragma once

#include <array>
#include <optional>
#include <string>
#include <vector>

#include "pack2mod/util.h"

namespace p2m {

struct CsObj;

struct CsVal {
    enum Kind { Float, Int, Bool, Str, Proxy, Enum, Floats, Arr, Obj } kind = Int;
    double f = 0;                  // Float (stored as the f32 it was read as)
    int64_t i = 0;                 // Int (long / int / short, the property type tells), Bool, Enum value
    std::optional<std::string> s;  // Str, Proxy (GUID hex)
    std::string enum_type;         // Enum
    std::vector<float> floats;     // Floats
    uint8_t arr_ver = 0;           // Arr
    std::vector<CsObj> objs;       // Arr: the elements, Obj: exactly one
};

struct CsProp {
    std::string n, t;
    uint16_t pv = 0;
    CsVal v;
    int enc = 0;  // payload size form of strings / proxies
};

struct CsObj {
    std::optional<std::string> cls;
    uint16_t v0 = 0, v1 = 0;
    std::vector<CsProp> props;
    Bytes extra;
    size_t end = 0;
    std::optional<std::array<uint32_t, 7>> acc;
};

struct CsSpecies {
    std::string name;
    uint32_t ft_lo = 0, ft_hi = 0;
    CsObj obj;
    uint16_t ver = 0;
    std::vector<CsObj> styles, moves, ais;
};

struct CsDoc {
    std::vector<std::string> strs;
    uint8_t flag = 0;
    std::vector<CsSpecies> species;
};

CsDoc parse_combat(const Bytes& b);  // throws Error on a file that is not the expected structure
Bytes write_combat(const CsDoc& d);

// semantic equality (ignores read-time bookkeeping: end offsets, string encodings, accessor headers)
bool cs_equal(const CsObj& a, const CsObj& b);
bool cs_equal(const CsVal& a, const CsVal& b);
bool cs_equal(const std::vector<CsObj>& a, const std::vector<CsObj>& b);

std::string utf8_to_latin1(std::string_view s);

}  // namespace p2m
