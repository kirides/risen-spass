#include "core/tpl_inventory.h"

#include <algorithm>

namespace mk {

using p2m::Bytes;
using p2m::Error;

namespace {

struct Cur {
    const Bytes& b;
    const std::vector<std::string>& S;
    size_t p = 0;
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
    uint8_t u8() {
        if (p + 1 > b.size()) throw Error("read past the end of the data");
        return static_cast<uint8_t>(b[p++]);
    }
    const std::string& str(uint16_t i) const {
        if (i >= S.size()) throw Error("string index out of range");
        return S[i];
    }
};

struct Props {
    std::vector<std::pair<std::string, int64_t>> ints;  // int / long / enum
    std::string guid;                                    // the first eCTemplateEntityProxy
    int64_t get(const char* n, int64_t def) const {
        for (auto& [k, v] : ints)
            if (k == n) return v;
        return def;
    }
};

// `u16 v0, u32 size, u16 v1, u32 nprops, props...`; the body ends `size` bytes after the size field (which includes a trailing u16)
size_t read_body(Cur& c, Props& out, bool jump = true) {
    c.u16();
    const uint32_t size = c.u32();
    const size_t end = c.p + size;
    if (end > c.b.size()) throw Error("object runs past the end of the file");
    c.u16();
    const uint32_t n = c.u32();
    if (n > 64) throw Error("implausible property count");
    for (uint32_t i = 0; i < n; ++i) {
        const std::string name = c.str(c.u16());
        const std::string type = c.str(c.u16());
        c.u16();
        const uint32_t sz = c.u32();
        const size_t pend = c.p + sz;
        if (pend > end) throw Error("property runs past its object");
        if ((type == "int" || type == "long") && sz == 4) {
            out.ints.push_back({name, static_cast<int32_t>(p2m::rd32(c.b, c.p))});
        } else if (type.rfind("bTPropertyContainer<enum", 0) == 0 && sz == 6) {
            out.ints.push_back({name, static_cast<int64_t>(p2m::rd32(c.b, c.p + 2))});
        } else if (type == "eCTemplateEntityProxy" && sz == 23 && out.guid.empty()) {
            out.guid = "@" + p2m::hex_of(std::string_view(c.b.data() + c.p + 3, 16));
        }
        c.p = pend;
    }
    if (jump) c.p = end;
    return end;
}

}  // namespace

RawInventory decode_inventory(const Bytes& file, const std::vector<std::string>& strings) {
    RawInventory out;
    try {
        const auto it = std::find(strings.begin(), strings.end(), "gCInventory_PS");
        if (it == strings.end()) return out;
        const uint16_t idx = static_cast<uint16_t>(it - strings.begin());
        // the set's object header: 01 00 01 01 00 01 <u16 class string> 01 00 00 <u16>
        std::string pat("\x01\x00\x01\x01\x00\x01", 6);
        pat.push_back(static_cast<char>(idx & 0xff));
        pat.push_back(static_cast<char>(idx >> 8));
        const size_t at = file.find(pat);
        if (at == std::string::npos) return out;
        Cur c{file, strings, at + pat.size() + 5};
        Props flags;
        read_body(c, flags, false);  // GeneratedPlunder, GeneratedTrade: the stacks follow inside this object
        c.u16();              // 0xd4
        c.u16();              // 2
        c.u8();               // 1
        const uint32_t nstacks = c.u32();
        if (nstacks > 4096) return out;
        for (uint32_t i = 0; i < nstacks; ++i) {
            Props s;
            read_body(c, s);
            RawStack st;
            st.guid = s.guid;
            st.amount = static_cast<int>(s.get("Amount", 1));
            st.selection = static_cast<int>(s.get("Selection", 0));
            st.select_limit = static_cast<int>(s.get("SelectLimit", 0));
            st.quick_slot = static_cast<int>(s.get("QuickSlot", -1));
            st.type = static_cast<int>(s.get("Type", 0));
            st.equip_slot = static_cast<int>(s.get("EquipSlot", 0));
            out.stacks.push_back(std::move(st));
        }
        c.u16();  // 3
        const uint32_t nworn = c.u32();
        if (nworn > 4096) return out;
        for (uint32_t i = 0; i < nworn; ++i) {
            RawWorn w;
            w.slot = static_cast<int>(c.u32());
            Props s;
            read_body(c, s);
            w.guid = s.guid;
            out.worn.push_back(std::move(w));
        }
        out.found = true;
    } catch (const std::exception&) {
        out = RawInventory{};
    }
    return out;
}

}  // namespace mk
