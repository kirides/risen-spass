// Shared test fixture of the pack2mod tests (tests/pack2mod_tests.inc) and of the editor project tests (tests/modkit/pack2project_tests.inc): a tiny writer for
// entity templates (GENOMFLE) and item templates built with it. Textually included, no includes of its own.

// ---- helpers: a tiny writer for templates (GENOMFLE) -----------------------------------------------------------------------------------------

struct TplBuilder {
    std::vector<std::string> strs;
    p2m::Bytes body;
    int idx(const std::string& s) {
        for (size_t i = 0; i < strs.size(); ++i)
            if (strs[i] == s) return static_cast<int>(i);
        strs.push_back(s);
        return static_cast<int>(strs.size() - 1);
    }
    void set(const std::string& name) {
        body += std::string("\x01\x00\x01\x01\x00\x01", 6);
        p2m::wr16(body, static_cast<uint16_t>(idx(name)));
    }
    static p2m::Bytes rec(int n, int t, const p2m::Bytes& payload) {
        p2m::Bytes r;
        p2m::wr16(r, static_cast<uint16_t>(n));
        p2m::wr16(r, static_cast<uint16_t>(t));
        p2m::wr16(r, 0x1E);
        p2m::wr32(r, static_cast<uint32_t>(payload.size()));
        return r + payload;
    }
    void prop(const std::string& n, const std::string& type, const p2m::Bytes& payload) { body += rec(idx(n), idx(type), payload); }
    void lng(const std::string& n, int32_t v) {
        p2m::Bytes p;
        p2m::wr32(p, static_cast<uint32_t>(v));
        prop(n, "long", p);
    }
    void flt(const std::string& n, float v) {
        p2m::Bytes p;
        p2m::wrf(p, v);
        prop(n, "float", p);
    }
    void str(const std::string& n, const std::string& v) {
        p2m::Bytes p;
        p2m::wr16(p, static_cast<uint16_t>(idx(v)));
        prop(n, "bCString", p);
    }
    void enm(const std::string& n, const std::string& type, uint32_t v) {
        p2m::Bytes p;
        p2m::wr16(p, 0xC9);
        p2m::wr32(p, v);
        prop(n, "bTPropertyContainer<enum " + type + ">", p);
    }
    p2m::Bytes skill_element(int skill, int amount) {
        p2m::Bytes e;
        p2m::Bytes p;
        p2m::wr16(p, 0xC9);
        p2m::wr32(p, static_cast<uint32_t>(skill));
        e += rec(idx("Skill"), idx("bTPropertyContainer<enum gESkill>"), p);
        p2m::Bytes a;
        p2m::wr32(a, static_cast<uint32_t>(amount));
        e += rec(idx("Amount"), idx("long"), a);
        return e;
    }
    void skills(const std::string& arr, const std::vector<std::pair<int, int>>& list) {
        p2m::Bytes payload;
        for (const auto& s : list) payload += skill_element(s.first, s.second);
        prop(arr, "bTObjArray<class gCSkillValue>", payload);
    }
    void guid_ref(const std::string& n, const std::string& type, const p2m::Bytes& guid16) {
        p2m::Bytes p = std::string("\x02\x00\x01", 3) + guid16 + std::string(4, '\0');
        prop(n, type, p);
    }
    // header, the two GUID records, the property stream, the string table; the builder's string 0 / 1 are the template name
    p2m::Bytes build(const p2m::Bytes& guid1, const p2m::Bytes& guid2) {
        p2m::Bytes data = "GENOMFLE";
        p2m::wr16(data, 1);
        p2m::wr32(data, 0);
        data += "GENOMETP";
        data += std::string("\xdb\x00", 2) + std::string("\x02\x00\x00\x00", 4) + std::string("\xdb\x00", 2) + guid1;  // property data starts at 0x2e
        data += std::string("\xdb\x00", 2) + guid2;
        data += body;
        const uint32_t tbl = static_cast<uint32_t>(data.size());
        std::memcpy(data.data() + 10, &tbl, 4);
        data += std::string("\xef\xbe\xad\xde\x01", 5);
        p2m::wr32(data, static_cast<uint32_t>(strs.size()));
        for (const std::string& s : strs) {
            p2m::wr16(data, static_cast<uint16_t>(s.size()));
            data += s;
        }
        return data;
    }
};

template <class Fn>
p2m::Bytes make_tpl(const std::string& name, const p2m::Bytes& g1, const p2m::Bytes& g2, Fn fill) {
    TplBuilder b;
    b.strs = {name + ".tple", name};
    fill(b);
    return b.build(g1, g2);
}

p2m::Bytes guid_bytes(int seed) {
    p2m::Bytes g;
    for (int i = 0; i < 16; ++i) g.push_back(static_cast<char>(seed * 16 + i + 1));
    return g;
}

p2m::Bytes item_tpl(const std::string& name, int g, int use_type, int gold, int damage, std::vector<std::pair<int, int>> req, const std::string& mesh) {
    return make_tpl(name, guid_bytes(g), guid_bytes(g + 100), [&](TplBuilder& b) {
        b.set("eCMesh_PS");
        b.str("MeshFileName", mesh);
        b.set("gCItem_PS");
        b.lng("GoldValue", gold);
        b.enm("UseType", "gEItemUseType", static_cast<uint32_t>(use_type));
        b.enm("Category", "gEItemCategory", 1);
        b.skills("RequiredSkills", req);
        b.set("gCDamage_PS");
        b.lng("DamageAmount", damage);
        b.flt("DamageMultiplier", 1.5f);
    });
}
