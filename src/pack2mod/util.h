// Small helpers shared by the risen-pack2mod readers and converter (no dependency on the game layer).
#pragma once

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace p2m {

// Binary data is carried in std::string (compares and hashes for free).
using Bytes = std::string;

struct Error : std::runtime_error {
    using std::runtime_error::runtime_error;
};

inline uint16_t rd16(const Bytes& b, size_t p) {
    if (p + 2 > b.size()) throw Error("read past the end of the data");
    return static_cast<uint16_t>(static_cast<uint8_t>(b[p]) | (static_cast<uint8_t>(b[p + 1]) << 8));
}
inline uint32_t rd32(const Bytes& b, size_t p) {
    if (p + 4 > b.size()) throw Error("read past the end of the data");
    uint32_t v;
    std::memcpy(&v, b.data() + p, 4);
    return v;
}
inline uint64_t rd64(const Bytes& b, size_t p) {
    if (p + 8 > b.size()) throw Error("read past the end of the data");
    uint64_t v;
    std::memcpy(&v, b.data() + p, 8);
    return v;
}
inline float rdf(const Bytes& b, size_t p) {
    float v;
    uint32_t u = rd32(b, p);
    std::memcpy(&v, &u, 4);
    return v;
}
inline void wr16(Bytes& b, uint16_t v) { b.append(reinterpret_cast<const char*>(&v), 2); }
inline void wr32(Bytes& b, uint32_t v) { b.append(reinterpret_cast<const char*>(&v), 4); }
inline void wr64(Bytes& b, uint64_t v) { b.append(reinterpret_cast<const char*>(&v), 8); }
inline void wrf(Bytes& b, float v) { b.append(reinterpret_cast<const char*>(&v), 4); }

// ASCII lower case (file and template names are ASCII; non-ASCII bytes are left alone).
std::string lower(std::string_view s);
bool starts_with(std::string_view s, std::string_view p);
bool ends_with(std::string_view s, std::string_view p);
bool contains(std::string_view s, std::string_view p);
// last path component, '/' or '\\' separated
std::string bare_name(std::string_view path);

// Text conversions. The archives hold Latin-1 (templates) and UTF-16LE (string tables); everything inside the tool is UTF-8.
std::string latin1_to_utf8(std::string_view s);
std::string utf16le_to_utf8(const char* data, size_t units);
std::string utf8_from_cp(uint32_t cp);
std::vector<uint32_t> utf8_decode(std::string_view s);
std::string utf8_encode(const std::vector<uint32_t>& cps);
// Python's str.strip(): strips Unicode white space
std::string py_strip(std::string_view s);
std::string replace_all(std::string s, std::string_view from, std::string_view to);

// Python-compatible number formatting
double py_round(double x, int digits);  // round(x, digits) of a float
std::string fmt_g(double x);            // "%g"
std::string fmt_g9(double x);           // "%.9g"
std::string hex_of(std::string_view s); // lower case hex

Bytes read_file(const std::string& path);                 // throws Error
void write_file(const std::string& path, const Bytes& d); // creates directories, throws Error

}  // namespace p2m
