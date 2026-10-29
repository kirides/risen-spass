#include "core/vegetation.h"

#include <algorithm>
#include <cstring>

namespace mk {

namespace {

struct Cur {
    const p2m::Bytes& b;
    const std::vector<std::string>& S;
    size_t p;
    void need(size_t n) const {
        if (p + n > b.size()) throw p2m::Error("vegetation: truncated");
    }
    uint8_t u8() {
        need(1);
        return static_cast<uint8_t>(b[p++]);
    }
    uint16_t u16() {
        need(2);
        const uint16_t v = p2m::rd16(b, p);
        p += 2;
        return v;
    }
    uint32_t u32() {
        need(4);
        const uint32_t v = p2m::rd32(b, p);
        p += 4;
        return v;
    }
    float f32() {
        need(4);
        float v;
        std::memcpy(&v, b.data() + p, 4);
        p += 4;
        return v;
    }
    std::string str() {
        const uint16_t i = u16();
        return i < S.size() ? S[i] : std::string();
    }
};

std::vector<std::string> string_table(const p2m::Bytes& b) {
    if (b.size() < 22 || b.compare(0, 8, "GENOMFLE") != 0) throw p2m::Error("not a layer file");
    const size_t t = p2m::rd32(b, 10);
    if (t + 9 > b.size()) throw p2m::Error("string table outside the file");
    const uint32_t n = p2m::rd32(b, t + 5);
    std::vector<std::string> v;
    size_t q = t + 9;
    for (uint32_t i = 0; i < n; ++i) {
        if (q + 2 > b.size()) throw p2m::Error("string table truncated");
        const size_t len = p2m::rd16(b, q);
        q += 2;
        if (q + len > b.size()) throw p2m::Error("string table truncated");
        v.push_back(b.substr(q, len));
        q += len;
    }
    return v;
}

std::string base_name(const std::string& path) {
    const auto s = path.find_last_of("\\/");
    return s == std::string::npos ? path : path.substr(s + 1);
}

// the accessor object's header: returns the class name
std::string object_header(Cur& c) {
    if (c.u16() != 1 || c.u8() != 1 || c.u16() != 1 || c.u8() != 1) throw p2m::Error("vegetation: no object header");
    const std::string cls = c.str();
    if (c.u16() != 1 || c.u8() != 0) throw p2m::Error("vegetation: no class record");
    c.u16();  // class version
    return cls;
}

// the property body of an accessor object: returns the string values by property name, leaves `c` behind the properties (the class tail is next)
std::map<std::string, std::string> read_body(Cur& c, size_t& body_end) {
    std::map<std::string, std::string> out;
    c.u16();  // 0xc9
    const uint32_t size = c.u32();
    body_end = c.p + size;
    c.u16();  // 0x1e
    const uint32_t n = c.u32();
    for (uint32_t i = 0; i < n; ++i) {
        const std::string name = c.str(), type = c.str();
        if (c.u16() != 0x1e) throw p2m::Error("vegetation: bad property");
        const uint32_t sz = c.u32();
        c.need(sz);
        if (sz == 2 && (type.find("String") != std::string::npos || type == "bCString")) {
            const uint16_t idx = p2m::rd16(c.b, c.p);
            out[name] = idx < c.S.size() ? c.S[idx] : std::string();
        }
        c.p += sz;
    }
    if (c.p > body_end) throw p2m::Error("vegetation: properties exceed the body");
    return out;  // the class's own tail follows inside the declared body
}

}  // namespace

std::optional<VegetationInfo> read_vegetation(const p2m::Bytes& b, size_t max_entries) {
    if (b.find("eCVegetation_PS") == std::string::npos) return std::nullopt;
    const std::vector<std::string> S = string_table(b);
    const auto it = std::find(S.begin(), S.end(), "eCVegetation_PS");
    if (it == S.end()) return std::nullopt;
    const uint16_t idx = static_cast<uint16_t>(it - S.begin());
    std::string pat;
    for (int i = 0; i < 2; ++i) pat.append("\x01\x00\x01", 3);  // u16 1, u8 1, twice: the accessor header and the class record
    pat.push_back(static_cast<char>(idx & 0xff));
    pat.push_back(static_cast<char>(idx >> 8));
    const size_t at = b.find(pat);
    if (at == std::string::npos) return std::nullopt;
    Cur c{b, S, at};
    if (object_header(c) != "eCVegetation_PS") return std::nullopt;
    size_t body_end = 0;
    read_body(c, body_end);
    VegetationInfo v;
    uint16_t ver = c.u16();
    if (ver < 2) c.u16();
    c.u8();
    const uint32_t ntypes = c.u32();
    for (uint32_t i = 0; i < ntypes; ++i) {
        c.u16();
        c.str();
    }
    c.u32();  // slots
    const uint32_t nmesh = c.u32();
    for (uint32_t i = 0; i < nmesh; ++i) {
        object_header(c);
        size_t mend = 0;
        const auto props = read_body(c, mend);
        VegMesh m;
        const auto p = props.find("MeshFilePath");
        if (p != props.end()) m.path = base_name(p->second);
        const uint16_t mver = c.u16();
        m.id = c.u32();
        c.p += 8;  // bCDateTime
        if (mver >= 2) m.material = c.str();
        if (mver == 2) {
            c.p += 24;
            for (size_t esz : {12, 12, 8, 4}) {
                c.u8();
                const uint32_t n = c.u32();
                c.need(static_cast<size_t>(n) * esz);
                c.p += static_cast<size_t>(n) * esz;
            }
        }
        if (c.p != mend) throw p2m::Error("vegetation: a mesh object does not end where it says");
        v.meshes.push_back(std::move(m));
    }
    c.u16();  // grid version
    v.node_dimension = c.f32();
    for (int k = 0; k < 4; ++k) v.rect[static_cast<size_t>(k)] = static_cast<int>(c.u32());
    v.nodes = c.u32();
    size_t seen = 0;
    // first pass counts, the sample takes every n-th entry when a limit is given
    const size_t grid_at = c.p;
    uint64_t total = 0;
    for (uint32_t i = 0; i < v.nodes; ++i) {
        c.u32();
        c.u16();
        c.need(24);
        c.p += 24;
        const uint32_t cnt = c.u32();
        c.need(static_cast<size_t>(cnt) * 44);
        total += cnt;
        c.p += static_cast<size_t>(cnt) * 44;
    }
    v.entries = static_cast<uint32_t>(total);
    const size_t every = max_entries && total > max_entries ? static_cast<size_t>((total + max_entries - 1) / max_entries) : 1;
    c.p = grid_at;
    for (uint32_t i = 0; i < v.nodes; ++i) {
        c.u32();
        c.u16();
        c.p += 24;
        const uint32_t cnt = c.u32();
        for (uint32_t k = 0; k < cnt; ++k, c.p += 44) {
            const uint32_t id = p2m::rd32(b, c.p);
            ++v.per_mesh[id];
            if (max_entries && seen++ % every == 0) {
                VegEntry e;
                e.mesh = id;
                for (int a = 0; a < 3; ++a) std::memcpy(&e.pos[static_cast<size_t>(a)], b.data() + c.p + 4 + 4 * a, 4);
                std::memcpy(&e.scale_h, b.data() + c.p + 0x20, 4);
                std::memcpy(&e.scale_v, b.data() + c.p + 0x24, 4);
                e.color = p2m::rd32(b, c.p + 0x28);
                v.sample.push_back(e);
            }
        }
    }
    return v;
}

}  // namespace mk
