#include "pack2mod/cs.h"

#include <cstring>
#include <unordered_map>

#include "pack2mod/tple.h"

namespace p2m {

std::string utf8_to_latin1(std::string_view s) {
    std::string o;
    for (uint32_t c : utf8_decode(s)) o += static_cast<char>(c < 256 ? c : '?');
    return o;
}

namespace {

struct R {
    const Bytes& b;
    const std::vector<std::string>& strs;
    size_t p = 0;
    uint8_t u8() {
        if (p >= b.size()) throw Error("combat table: read past the end");
        return static_cast<uint8_t>(b[p++]);
    }
    uint16_t u16() { const uint16_t v = rd16(b, p); p += 2; return v; }
    uint32_t u32() { const uint32_t v = rd32(b, p); p += 4; return v; }
    std::optional<std::string> s() {
        const uint16_t i = u16();
        if (i == 0xFFFF) return std::nullopt;
        if (i >= strs.size()) throw Error("combat table: string index out of range");
        return strs[i];
    }
};

[[noreturn]] void bad(const char* what) { throw Error(std::string("combat table: ") + what); }

CsObj read_body(R& r, std::optional<std::string> cls, bool nested);

bool is_str_type(const std::string& t) {
    return t == "bCString" || t == "gCScriptProxyAIFunction" || t == "eCScriptProxyScript" || t == "gCScriptProxyAIState" ||
           t == "gCScriptProxyAICallback";
}

void dec_payload(R& r, CsProp& pr, size_t size, size_t end) {
    const Bytes& b = r.b;
    const size_t p0 = r.p;
    const std::string& typ = pr.t;
    CsVal& v = pr.v;
    if (typ == "float") {
        if (size != 4) bad("float size");
        v.kind = CsVal::Float;
        v.f = rdf(b, p0);
    } else if (typ == "bool") {
        if (size != 1) bad("bool size");
        v.kind = CsVal::Bool;
        v.i = b[p0] != 0;
    } else if (typ == "long" || typ == "int") {
        if (size != 4) bad("int size");
        v.kind = CsVal::Int;
        v.i = static_cast<int32_t>(rd32(b, p0));
    } else if (typ == "short") {
        if (size != 2) bad("short size");
        v.kind = CsVal::Int;
        v.i = static_cast<int16_t>(rd16(b, p0));
    } else if (is_str_type(typ)) {
        pr.enc = static_cast<int>(size);
        size_t i;
        if (size == 2) {
            i = rd16(b, p0);
        } else if (size == 3) {
            if (b.compare(p0, 3, std::string("\x01\x00\x00", 3)) != 0) bad("string form");
            i = 0xFFFF;
        } else if (size == 5) {
            if (b.compare(p0, 3, std::string("\x01\x00\x01", 3)) != 0) bad("string form");
            i = rd16(b, p0 + 3);
        } else {
            bad("string size");
        }
        v.kind = CsVal::Str;
        if (i != 0xFFFF) {
            if (i >= r.strs.size()) bad("string index");
            v.s = r.strs[i];
        }
    } else if (typ == "eCTemplateEntityProxy") {
        pr.enc = static_cast<int>(size);
        v.kind = CsVal::Proxy;
        if (size == 3) {
            if (b.compare(p0, 3, std::string("\x02\x00\x00", 3)) != 0) bad("proxy form");
        } else {
            if (size != 23 || b.compare(p0, 3, std::string("\x02\x00\x01", 3)) != 0 || b.compare(p0 + 19, 4, std::string(4, '\0')) != 0) bad("proxy form");
            v.s = hex_of(b.substr(p0 + 3, 16));
        }
    } else if (starts_with(typ, "bTPropertyContainer<enum")) {
        if (size != 6 || rd16(b, p0) != 0xC9) bad("enum form");
        v.kind = CsVal::Enum;
        v.enum_type = typ.substr(std::strlen("bTPropertyContainer<enum "), typ.size() - std::strlen("bTPropertyContainer<enum ") - 1);
        v.i = rd32(b, p0 + 2);
    } else if (typ == "bCVector2" || typ == "bCVector" || typ == "bCFloatColor" || typ == "bCRange1") {
        if (size % 4) bad("float list size");
        v.kind = CsVal::Floats;
        for (size_t o = 0; o < size; o += 4) v.floats.push_back(rdf(b, p0 + o));
    } else if (starts_with(typ, "bTObjArray<class")) {
        v.kind = CsVal::Arr;
        v.arr_ver = r.u8();
        const uint32_t n = r.u32();
        for (uint32_t k = 0; k < n; ++k) v.objs.push_back(read_body(r, std::nullopt, true));
    } else if (starts_with(typ, "g") || starts_with(typ, "e")) {
        v.kind = CsVal::Obj;
        v.objs.push_back(read_body(r, std::nullopt, true));
    } else {
        bad("unknown property type");
    }
    r.p = end;
}

CsObj read_body(R& r, std::optional<std::string> cls, bool nested) {
    CsObj o;
    o.cls = std::move(cls);
    o.v0 = r.u16();
    const uint32_t size = r.u32();
    o.end = r.p + size;
    o.v1 = r.u16();
    const uint32_t n = r.u32();
    for (uint32_t i = 0; i < n; ++i) {
        CsProp pr;
        pr.n = r.s().value_or("");
        pr.t = r.s().value_or("");
        pr.pv = r.u16();
        const uint32_t sz = r.u32();
        const size_t pend = r.p + sz;
        if (pend > r.b.size()) bad("property outside the file");
        dec_payload(r, pr, sz, pend);
        o.props.push_back(std::move(pr));
    }
    if (nested) {
        if (o.end < r.p || o.end > r.b.size()) bad("object end");
        o.extra = r.b.substr(r.p, o.end - r.p);
        r.p = o.end;
    }
    return o;
}

CsObj read_accessor(R& r, bool jump) {
    std::array<uint32_t, 7> acc{};
    acc[0] = r.u16();
    acc[1] = r.u8();
    if (acc[1] != 1) bad("accessor flag");
    acc[2] = r.u16();
    acc[3] = r.u8();
    auto cls = r.s();
    acc[4] = r.u16();
    acc[5] = r.u8();
    acc[6] = r.u16();
    CsObj o = read_body(r, std::move(cls), false);
    o.acc = acc;
    if (jump) {
        if (o.end < r.p || o.end > r.b.size()) bad("accessor end");
        o.extra = r.b.substr(r.p, o.end - r.p);
        r.p = o.end;
    }
    return o;
}

// ---------------------------------------------------------------------------------------------- writer

struct W {
    std::vector<std::string> strs;
    std::unordered_map<std::string, size_t> idx;
    explicit W(const std::vector<std::string>& s) : strs(s) {
        for (size_t i = 0; i < strs.size(); ++i) idx.emplace(strs[i], i);
    }
    uint16_t sid(const std::optional<std::string>& s) {
        if (!s) return 0xFFFF;
        const auto it = idx.find(*s);
        if (it != idx.end()) return static_cast<uint16_t>(it->second);
        idx[*s] = strs.size();
        strs.push_back(*s);
        return static_cast<uint16_t>(strs.size() - 1);
    }
    uint16_t sid(const std::string& s) { return sid(std::optional<std::string>(s)); }
};

Bytes enc_body(W& w, const CsObj& o, const Bytes& tail = Bytes());

Bytes enc_payload(W& w, const CsProp& p) {
    const CsVal& v = p.v;
    const std::string& typ = p.t;
    Bytes o;
    if (typ == "float") {
        wrf(o, static_cast<float>(v.f));
    } else if (typ == "bool") {
        o.push_back(v.i ? 1 : 0);
    } else if (typ == "long" || typ == "int") {
        wr32(o, static_cast<uint32_t>(static_cast<int32_t>(v.i)));
    } else if (typ == "short") {
        wr16(o, static_cast<uint16_t>(static_cast<int16_t>(v.i)));
    } else if (is_str_type(typ)) {
        if (!v.s) {
            if (p.enc == 3) o = std::string("\x01\x00\x00", 3);
            else if (p.enc == 2) wr16(o, 0xFFFF);
            else o = std::string("\x01\x00\x01\xff\xff", 5);
        } else {
            const uint16_t i = w.sid(*v.s);
            if (p.enc == 2) {
                wr16(o, i);
            } else {
                o = std::string("\x01\x00\x01", 3);
                wr16(o, i);
            }
        }
    } else if (typ == "eCTemplateEntityProxy") {
        if (!v.s) {
            o = std::string("\x02\x00\x00", 3);
        } else {
            o = std::string("\x02\x00\x01", 3);
            for (size_t k = 0; k + 1 < v.s->size(); k += 2) o.push_back(static_cast<char>(std::stoi(v.s->substr(k, 2), nullptr, 16)));
            o += std::string(4, '\0');
        }
    } else if (starts_with(typ, "bTPropertyContainer<enum")) {
        wr16(o, 0xC9);
        wr32(o, static_cast<uint32_t>(v.i));
    } else if (typ == "bCVector2" || typ == "bCVector" || typ == "bCFloatColor" || typ == "bCRange1") {
        for (float f : v.floats) wrf(o, f);
    } else if (starts_with(typ, "bTObjArray<class")) {
        o.push_back(static_cast<char>(v.arr_ver));
        wr32(o, static_cast<uint32_t>(v.objs.size()));
        for (const CsObj& e : v.objs) o += enc_body(w, e);
    } else {
        o = enc_body(w, v.objs.at(0));
    }
    return o;
}

Bytes enc_props(W& w, const std::vector<CsProp>& props) {
    Bytes out;
    wr32(out, static_cast<uint32_t>(props.size()));
    for (const CsProp& p : props) {
        const Bytes pl = enc_payload(w, p);
        wr16(out, w.sid(p.n));
        wr16(out, w.sid(p.t));
        wr16(out, p.pv);
        wr32(out, static_cast<uint32_t>(pl.size()));
        out += pl;
    }
    return out;
}

Bytes enc_body(W& w, const CsObj& o, const Bytes& tail) {
    Bytes rest;
    wr16(rest, o.v1);
    rest += enc_props(w, o.props);
    rest += o.extra;
    rest += tail;
    Bytes out;
    wr16(out, o.v0);
    wr32(out, static_cast<uint32_t>(rest.size()));
    return out + rest;
}

void acc_head(Bytes& out, W& w, const CsObj& o) {
    const auto& a = *o.acc;
    wr16(out, static_cast<uint16_t>(a[0]));
    out.push_back(static_cast<char>(a[1]));
    wr16(out, static_cast<uint16_t>(a[2]));
    out.push_back(static_cast<char>(a[3]));
    wr16(out, w.sid(o.cls));
    wr16(out, static_cast<uint16_t>(a[4]));
    out.push_back(static_cast<char>(a[5]));
    wr16(out, static_cast<uint16_t>(a[6]));
}

Bytes enc_accessor(W& w, const CsObj& o) {
    Bytes out;
    acc_head(out, w, o);
    return out + enc_body(w, o);
}

}  // namespace

CsDoc parse_combat(const Bytes& b) {
    size_t tbl = 0;
    CsDoc d;
    {
        // the string table of a GENOMFLE file is Latin-1; keep the raw strings of the table (as UTF-8 internally)
        d.strs = read_string_table(b, &tbl);
    }
    if (b.compare(0, 8, "GENOMFLE") != 0 || rd16(b, 8) != 1) bad("not a GENOMFLE file");
    R r{b, d.strs};
    r.p = 14;
    if (r.u32() != 0x31303056) bad("not a V001 archive");
    d.flag = r.u8();
    const uint32_t cnt = r.u32();
    struct Head { uint32_t lo, hi; std::string name; };
    std::vector<Head> heads;
    for (uint32_t i = 0; i < cnt; ++i) {
        Head h;
        h.lo = r.u32();
        h.hi = r.u32();
        h.name = r.s().value_or("");
        heads.push_back(std::move(h));
    }
    for (uint32_t i = 0; i < cnt; ++i) {
        CsSpecies sp;
        sp.name = heads[i].name;
        sp.ft_lo = heads[i].lo;
        sp.ft_hi = heads[i].hi;
        sp.obj = read_accessor(r, false);
        sp.ver = r.u16();
        for (uint32_t n = r.u32(), k = 0; k < n; ++k) sp.styles.push_back(read_accessor(r, true));
        for (uint32_t n = r.u32(), k = 0; k < n; ++k) sp.moves.push_back(read_accessor(r, true));
        for (uint32_t n = r.u32(), k = 0; k < n; ++k) sp.ais.push_back(read_accessor(r, true));
        if (r.p != sp.obj.end) bad("species end");
        d.species.push_back(std::move(sp));
    }
    if (r.p != tbl) bad("data ends before the string table");
    return d;
}

Bytes write_combat(const CsDoc& d) {
    W w(d.strs);
    Bytes heads, body;
    for (const CsSpecies& sp : d.species) {
        wr32(heads, sp.ft_lo);
        wr32(heads, sp.ft_hi);
        wr16(heads, w.sid(sp.name));
    }
    for (const CsSpecies& sp : d.species) {
        Bytes tail;
        wr16(tail, sp.ver);
        wr32(tail, static_cast<uint32_t>(sp.styles.size()));
        for (const CsObj& s : sp.styles) tail += enc_accessor(w, s);
        wr32(tail, static_cast<uint32_t>(sp.moves.size()));
        for (const CsObj& s : sp.moves) tail += enc_accessor(w, s);
        wr32(tail, static_cast<uint32_t>(sp.ais.size()));
        for (const CsObj& s : sp.ais) tail += enc_accessor(w, s);
        const CsObj& o = sp.obj;
        Bytes rest;
        wr16(rest, o.v1);
        rest += enc_props(w, o.props);
        rest += tail;
        acc_head(body, w, o);
        wr16(body, o.v0);
        wr32(body, static_cast<uint32_t>(rest.size()));
        body += rest;
    }
    Bytes data;
    wr32(data, 0x31303056);
    data.push_back(static_cast<char>(d.flag));
    wr32(data, static_cast<uint32_t>(d.species.size()));
    data += heads;
    data += body;
    Bytes out = "GENOMFLE";
    wr16(out, 1);
    wr32(out, static_cast<uint32_t>(14 + data.size()));
    out += data;
    out += std::string("\xef\xbe\xad\xde\x01", 5);
    wr32(out, static_cast<uint32_t>(w.strs.size()));
    for (const std::string& s : w.strs) {
        const std::string l = utf8_to_latin1(s);
        wr16(out, static_cast<uint16_t>(l.size()));
        out += l;
    }
    return out;
}

bool cs_equal(const CsVal& a, const CsVal& b) {
    if (a.kind != b.kind) return false;
    switch (a.kind) {
        case CsVal::Float: return a.f == b.f;
        case CsVal::Int:
        case CsVal::Bool: return a.i == b.i;
        case CsVal::Str:
        case CsVal::Proxy: return a.s == b.s;
        case CsVal::Enum: return a.enum_type == b.enum_type && a.i == b.i;
        case CsVal::Floats: return a.floats == b.floats;
        case CsVal::Arr: return a.arr_ver == b.arr_ver && cs_equal(a.objs, b.objs);
        case CsVal::Obj: return cs_equal(a.objs, b.objs);
    }
    return false;
}

bool cs_equal(const CsObj& a, const CsObj& b) {
    if (a.cls != b.cls || a.v0 != b.v0 || a.v1 != b.v1 || a.extra != b.extra || a.props.size() != b.props.size()) return false;
    for (size_t i = 0; i < a.props.size(); ++i) {
        const CsProp &x = a.props[i], &y = b.props[i];
        if (x.n != y.n || x.t != y.t || x.pv != y.pv || !cs_equal(x.v, y.v)) return false;
    }
    return true;
}

bool cs_equal(const std::vector<CsObj>& a, const std::vector<CsObj>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (!cs_equal(a[i], b[i])) return false;
    return true;
}

}  // namespace p2m
