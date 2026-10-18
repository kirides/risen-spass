#include "pack2mod/tple.h"

#include <algorithm>
#include <map>
#include <unordered_set>

namespace p2m {

std::string Val::repr() const {
    switch (k) {
        case None: return "None";
        case Int: return std::to_string(i);
        case Bool: return i ? "True" : "False";
        case Float: return fmt_g9(d);
        case Str: return s;
        case Vec: {
            std::string o = "(";
            for (size_t n = 0; n < v.size(); ++n) o += (n ? ", " : "") + fmt_g(v[n]);
            return o + ")";
        }
    }
    return "";
}

bool val_eq(const Val& a, const Val& b) {
    if (a.k == Val::None || b.k == Val::None) return a.k == b.k;
    if (a.is_num() && b.is_num()) {
        if (a.k != Val::Float && b.k != Val::Float) return a.i == b.i;
        return a.num() == b.num();
    }
    if (a.k != b.k) return false;
    if (a.k == Val::Str) return a.s == b.s;
    return a.v == b.v;
}

namespace {

const std::unordered_set<std::string>& prims() {
    static const std::unordered_set<std::string> s = {
        "int", "long", "short", "float", "bool", "bCVector", "bCFloatColor", "bCString", "unsigned long", "unsigned int", "char",
        "eCTemplateEntityProxy", "eCEntityProxy", "eCGuiBitmapProxy2", "eCScriptProxyScript", "gCScriptProxyAIState", "gCScriptProxyAIFunction",
        "gCScriptProxyAICallback", "bCMatrix", "bCQuaternion", "bCBox", "bCRect", "bCEulerAngles", "bCFloatAlphaColor", "bCImageResourceString",
        "bCRange1", "bCRange3", "bCVector2", "bCImageOrMaterialResourceString", "bCMeshResourceString", "bCSpeedTreeResourceString"};
    return s;
}
const std::unordered_set<std::string>& string_types() {
    static const std::unordered_set<std::string> s = {
        "bCString", "eCScriptProxyScript", "gCScriptProxyAIState", "gCScriptProxyAIFunction", "gCScriptProxyAICallback", "eCTemplateEntityProxy",
        "eCEntityProxy", "eCGuiBitmapProxy2", "bCImageResourceString", "bCImageOrMaterialResourceString", "bCMeshResourceString",
        "bCSpeedTreeResourceString"};
    return s;
}
const std::unordered_set<std::string>& vec_types() {
    static const std::unordered_set<std::string> s = {"bCVector", "bCFloatColor", "bCEulerAngles", "bCQuaternion", "bCFloatAlphaColor", "bCRange1",
                                                      "bCRange3", "bCVector2", "bCBox", "bCMatrix"};
    return s;
}

Val decode(const std::string& typ, const Bytes& pl, const std::vector<std::string>& strs) {
    const size_t n = pl.size();
    if ((typ == "int" || typ == "long") && n == 4) return Val::of_int(static_cast<int32_t>(rd32(pl, 0)));
    if ((typ == "unsigned long" || typ == "unsigned int") && n == 4) return Val::of_int(rd32(pl, 0));
    if (typ == "short" && n == 2) return Val::of_int(static_cast<int16_t>(rd16(pl, 0)));
    if (typ == "float" && n == 4) return Val::of_float(py_round(rdf(pl, 0), 5));
    if (typ == "bool" && n == 1) return Val::of_bool(pl[0] != 0);
    if (vec_types().count(typ) && n % 4 == 0) {
        std::vector<double> v;
        for (size_t o = 0; o < n; o += 4) v.push_back(py_round(rdf(pl, o), 4));
        return Val::of_vec(std::move(v));
    }
    if (string_types().count(typ)) {
        size_t idx;
        if (n == 2) {
            idx = rd16(pl, 0);
        } else if (n == 3) {
            return Val::of_str("");
        } else if (n == 5 && pl[2] == 1) {
            idx = rd16(pl, 3);
        } else if (n == 23 && pl[2] == 1) {
            return Val::of_str("@" + hex_of(pl.substr(3, 16)));
        } else {
            return Val::of_str(hex_of(pl));
        }
        if (idx == 0xFFFF) return Val::of_str("");
        return Val::of_str(idx < strs.size() ? strs[idx] : "#" + std::to_string(idx));
    }
    if (starts_with(typ, "bTPropertyContainer<enum") && n == 6) return Val::of_int(rd32(pl, 2));
    return Val::of_str(hex_of(pl));
}

bool any_prefix(const std::string& s, std::initializer_list<const char*> ps) {
    for (const char* p : ps)
        if (starts_with(s, p)) return true;
    return false;
}

}  // namespace

Val decode_value(const std::string& type, const Bytes& payload, const std::vector<std::string>& strs) { return decode(type, payload, strs); }

std::vector<std::string> read_string_table(const Bytes& b, size_t* table_offset) {
    const size_t p = rd32(b, 10);
    if (p + 9 > b.size() || b.compare(p, 4, "\xef\xbe\xad\xde", 4) != 0) throw Error("no string table");
    const uint32_t cnt = rd32(b, p + 5);
    size_t q = p + 9;
    std::vector<std::string> strs;
    strs.reserve(cnt);
    for (uint32_t i = 0; i < cnt; ++i) {
        const uint16_t n = rd16(b, q);
        q += 2;
        if (q + n > b.size()) throw Error("string table is cut off");
        strs.push_back(latin1_to_utf8(std::string_view(b).substr(q, n)));
        q += n;
    }
    if (table_offset) *table_offset = p;
    return strs;
}

Template parse_template(const Bytes& b) {
    size_t tbl = 0;
    Template t;
    t.strings = read_string_table(b, &tbl);
    const auto& strs = t.strings;
    const size_t n = strs.size();
    t.name = n ? strs[0] : "";
    for (size_t i = 0x1c; i < std::min<size_t>(tbl, 0x200); ++i)
        if (i + 2 <= b.size() && b[i] == '\xDB' && b[i + 1] == '\0') t.guids.push_back("@" + hex_of(b.substr(i + 2, 16)));
    t.guid = t.guids.size() > 1 ? t.guids[1] : (t.guids.empty() ? "" : t.guids[0]);

    // property set markers
    static const char sig[6] = {1, 0, 1, 1, 0, 1};
    std::map<size_t, std::string> ps_at;
    size_t i = 0x2e;
    while (i + 6 <= tbl) {
        const size_t f = b.find(std::string_view(sig, 6), i);
        if (f == std::string::npos || f + 6 > tbl) break;
        i = f;
        if (i + 8 <= tbl) {
            const size_t idx = rd16(b, i + 6);
            if (idx < n && (ends_with(strs[idx], "_PS") || starts_with(strs[idx], "eC") || starts_with(strs[idx], "gC"))) ps_at[i] = strs[idx];
        }
        ++i;
    }
    std::vector<std::pair<size_t, std::string>> pos(ps_at.begin(), ps_at.end());
    std::string cur;
    size_t p = 0x2e, pi = 0;
    std::vector<std::pair<size_t, std::string>> stack;  // end offset, container property name of the arrays / records being walked
    while (p + 10 <= tbl) {
        while (!stack.empty() && p >= stack.back().first) stack.pop_back();
        while (pi < pos.size() && pos[pi].first <= p) cur = pos[pi++].second;
        const size_t ni = rd16(b, p), ti = rd16(b, p + 2), mk = rd16(b, p + 4), sz = rd32(b, p + 6);
        if (mk == 0x1E && ni < n && ti < n && sz <= tbl - p - 10 && !any_prefix(strs[ni], {"bT", "bC"}) &&
            (prims().count(strs[ti]) ||
             any_prefix(strs[ti], {"bTPropertyContainer<", "bTObjArray", "bTValArray", "class ", "gC", "eC"}))) {
            const std::string& typ = strs[ti];
            if (prims().count(typ) || starts_with(typ, "bTPropertyContainer<")) {
                const Bytes payload = b.substr(p + 10, sz);
                std::string nm;
                for (const auto& s : stack) nm += s.second + ".";
                nm += strs[ni];
                Prop pr;
                pr.pset = cur;
                pr.name = nm;
                pr.type = typ;
                pr.value = decode(typ, payload, strs);
                pr.off = p;
                pr.size = sz;
                t.props.push_back(std::move(pr));
                p += 10 + sz;
                continue;
            }
            stack.emplace_back(p + 10 + sz, strs[ni] + "[]");
            p += 10;
            continue;
        }
        ++p;
    }
    return t;
}

}  // namespace p2m
