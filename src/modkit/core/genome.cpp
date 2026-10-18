#include "core/genome.h"

#include <cstring>

namespace mk {

using p2m::Bytes;
using p2m::Error;

namespace {

constexpr uint32_t kDeadC0de = 0xDEADC0DEu;
constexpr uint32_t kDeadBeef = 0xDEADBEEFu;
constexpr uint32_t kLayerMagic = 0xD0DEFADEu;
constexpr uint16_t kPropertyRecord = 0x1e;  // the u16 in front of every property value (bCPropertyTypeBase::Read)

struct Cur {
    const Bytes& b;
    const std::vector<std::string>& S;
    size_t p = 0;

    void need(size_t n) const {
        if (p + n > b.size()) throw Error("read past the end of the data at " + std::to_string(p));
    }
    uint8_t u8() {
        need(1);
        return static_cast<uint8_t>(b[p++]);
    }
    uint16_t u16() {
        const uint16_t v = p2m::rd16(b, p);
        p += 2;
        return v;
    }
    uint32_t u32() {
        const uint32_t v = p2m::rd32(b, p);
        p += 4;
        return v;
    }
    float f32() {
        const float v = p2m::rdf(b, p);
        p += 4;
        return v;
    }
    void skip(size_t n) {
        need(n);
        p += n;
    }
    // a bCString inside a file with a string table is the u16 index (0xffff and out of range = empty)
    std::string str() {
        const uint16_t i = u16();
        return i < S.size() ? S[i] : std::string();
    }
    // bCPropertyID: 16 bytes GUID and a u32 that is read and dropped
    std::string guid() {
        need(20);
        std::string g = "@" + p2m::hex_of(std::string_view(b.data() + p, 16));
        p += 20;
        return g;
    }
    // eCEntityProxy / eCTemplateEntityProxy: u16 version, u8 flag, and the GUID when the flag is set
    std::string proxy() {
        u16();
        return u8() ? guid() : std::string();
    }
    void expect(uint32_t v, const char* what) {
        const size_t at = p;
        if (u32() != v) throw Error(std::string(what) + " is missing at " + std::to_string(at));
    }
};

// ---- property objects ------------------------------------------------------------------------------------------------------------------------------

bool is_object_array_element(const std::string& cls) {
    // arrays of these are not arrays of property objects (strings, proxies); everything else named "class X" is
    return cls != "bCString" && cls != "eCEntityProxy" && cls != "eCTemplateEntityProxy" && cls != "eCScriptProxyScript" && cls != "bCGuid" && cls != "float" &&
           cls != "int" && cls != "long" && cls != "bool" && cls != "bCVector";
}

std::string array_element_class(const std::string& type) {
    const size_t lt = type.find('<');
    if (lt == std::string::npos || type.back() != '>') return {};
    std::string e = type.substr(lt + 1, type.size() - lt - 2);
    if (p2m::starts_with(e, "class ")) e = e.substr(6);
    return e;
}

void read_body(Cur& c, const std::string& pset, const std::string& prefix, std::vector<p2m::Prop>& out, size_t* tail_begin, size_t* tail_end);
void find_objects(const Bytes& b, const std::vector<std::string>& S, size_t begin, size_t end, std::vector<GSet>& found);
void read_nested(const Bytes& b, const std::vector<std::string>& S, GSet& s);
void append_set(std::vector<p2m::Prop>& out, const GSet& s);

p2m::Prop make_prop(const std::string& pset, const std::string& name, const std::string& type, p2m::Val v, size_t off, size_t size) {
    p2m::Prop p;
    p.pset = pset;
    p.name = name;
    p.type = type;
    p.value = std::move(v);
    p.off = off;
    p.size = size;
    return p;
}

// the value types the shared decoder does not know (checked against every template: the sizes below are the ones in the data; docs/RE.md section 56).
// Arrays are `u8 1, u32 count` and the elements. Returns false when `type` is not one of these or the payload does not have the size its count says.
bool decode_more(const Cur& c, const std::string& pset, const std::string& name, const std::string& type, size_t pay, size_t sz, size_t rec,
                 std::vector<p2m::Prop>& out) {
    const Bytes& b = c.b;
    auto one = [&](p2m::Val v) {
        out.push_back(make_prop(pset, name, type, std::move(v), rec, sz));
        return true;
    };
    auto sidx = [&](size_t at) {
        const uint16_t i = p2m::rd16(b, at);
        return i < c.S.size() ? c.S[i] : std::string();
    };
    if (type == "char" && sz == 1) return one(p2m::Val::of_int(static_cast<int8_t>(b[pay])));
    if (type == "bTPropertyContainer<bool>" && sz == 3) return one(p2m::Val::of_bool(b[pay + 2] != 0));
    if ((type == "eCLocString" || type == "gCBookLocString" || type == "gCLetterLocString") && sz == 2) return one(p2m::Val::of_str(sidx(pay)));
    if (type == "eCEntityStringProxy" && (sz == 3 || sz == 5)) return one(p2m::Val::of_str(sz == 5 && b[pay + 2] ? sidx(pay + 3) : std::string()));
    if (!p2m::starts_with(type, "bTValArray<") && !p2m::starts_with(type, "bTObjArray<")) return false;
    if (sz < 5) return false;
    const uint32_t count = p2m::rd32(b, pay + 1);
    const std::string elem = array_element_class(type);
    size_t esize = 0;
    if (p2m::starts_with(type, "bTValArray<")) {
        if (elem == "float" || elem == "enum gEDirection") esize = 4;
        else if (elem == "bCVector") esize = 12;
        else return false;
        if (sz != 5 + static_cast<size_t>(count) * esize) return false;
        std::vector<double> v;
        for (size_t o = pay + 5; o < pay + sz; o += 4) {
            if (elem == "enum gEDirection") v.push_back(static_cast<double>(p2m::rd32(b, o)));
            else v.push_back(p2m::py_round(p2m::rdf(b, o), 4));
        }
        return one(p2m::Val::of_vec(std::move(v)));
    }
    if (elem == "bCString") {
        if (sz != 5 + static_cast<size_t>(count) * 2) return false;
        for (uint32_t i = 0; i < count; ++i) out.push_back(make_prop(pset, name + "[]", type, p2m::Val::of_str(sidx(pay + 5 + i * 2)), rec, sz));
        return true;
    }
    if (elem == "eCEntityProxy") {
        size_t o = pay + 5;
        std::vector<p2m::Prop> got;
        for (uint32_t i = 0; i < count; ++i) {
            if (o + 3 > pay + sz) return false;
            const bool has = b[o + 2] != 0;
            if (o + 3 + (has ? 20 : 0) > pay + sz) return false;
            got.push_back(make_prop(pset, name + "[]", type, p2m::Val::of_str(has ? "@" + p2m::hex_of(std::string_view(b.data() + o + 3, 16)) : std::string()), rec, sz));
            o += 3 + (has ? 20 : 0);
        }
        if (o != pay + sz) return false;
        out.insert(out.end(), got.begin(), got.end());
        return true;
    }
    return false;
}

// the payload [begin, begin + size) of a bTObjArray<class X>: u8 flag, u32 count, then `count` property objects
bool read_object_array(Cur& c, const std::string& pset, const std::string& name, size_t begin, size_t size, std::vector<p2m::Prop>& out) {
    const size_t end = begin + size;
    Cur a{c.b, c.S, begin};
    try {
        if (size < 5) return false;
        a.u8();
        const uint32_t count = a.u32();
        std::vector<p2m::Prop> got;
        for (uint32_t i = 0; i < count; ++i) read_body(a, pset, name + "[].", got, nullptr, nullptr);
        if (a.p != end) return false;
        out.insert(out.end(), std::make_move_iterator(got.begin()), std::make_move_iterator(got.end()));
        return true;
    } catch (const Error&) {
        return false;
    }
}

// bCObjectBase::Read of a property class: u16 version, u32 size (the body ends `size` bytes later), u16 property format version, u32 count, the properties,
// then the data of the class itself up to the end of the body
void read_body(Cur& c, const std::string& pset, const std::string& prefix, std::vector<p2m::Prop>& out, size_t* tail_begin, size_t* tail_end) {
    const uint16_t v0 = c.u16();
    if (v0 == 1 || v0 < 0x52) throw Error("an object version this reader does not know (" + std::to_string(v0) + ")");
    const uint32_t size = c.u32();
    const size_t end = c.p + size;
    if (end > c.b.size()) throw Error("an object runs past the end of the file");
    const uint16_t v1 = c.u16();
    if (v1 <= 0x1d) throw Error("an old property format (" + std::to_string(v1) + ")");
    const uint32_t n = c.u32();
    for (uint32_t i = 0; i < n; ++i) {
        const size_t rec = c.p;
        const std::string name = c.str(), type = c.str();
        if (c.u16() != kPropertyRecord) throw Error("property record marker missing at " + std::to_string(c.p - 2));
        const uint32_t sz = c.u32();
        if (c.p + sz > end) throw Error("a property runs past its object");
        const size_t pay = c.p;
        c.p += sz;
        if (p2m::starts_with(type, "bTObjArray<") && is_object_array_element(array_element_class(type)) &&
            read_object_array(c, pset, prefix + name, pay, sz, out))
            continue;
        // a property whose value is itself a property object (a flight path, a slot): u16 0xc9, u32 size = the rest of the payload; its members are named
        // "Name[].Member" like the members of an array element
        if (sz >= 10 && !p2m::starts_with(type, "bTPropertyContainer<") && p2m::rd16(c.b, pay) == 0xc9 && p2m::rd32(c.b, pay + 2) == sz - 6) {
            try {
                Cur o{c.b, c.S, pay};
                std::vector<p2m::Prop> got;
                read_body(o, pset, prefix + name + "[].", got, nullptr, nullptr);
                if (o.p == pay + sz) {
                    out.insert(out.end(), std::make_move_iterator(got.begin()), std::make_move_iterator(got.end()));
                    continue;
                }
            } catch (const Error&) {
            }
        }
        if (decode_more(c, pset, prefix + name, type, pay, sz, rec, out)) continue;
        // an array of polymorphic property objects (event filters of the state graph): accessor objects, found by their headers inside the payload; each is
        // listed under its own class like the objects of a set's tail
        if (p2m::starts_with(type, "bTRefPropertyArray<")) {
            std::vector<GSet> found;
            find_objects(c.b, c.S, pay, pay + sz, found);
            if (!found.empty()) {
                for (const GSet& n : found) append_set(out, n);
                continue;
            }
        }
        out.push_back(make_prop(pset, prefix + name, type, p2m::decode_value(type, c.b.substr(pay, sz), c.S), rec, sz));
    }
    if (c.p > end) throw Error("the properties of an object run past its size");
    if (tail_begin) *tail_begin = c.p;
    if (tail_end) *tail_end = end;
    c.p = end;
}

// bCAccessorPropertyObject: u16 1, u8 1; u16 1, u8 1, class name; u16 1, u8 0, u16 class version; then the object
std::string read_object_header(Cur& c, uint16_t* class_version) {
    if (c.u16() != 1 || c.u8() != 1) throw Error("accessor header expected at " + std::to_string(c.p - 3));
    if (c.u16() != 1 || c.u8() != 1) throw Error("object reference expected at " + std::to_string(c.p - 3));
    std::string cls = c.str();
    if (c.u16() != 1 || c.u8() != 0) throw Error("new object expected at " + std::to_string(c.p - 3));
    *class_version = c.u16();
    return cls;
}

// the tail of gCInventory_PS (docs/RE.md section 52): u16 version (0xd4), u16 2, u8 1, u32 stacks, each stack a property object, u16 3, u32 worn items, each
// a u32 slot and a property object. Read on a copy first: the tail has to end exactly where the set's object does.
bool read_inventory_tail(const Bytes& b, const std::vector<std::string>& S, GSet& s) {
    if (s.cls != "gCInventory_PS" || s.tail_end < s.tail_begin + 15) return false;
    try {
        Cur t{b, S, s.tail_begin};
        std::vector<p2m::Prop> got;
        t.u16();
        t.u16();
        t.u8();
        const uint32_t stacks = t.u32();
        if (stacks > 4096) return false;
        for (uint32_t i = 0; i < stacks; ++i) read_body(t, s.cls, std::string(), got, nullptr, nullptr);
        // the worn list: version 3 has a u32 slot in front of each item, the older version 2 (a few test NPCs) a flag byte and the items alone
        const uint16_t wv = t.u16();
        if (wv == 2) t.u8();
        else if (wv != 3) return false;
        const uint32_t worn = t.u32();
        if (worn > 4096) return false;
        for (uint32_t i = 0; i < worn; ++i) {
            if (wv == 3) t.u32();
            read_body(t, s.cls, std::string(), got, nullptr, nullptr);
        }
        if (t.p != s.tail_end) return false;
        s.props.insert(s.props.end(), std::make_move_iterator(got.begin()), std::make_move_iterator(got.end()));
        return true;
    } catch (const Error&) {
        return false;
    }
}

// the accessor objects inside the tail of a set: an accessor header (u16 1, u8 1, u16 1, u8 1, class name, u16 1, u8 0, class version) whose object then has to read
// to its exact size; anything that does not is not an object and is skipped a byte at a time
void find_objects(const Bytes& b, const std::vector<std::string>& S, size_t begin, size_t end, std::vector<GSet>& found) {
    for (size_t p = begin; p + 13 + 2 + 4 <= end;) {
        if (static_cast<uint8_t>(b[p]) == 1 && b[p + 1] == 0 && static_cast<uint8_t>(b[p + 2]) == 1 && static_cast<uint8_t>(b[p + 3]) == 1 && b[p + 4] == 0 &&
            static_cast<uint8_t>(b[p + 5]) == 1 && static_cast<uint8_t>(b[p + 8]) == 1 && b[p + 9] == 0 && b[p + 10] == 0) {
            try {
                Cur c{b, S, p};
                uint16_t cv = 0;
                GSet n;
                n.begin = p;
                n.cls = read_object_header(c, &cv);
                n.version = cv;
                if (!n.cls.empty()) {
                    read_body(c, n.cls, std::string(), n.props, &n.tail_begin, &n.tail_end);
                    if (c.p <= end) {
                        n.end = c.p;
                        read_nested(b, S, n);
                        found.push_back(std::move(n));
                        p = c.p;
                        continue;
                    }
                }
            } catch (const Error&) {
            }
        }
        ++p;
    }
}

void read_nested(const Bytes& b, const std::vector<std::string>& S, GSet& s) {
    if ((s.inventory_read = read_inventory_tail(b, S, s))) return;
    find_objects(b, S, s.tail_begin, s.tail_end, s.nested);
}

void append_set(std::vector<p2m::Prop>& out, const GSet& s) {
    out.insert(out.end(), s.props.begin(), s.props.end());
    for (const GSet& n : s.nested) append_set(out, n);
}

// ---- entities --------------------------------------------------------------------------------------------------------------------------------------

// a template entity's property sets: u32 count, per set u16 version, class name, u32 size, the object (exactly `size` bytes), 0xDEADC0DE
void read_template_sets(Cur& c, GEntity& e) {
    const uint32_t n = c.u32();
    for (uint32_t i = 0; i < n; ++i) {
        GSet s;
        s.begin = c.p;
        s.version = c.u16();
        s.cls = c.str();
        const uint32_t size = c.u32();
        const size_t end = c.p + size;
        if (end > c.b.size()) throw Error("a property set runs past the end of the file");
        uint16_t cv = 0;
        const std::string cls = read_object_header(c, &cv);
        if (cls != s.cls) throw Error("the class of a property set differs from its record (" + cls + " / " + s.cls + ")");
        read_body(c, s.cls, std::string(), s.props, &s.tail_begin, &s.tail_end);
        if (c.p != end) throw Error("a property set is not exactly its size (" + s.cls + ")");
        read_nested(c.b, c.S, s);
        c.expect(kDeadC0de, "the end mark of a property set");
        s.end = c.p;
        e.sets.push_back(std::move(s));
    }
}

// eCTemplateEntity::ReadHeader (docs/RE.md section 56; the order is the engine's, the version gates are those of its code)
void read_template_header(Cur& c, GEntity& e) {
    e.begin = c.p;
    const uint16_t v = c.u16();
    e.version = v;
    e.id = c.guid();
    c.skip(3);                    // three flags
    if (v < 0xd7) c.skip(1);
    c.skip(2);                    // two flags
    c.skip(1);                    // a flag (read as ignored below 0xdb, flag 0x200 from there)
    c.skip(1);
    if (v > 0xd5) c.skip(1);
    if (c.u8()) e.ref = c.guid();  // a reference to another template: no property sets follow
    c.skip(4);                    // float
    if (v > 0xd4) c.skip(4);
    c.skip(2);                    // insert type
    if (v < 0xd5) c.skip(1);
    e.name = c.str();
    c.skip(12 + 16);              // position, rotation
    if (v < 0xd5) c.skip(5);
    if (v < 0xd4) c.skip(1);
    if (v < 0xd5) c.skip(4);
    c.skip(8);                    // bCDateTime
    if (v < 0xd5) c.skip(1);
    if (v < 0xd9) c.skip(4);
    if (v < 0xd5) c.skip(2);
    c.skip(3);                    // three flags
    if (v > 0xd2) c.skip(1);
    if (v > 0xd9) c.skip(1);
}

// a world layer's entity: eCDynamicEntity, eCGeometryEntity and eCEntity in this order (no sizes, no accessor header: bCObjectBase::Read takes the direct path)
void read_layer_entity(Cur& c, GEntity& e) {
    e.begin = c.p;
    const uint16_t v = c.u16();
    if (v < 0xd3) throw Error("an entity version this reader does not know (" + std::to_string(v) + ")");
    e.version = v;
    e.id = c.guid();
    e.name = c.str();
    for (auto& f : e.matrix) f = c.f32();
    for (auto& f : e.box_min) f = c.f32();
    for (auto& f : e.box_max) f = c.f32();
    c.skip(16 + 24);              // sphere, the second box
    if (v < 0xd5) {
        if (c.u8()) e.creator = c.guid();
    } else {
        e.creator = c.proxy();
    }
    // eCGeometryEntity
    const uint16_t gv = c.u16();
    if (gv < 0xd6) c.skip(4);
    c.skip(64 + 24 + 16);
    c.skip(gv < 0xd4 ? 16 : 8);
    if (gv >= 0xd5) c.skip(4);
    // eCEntity
    const uint16_t ev = c.u16();
    c.skip(2);                    // second version, ignored
    c.skip(3);
    if (ev > 0xd2) c.skip(3);
    if (ev < 0xd6) c.skip(1);
    c.skip(3);
    c.skip(2);                    // insert type
    c.skip(1);
    if (ev < 0xd5) c.skip(1);
    c.skip(1);
    if (ev < 0xd5) c.skip(1);
    c.skip(8);                    // bCDateTime
    if (ev < 0xd5) c.skip(2);
    if (ev > 0xd3) c.skip(1);
    if (ev > 0xd7) c.skip(1);
    const uint32_t n = c.u32();
    for (uint32_t i = 0; i < n; ++i) {
        GSet s;
        s.begin = c.p;
        s.version = c.u16();
        uint16_t cv = 0;
        s.cls = read_object_header(c, &cv);
        read_body(c, s.cls, std::string(), s.props, &s.tail_begin, &s.tail_end);
        read_nested(c.b, c.S, s);
        c.expect(kDeadC0de, "the end mark of a property set");
        s.end = c.p;
        e.sets.push_back(std::move(s));
    }
    e.end = c.p;
}

void read_deps(Cur& c, GFile& f) {
    for (;;) {
        const uint32_t a = c.u32(), b = c.u32();
        if (a == 0xffffffffu && b == 0xffffffffu) break;
        if (a >= f.entities.size() || b >= f.entities.size()) throw Error("an entity dependency names an entity that does not exist");
        f.deps.push_back({a, b});
    }
}

}  // namespace

GFile parse_genome(const Bytes& b) {
    if (b.size() < 22 || b.compare(0, 8, "GENOMFLE") != 0) throw Error("not a GENOMFLE file");
    GFile f;
    f.table_offset = p2m::rd32(b, 10);
    if (p2m::rd16(b, 8) != 1) throw Error("an unknown container version");
    if (f.table_offset + 9 > b.size() || p2m::rd32(b, f.table_offset) != kDeadBeef) throw Error("the string table's security token is missing");
    size_t tbl = 0;
    f.strings = p2m::read_string_table(b, &tbl);
    Cur c{b, f.strings, 14};
    if (b.compare(14, 8, "GENOMETP") == 0) {
        c.p = 22;
        f.file_version = c.u16();
        const uint32_t n = c.u32();
        if (n == 0 || n > 4096) throw Error("implausible entity count");
        f.entities.resize(n);
        for (GEntity& e : f.entities) {
            read_template_header(c, e);
            if (e.ref.empty()) read_template_sets(c, e);
            e.end = c.p;
        }
        read_deps(c, f);
        if (c.p != f.table_offset) throw Error("the template does not end at its string table");
    } else if (p2m::rd32(b, 14) == kLayerMagic) {
        f.layer = true;
        c.p = 18;
        uint16_t cv = 0;
        if (read_object_header(c, &cv) != "gCDynamicLayer") throw Error("a world layer starts with a gCDynamicLayer");
        std::vector<p2m::Prop> layer_props;
        size_t tb = 0, te = 0;
        read_body(c, "gCDynamicLayer", std::string(), layer_props, &tb, &te);
        c.p = tb;  // the tail of the layer object: the context, with the entities
        if (c.u32() != 0x4f4e4547u || c.u32() != 0x4c44454du) throw Error("the magic code GENOMEDL is missing");
        f.file_version = c.u16();
        if (read_object_header(c, &cv) != "eCEntityDynamicContext") throw Error("a world layer holds an eCEntityDynamicContext");
        std::vector<p2m::Prop> ctx_props;
        size_t cb = 0, ce = 0;
        read_body(c, "eCEntityDynamicContext", std::string(), ctx_props, &cb, &ce);
        c.p = cb;
        const uint16_t ctx = c.u16();
        if (ctx > 1) c.skip(1);
        if (ctx >= 0x27 && ctx < 0xd4) c.skip(8);
        if (ctx >= 0x28) c.skip(24);
        const uint32_t n = c.u32();
        f.entities.resize(n);
        for (GEntity& e : f.entities) read_layer_entity(c, e);
        if (n > 0) read_deps(c, f);
        if (c.p != ce) throw Error("the entity context does not end where its size says");
        if (c.p != te || c.p != f.table_offset) throw Error("the layer does not end at its string table");
    } else {
        throw Error("neither a template nor a world layer");
    }
    return f;
}


p2m::Template genome_entity_props(const GEntity& e) {
    p2m::Template t;
    for (const GSet& s : e.sets) append_set(t.props, s);
    return t;
}

p2m::Template genome_template(const GFile& f) {
    p2m::Template t;
    t.strings = f.strings;
    t.name = f.strings.empty() ? std::string() : f.strings[0];
    for (const GEntity& e : f.entities) t.guids.push_back(e.id);
    t.guid = t.guids.size() > 1 ? t.guids[1] : (t.guids.empty() ? std::string() : t.guids[0]);
    for (const GEntity& e : f.entities)
        for (const GSet& s : e.sets) append_set(t.props, s);
    return t;
}

}  // namespace mk
