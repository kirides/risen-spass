#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

// Pure helpers for runtime-created entity templates (docs/RE.md section 25): the deterministic template ID derived from the
// template name, name validation and the GUID text of an ID. No engine access, so the unit tests cover them.
//
// The engine identifies a template by a bCPropertyID = a 16 byte GUID (+ a 4 byte counter that is always 0); a savegame stores the
// ID of the template of every item stack/entity (eCTemplateEntityProxy::operator>>). The engine's own bCPropertyID::CreateFromString
// is the MD5 digest of the text, we use the same construction on the prefixed name "spass.template:<name>" so a mod that creates
// the template "My_Sword" in any process, on any machine, gets the same ID and a savegame written with it resolves again.
namespace spass::templates {

using TemplateId = std::array<std::uint8_t, 16>;

namespace detail {

inline std::uint32_t rotl(std::uint32_t x, int c) { return (x << c) | (x >> (32 - c)); }

// Plain MD5 (RFC 1321).
inline TemplateId md5(std::string_view data) {
    static constexpr std::uint32_t K[64] = {
        0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501, 0x698098d8, 0x8b44f7af, 0xffff5bb1,
        0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821, 0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453,
        0xd8a1e681, 0xe7d3fbc8, 0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a, 0xfffa3942,
        0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70, 0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05,
        0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665, 0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d,
        0x85845dd1, 0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};
    static constexpr int S[64] = {7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 5, 9,  14, 20, 5, 9,
                                  14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
                                  4, 11, 16, 23, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};
    std::uint32_t a0 = 0x67452301, b0 = 0xefcdab89, c0 = 0x98badcfe, d0 = 0x10325476;

    std::string msg(data);
    const std::uint64_t bits = static_cast<std::uint64_t>(data.size()) * 8u;
    msg.push_back(static_cast<char>(0x80));
    while (msg.size() % 64 != 56) {
        msg.push_back(0);
    }
    for (int i = 0; i < 8; ++i) {
        msg.push_back(static_cast<char>((bits >> (8 * i)) & 0xff));
    }
    for (std::size_t off = 0; off < msg.size(); off += 64) {
        std::uint32_t M[16];
        for (int i = 0; i < 16; ++i) {
            const auto* p = reinterpret_cast<const unsigned char*>(msg.data() + off + 4 * i);
            M[i] = static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) | (static_cast<std::uint32_t>(p[2]) << 16) |
                   (static_cast<std::uint32_t>(p[3]) << 24);
        }
        std::uint32_t A = a0, B = b0, C = c0, D = d0;
        for (int i = 0; i < 64; ++i) {
            std::uint32_t F;
            int g;
            if (i < 16) {
                F = (B & C) | (~B & D);
                g = i;
            } else if (i < 32) {
                F = (D & B) | (~D & C);
                g = (5 * i + 1) % 16;
            } else if (i < 48) {
                F = B ^ C ^ D;
                g = (3 * i + 5) % 16;
            } else {
                F = C ^ (B | ~D);
                g = (7 * i) % 16;
            }
            F = F + A + K[i] + M[g];
            A = D;
            D = C;
            C = B;
            B = B + rotl(F, S[i]);
        }
        a0 += A;
        b0 += B;
        c0 += C;
        d0 += D;
    }
    TemplateId out{};
    const std::uint32_t words[4] = {a0, b0, c0, d0};
    for (int w = 0; w < 4; ++w) {
        for (int b = 0; b < 4; ++b) {
            out[static_cast<std::size_t>(w * 4 + b)] = static_cast<std::uint8_t>((words[w] >> (8 * b)) & 0xff);
        }
    }
    return out;
}

}  // namespace detail

inline constexpr const char* kIdPrefix = "spass.template:";

// The deterministic ID of the template called `name` (MD5 of "spass.template:<name>").
inline TemplateId idForName(std::string_view name) { return detail::md5(std::string(kIdPrefix) + std::string(name)); }

// The ID of the hidden root entity ("<name>.tple") every template hangs under.
inline TemplateId rootIdForName(std::string_view name) { return detail::md5(std::string(kIdPrefix) + std::string(name) + ".tple"); }

// GUID text "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx" of an ID's bytes, read the way Windows lays out a GUID in memory
// (Data1/2/3 little endian), which is also how the engine prints a bCPropertyID.
inline std::string guidText(const TemplateId& id) {
    static constexpr char hex[] = "0123456789abcdef";
    std::string out;
    auto byte = [&](int i) {
        out.push_back(hex[id[static_cast<std::size_t>(i)] >> 4]);
        out.push_back(hex[id[static_cast<std::size_t>(i)] & 15]);
    };
    for (int i : {3, 2, 1, 0}) byte(i);
    out.push_back('-');
    for (int i : {5, 4}) byte(i);
    out.push_back('-');
    for (int i : {7, 6}) byte(i);
    out.push_back('-');
    for (int i : {8, 9}) byte(i);
    out.push_back('-');
    for (int i = 10; i < 16; ++i) byte(i);
    return out;
}

constexpr std::size_t kMaxNameLength = 63;

// A template name must be usable as a file-like identifier, a script string and the tail of a localization key (FO_<name>):
// letters, digits and underscores, not starting with a digit, at most 63 characters. Returns "" when valid, else the reason.
inline std::string nameProblem(std::string_view name) {
    if (name.empty()) {
        return "the template name is empty";
    }
    if (name.size() > kMaxNameLength) {
        return "the template name is longer than " + std::to_string(kMaxNameLength) + " characters";
    }
    for (std::size_t i = 0; i < name.size(); ++i) {
        const char c = name[i];
        const bool alpha = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
        const bool digit = c >= '0' && c <= '9';
        if (!(alpha || (digit && i > 0))) {
            return std::string("the template name may only contain letters, digits and '_' and must not start with a digit (bad character '") +
                   c + "')";
        }
    }
    return "";
}

}  // namespace spass::templates
