#include "core/material.h"

#include <algorithm>
#include <cstring>

namespace mk {
namespace {

const char kHdr[6] = {1, 0, 1, 1, 0, 1};

uint16_t r16(const p2m::Bytes& b, size_t p) { return p2m::rd16(b, p); }
uint32_t r32(const p2m::Bytes& b, size_t p) { return p2m::rd32(b, p); }
void w16(std::string& s, uint16_t v) { s.append(reinterpret_cast<const char*>(&v), 2); }
void w32(std::string& s, uint32_t v) { s.append(reinterpret_cast<const char*>(&v), 4); }
void put32(std::string& s, size_t at, uint32_t v) { std::memcpy(&s[at], &v, 4); }

void need(const p2m::Bytes& b, size_t p, size_t n, const char* what) {
    if (p > b.size() || n > b.size() - p) throw p2m::Error(std::string("material: truncated at ") + what);
}

std::string read_str(const p2m::Bytes& b, size_t& p) {
    need(b, p, 2, "string length");
    const size_t n = r16(b, p);
    need(b, p + 2, n, "string");
    std::string s = b.substr(p + 2, n);
    p += 2 + n;
    return s;
}

// an object header with a plausible class name: tail data can contain the six header bytes by chance
bool object_at(const p2m::Bytes& b, size_t p) {
    if (p + 8 > b.size() || std::memcmp(b.data() + p, kHdr, 6) != 0) return false;
    const size_t n = static_cast<uint8_t>(b[p + 6]);
    if (n < 8 || n > 0x40 || b[p + 7] != 0 || p + 8 + n + 3 > b.size()) return false;
    const char* name = b.data() + p + 8;
    if (!((name[0] == 'e' || name[0] == 'g') && name[1] == 'C')) return false;
    for (size_t i = 2; i < n; ++i) {
        const char c = name[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_')) return false;
    }
    return std::memcmp(name + n, "\x01\x00\x00", 3) == 0;
}

std::string str_bytes(const std::string& s) {
    std::string o;
    w16(o, static_cast<uint16_t>(s.size()));
    return o + s;
}

}  // namespace

MatProp* MatObject::find(const std::string& name) {
    for (auto& p : props)
        if (p.name == name) return &p;
    return nullptr;
}
const MatProp* MatObject::find(const std::string& name) const { return const_cast<MatObject*>(this)->find(name); }

bool MatObject::get_string(const std::string& name, std::string* out) const {
    const MatProp* p = find(name);
    if (!p || p->payload.size() < 2 || p->payload.size() != 2u + r16(p->payload, 0)) return false;
    if (out) *out = p->payload.substr(2);
    return true;
}
bool MatObject::set_string(const std::string& name, const std::string& value) {
    MatProp* p = find(name);
    if (!p || p->payload.size() < 2 || p->payload.size() != 2u + r16(p->payload, 0) || value.size() > 0xffff) return false;
    p->payload = str_bytes(value);
    return true;
}
bool MatObject::get_bool(const std::string& name, bool fallback) const {
    const MatProp* p = find(name);
    return p && p->payload.size() == 1 ? p->payload[0] != 0 : fallback;
}
bool MatObject::set_bool(const std::string& name, bool v) {
    MatProp* p = find(name);
    if (!p || p->payload.size() != 1) return false;
    p->payload[0] = v ? 1 : 0;
    return true;
}
bool MatObject::get_int(const std::string& name, int32_t* out) const {
    const MatProp* p = find(name);
    if (!p) return false;
    if (p->payload.size() == 4) {
        if (out) *out = static_cast<int32_t>(r32(p->payload, 0));
        return true;
    }
    if (p->payload.size() == 6 && r16(p->payload, 0) == 0xc9) {  // an enum: u16 0xc9 + u32
        if (out) *out = static_cast<int32_t>(r32(p->payload, 2));
        return true;
    }
    return false;
}
bool MatObject::set_int(const std::string& name, int32_t v) {
    MatProp* p = find(name);
    if (!p) return false;
    if (p->payload.size() == 4) {
        put32(p->payload, 0, static_cast<uint32_t>(v));
        return true;
    }
    if (p->payload.size() == 6 && r16(p->payload, 0) == 0xc9) {
        put32(p->payload, 2, static_cast<uint32_t>(v));
        return true;
    }
    return false;
}
bool MatObject::get_float(const std::string& name, float* out) const {
    const MatProp* p = find(name);
    if (!p || p->payload.size() != 4) return false;
    if (out) std::memcpy(out, p->payload.data(), 4);
    return true;
}
bool MatObject::set_float(const std::string& name, float v) {
    MatProp* p = find(name);
    if (!p || p->payload.size() != 4) return false;
    std::memcpy(&p->payload[0], &v, 4);
    return true;
}

Material Material::parse(const p2m::Bytes& b) {
    if (b.size() < 0x28 || b.compare(0, 8, "GR01SM01") != 0) throw p2m::Error("not a GR01/SM01 material");
    Material m;
    m.header = b.substr(0, 0x28);
    std::vector<size_t> begins, body_ends;
    size_t p = 0x28;
    while (p < b.size()) {
        if (!object_at(b, p)) throw p2m::Error("material: no object at " + std::to_string(p));
        MatObject o;
        size_t q = p + 6;
        o.cls = read_str(b, q);
        need(b, q, 7, "class record");
        o.version = r16(b, q + 3);
        q += 5;
        if (r16(b, q) != 0xc9) throw p2m::Error("material: " + o.cls + ": no body marker");
        const size_t body_begin = q + 6, size = r32(b, q + 2);
        need(b, body_begin, size, "object body");
        q = body_begin;
        need(b, q, 6, "property count");
        if (r16(b, q) != 0xc9) throw p2m::Error("material: " + o.cls + ": no property marker");
        const size_t n = r32(b, q + 2);
        q += 6;
        for (size_t i = 0; i < n; ++i) {
            MatProp pr;
            pr.name = read_str(b, q);
            pr.type = read_str(b, q);
            need(b, q, 6, "property header");
            if (r16(b, q) != 0x1e) throw p2m::Error("material: " + o.cls + "." + pr.name + ": no 0x1e");
            const size_t psize = r32(b, q + 2);
            need(b, q + 6, psize, "property value");
            pr.payload = b.substr(q + 6, psize);
            q += 6 + psize;
            o.props.push_back(std::move(pr));
        }
        if (q > body_begin + size) throw p2m::Error("material: " + o.cls + ": properties exceed the body");
        size_t next = p + 1;
        while (next < b.size() && !object_at(b, next)) ++next;
        o.tail = b.substr(q, next - q);
        begins.push_back(p);
        body_ends.push_back(body_begin + size);
        m.objects.push_back(std::move(o));
        p = next;
    }
    begins.push_back(b.size());
    // which objects does each body cover: its end must be an object boundary
    for (size_t i = 0; i < m.objects.size(); ++i) {
        size_t e = i + 1;
        while (e < begins.size() && begins[e] < body_ends[i]) ++e;
        if (e >= begins.size() || begins[e] != body_ends[i]) throw p2m::Error("material: " + m.objects[i].cls + ": body ends inside an object");
        m.objects[i].covers = e - i - 1;
    }
    return m;
}

p2m::Bytes Material::write() const {
    std::string out = header;
    std::vector<size_t> begins, size_at, body_begin;
    for (const MatObject& o : objects) {
        begins.push_back(out.size());
        out.append(kHdr, 6);
        out += str_bytes(o.cls);
        out.append("\x01\x00\x00", 3);
        w16(out, o.version);
        w16(out, 0xc9);
        size_at.push_back(out.size());
        w32(out, 0);
        body_begin.push_back(out.size());
        w16(out, 0xc9);
        w32(out, static_cast<uint32_t>(o.props.size()));
        for (const MatProp& p : o.props) {
            out += str_bytes(p.name);
            out += str_bytes(p.type);
            w16(out, 0x1e);
            w32(out, static_cast<uint32_t>(p.payload.size()));
            out += p.payload;
        }
        out += o.tail;
    }
    begins.push_back(out.size());
    for (size_t i = 0; i < objects.size(); ++i) {
        const size_t e = std::min(i + 1 + objects[i].covers, begins.size() - 1);
        put32(out, size_at[i], static_cast<uint32_t>(begins[e] - body_begin[i]));
    }
    if (!begins.empty() && begins.size() > 1) {
        const size_t first = begins[1] - 0x28;
        put32(out, 12, static_cast<uint32_t>(first));
        put32(out, 16, static_cast<uint32_t>(0x28 + first));
        put32(out, 20, static_cast<uint32_t>(out.size() - (0x28 + first)));
    }
    return out;
}

std::vector<MatObject*> Material::of_class(const std::string& cls) {
    std::vector<MatObject*> r;
    for (auto& o : objects)
        if (o.cls == cls) r.push_back(&o);
    return r;
}

std::vector<std::string> Material::sampler_images() const {
    std::vector<std::string> r;
    for (const auto& o : objects) {
        std::string s;
        if (o.cls == "eCColorSrcSampler" && o.get_string("ImageFilePath", &s)) r.push_back(s);
    }
    return r;
}

namespace {
std::string hex16(const std::string& b, size_t p) {
    static const char* d = "0123456789abcdef";
    std::string o;
    for (size_t i = 0; i < 16; ++i) {
        const auto c = static_cast<uint8_t>(b[p + i]);
        o += d[c >> 4];
        o += d[c & 15];
    }
    return o;
}
const char* kSlotNames[8] = {"Diffuse", "Opacity", "SelfIllumination", "Specular", "SpecularPower", "Normal", "Distortion", "LightEmission"};
}  // namespace

std::string Material::guid_of(size_t i) const {
    if (i >= objects.size()) return {};
    const MatObject& o = objects[i];
    if (o.cls.rfind("eCShader", 0) == 0) {
        if (o.tail.size() < 2 + 256 + 4 + 20) return {};
        return hex16(o.tail, 2 + 256 + 4);
    }
    if (o.tail.size() < 41) return {};
    return hex16(o.tail, o.tail.size() - 37);
}

std::vector<MatSlot> Material::slots() const {
    std::vector<MatSlot> r;
    size_t sh = objects.size();
    for (size_t i = 0; i < objects.size(); ++i)
        if (objects[i].cls == "eCShaderDefault") sh = i;
    if (sh == objects.size() || objects[sh].tail.size() < 2 + 256) return r;
    const std::string& t = objects[sh].tail;
    for (int k = 0; k < 8; ++k) {
        const size_t p = 2 + 32 * k;
        MatSlot s;
        s.name = kSlotNames[k];
        s.component = r32(t, p + 4);
        s.modifier = r32(t, p + 8);
        s.linked = t[p + 12 + 16] != 0;
        if (s.linked) {
            s.guid = hex16(t, p + 12);
            for (size_t i = 0; i < objects.size(); ++i)
                if (i != sh && guid_of(i) == s.guid) s.node = static_cast<int>(i);
        }
        r.push_back(std::move(s));
    }
    return r;
}

int Material::slot_sampler(const MatSlot& slot, int depth) const {
    if (slot.node < 0 || depth > 8) return -1;
    const MatObject& o = objects[static_cast<size_t>(slot.node)];
    if (o.cls == "eCColorSrcSampler") return slot.node;
    if (o.cls == "eCColorSrcCombiner" && o.tail.size() >= 2 + 64) {
        for (int k = 0; k < 2; ++k) {
            const size_t p = 2 + 32 * k;
            if (!o.tail[p + 28]) continue;
            const std::string g = hex16(o.tail, p + 12);
            for (size_t i = 0; i < objects.size(); ++i)
                if (guid_of(i) == g) {
                    MatSlot sub;
                    sub.linked = true;
                    sub.node = static_cast<int>(i);
                    const int r = slot_sampler(sub, depth + 1);
                    if (r >= 0) return r;
                }
        }
    }
    return -1;
}

int Material::add_sampler(const std::string& image) {
    size_t sh = objects.size();
    for (size_t i = 0; i < objects.size(); ++i)
        if (objects[i].cls == "eCShaderDefault") sh = i;
    if (sh == objects.size() || objects[sh].tail.size() != 2 + 256 + 2 + 2 + 20 + 16 + 1 + 4) return -1;
    MatObject o;
    o.cls = "eCColorSrcSampler";
    o.version = 1;
    auto str = [](const std::string& v) {
        std::string r;
        w16(r, static_cast<uint16_t>(v.size()));
        return r + v;
    };
    auto enum0 = [] {
        std::string r;
        w16(r, 0xc9);
        w32(r, 0);
        return r;
    };
    o.props.push_back({"ImageFilePath", "bCImageResourceString", str(image)});
    o.props.push_back({"TexRepeatU", "bTPropertyContainer<enum eEColorSrcSampleTexRepeat>", enum0()});
    o.props.push_back({"TexRepeatV", "bTPropertyContainer<enum eEColorSrcSampleTexRepeat>", enum0()});
    o.props.push_back({"AnimationSpeed", "float", std::string(4, '\0')});
    o.props.push_back({"SwitchRepeat", "bTPropertyContainer<enum eEColorSrcSwitchRepeat>", enum0()});
    o.props.push_back({"SwitchBegin", "int", std::string(4, '\0')});
    // tail: version 1, an empty TexCoord proxy (index 0, invalid guid), -1, the element suffix (versions 1 and 2, own guid, editor rect, bool)
    std::string t;
    w16(t, 1);
    w32(t, 0);
    t.append(20, '\0');
    w32(t, 0xffffffffu);
    w16(t, 1);
    w16(t, 2);
    // a deterministic guid from the image name and the node number (FNV-1a, twice): the same edit exports the same bytes
    uint64_t a = 1469598103934665603ull, b = 0x9e3779b97f4a7c15ull;
    for (char c : image + "#" + std::to_string(objects.size())) {
        a = (a ^ static_cast<uint8_t>(c)) * 1099511628211ull;
        b = (b + static_cast<uint8_t>(c)) * 0xff51afd7ed558ccdull;
    }
    t.append(reinterpret_cast<const char*>(&a), 8);
    t.append(reinterpret_cast<const char*>(&b), 8);
    t.push_back(1);
    t.append(3, '\0');
    w32(t, 0x1f0);
    w32(t, static_cast<uint32_t>(0x20 + 0x60 * objects.size()));
    w32(t, 0x2e0);
    w32(t, static_cast<uint32_t>(0xc8 + 0x60 * objects.size()));
    t.push_back(0);
    o.tail = t;
    objects.push_back(std::move(o));
    // the shader's node count and the nodes its body covers
    std::string& st = objects[sh].tail;
    const uint32_t n = r32(st, st.size() - 4) + 1;
    std::memcpy(&st[st.size() - 4], &n, 4);
    objects[sh].covers += 1;
    return static_cast<int>(objects.size() - 1);
}

bool Material::link_slot(size_t slot, size_t node, uint32_t component, uint32_t modifier) {
    size_t sh = objects.size();
    for (size_t i = 0; i < objects.size(); ++i)
        if (objects[i].cls == "eCShaderDefault") sh = i;
    if (sh == objects.size() || slot >= 8 || node >= objects.size() || objects[sh].tail.size() < 2 + 256) return false;
    const std::string g = guid_of(node);
    if (g.size() != 32) return false;
    std::string& t = objects[sh].tail;
    const size_t p = 2 + 32 * slot;
    const uint32_t ver = 0x29a;
    std::memcpy(&t[p], &ver, 4);
    std::memcpy(&t[p + 4], &component, 4);
    std::memcpy(&t[p + 8], &modifier, 4);
    for (size_t i = 0; i < 16; ++i) t[p + 12 + i] = static_cast<char>(std::stoi(g.substr(i * 2, 2), nullptr, 16));
    t[p + 28] = 1;
    t[p + 29] = t[p + 30] = t[p + 31] = 0;
    return true;
}

bool Material::set_slot_component(size_t slot, uint32_t component) {
    size_t sh = objects.size();
    for (size_t i = 0; i < objects.size(); ++i)
        if (objects[i].cls == "eCShaderDefault") sh = i;
    if (sh == objects.size() || slot >= 8 || objects[sh].tail.size() < 2 + 256) return false;
    std::memcpy(&objects[sh].tail[2 + 32 * slot + 4], &component, 4);
    return true;
}

std::string Material::slot_image(const MatSlot& slot, int depth) const {
    const int i = slot_sampler(slot, depth);
    std::string img;
    if (i >= 0) objects[static_cast<size_t>(i)].get_string("ImageFilePath", &img);
    return img;
}

bool apply_material_edit(Material& m, const MaterialEdit& e, std::string& error) {
    static const char* const kNames[8] = {"Diffuse", "Opacity", "SelfIllumination", "Specular", "SpecularPower", "Normal", "Distortion", "LightEmission"};
    auto slot_index = [&](const std::string& name) {
        for (size_t k = 0; k < 8; ++k)
            if (name == kNames[k]) return static_cast<int>(k);
        return -1;
    };
    for (const auto& [name, image] : e.slots) {
        const int k = slot_index(name);
        if (k < 0) {
            error = "unknown slot " + name + " in " + e.material;
            return false;
        }
        const auto slots = m.slots();
        if (slots.empty()) {
            error = e.material + " has no shader the editor can change";
            return false;
        }
        const MatSlot& s = slots[static_cast<size_t>(k)];
        if (!s.linked) {
            const int node = m.add_sampler(image);
            const auto ch = e.channels.find(name);
            if (node < 0 || !m.link_slot(static_cast<size_t>(k), static_cast<size_t>(node), ch != e.channels.end() ? static_cast<uint32_t>(ch->second) : 0u)) {
                error = "slot " + name + " cannot get an image in " + e.material;
                return false;
            }
            continue;
        }
        const int sampler = m.slot_sampler(s);
        if (sampler < 0) {
            error = "slot " + name + " has no texture sampler in " + e.material;
            return false;
        }
        if (!m.objects[static_cast<size_t>(sampler)].set_string("ImageFilePath", image)) {
            error = "the sampler behind " + name + " has no image path";
            return false;
        }
    }
    for (const auto& [name, comp] : e.channels) {
        const int k = slot_index(name);
        if (k < 0 || comp < 0 || comp > 5) {
            error = "bad channel setting for " + name + " in " + e.material;
            return false;
        }
        if (e.slots.count(name) && !m.slots()[static_cast<size_t>(k)].linked) continue;
        if (!m.slots()[static_cast<size_t>(k)].linked || !m.set_slot_component(static_cast<size_t>(k), static_cast<uint32_t>(comp))) {
            error = "slot " + name + " has no image to take a channel of in " + e.material;
            return false;
        }
    }
    for (const auto& [key, value] : e.props) {
        const auto dot = key.find('.');
        bool done = false;
        for (auto& o : m.objects) {
            if (dot == std::string::npos || o.cls != key.substr(0, dot)) continue;
            const std::string pn = key.substr(dot + 1);
            const MatProp* p = o.find(pn);
            if (!p) continue;
            if (p->payload.size() == 1) done = o.set_bool(pn, value != 0) || done;
            else done = o.set_int(pn, value) || done;
        }
        if (!done) {
            error = "no property " + key + " (or not a number) in " + e.material;
            return false;
        }
    }
    return true;
}

MaterialShading material_shading(const p2m::Bytes& file) {
    MaterialShading r;
    try {
        Material m = Material::parse(file);
        for (MatObject& o : m.objects) {  // the shader object: eCShaderDefault, or another shader class (the tree materials have their own)
            int32_t v = 0;
            if (!o.get_int("BlendMode", &v)) continue;
            r.blend = v;
            if (const MatProp* p = o.find("MaskReference"); p && !p->payload.empty()) r.mask = static_cast<uint8_t>(p->payload[0]);
            if (r.blend >= 2 && o.get_bool("TradeAlphaAsSolid", false)) r.blend = 0;  // drawn as a solid whatever the mode says (ground, rocks)
            return r;
        }
    } catch (const std::exception&) {
    }
    // a material the strict grammar does not cover (some tree materials): the two values are found by their names, `BlendMode` + type + 0x1e, size, enum payload
    // (u16 0xc9, u32 value) and `MaskReference` + type + 0x1e, size 1, the char
    try {
        if (const size_t i = file.find("BlendMode"); i != std::string::npos)
            if (const size_t j = file.find(std::string("\xc9\x00", 2), i + 10); j != std::string::npos && j + 6 <= file.size()) r.blend = static_cast<int>(p2m::rd32(file, j + 2));
        if (const size_t k = file.find("MaskReference"); k != std::string::npos)
            if (const size_t j = file.find(std::string("\x1e\x00", 2), k + 14); j != std::string::npos && j + 7 <= file.size()) r.mask = static_cast<uint8_t>(file[j + 6]);
        if (const size_t t = file.find("TradeAlphaAsSolid"); t != std::string::npos && r.blend >= 2)
            if (const size_t j = file.find(std::string("\x1e\x00", 2), t + 18); j != std::string::npos && j + 7 <= file.size() && file[j + 6] != 0) r.blend = 0;
    } catch (const std::exception&) {
    }
    return r;
}

}  // namespace mk
