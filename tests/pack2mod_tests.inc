// risen-pack2mod: archive reader/writer round trip, template / string table / combat table readers, text helpers, Lua emitter and an end-to-end
// conversion of synthetic mini archives (no game data). Included by test_main.cpp inside its anonymous namespace.

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

// ---- archives ----------------------------------------------------------------------------------------------------------------------------

TEST(p2m_pak_round_trip) {
    TempDir t;
    std::vector<p2m::PakFile> files = {{"Items/Sub/a.tple", std::string(500, 'x') + "hello", true},
                                       {"b._xmsh", std::string("\x01\x02\x03", 3), false},
                                       {"Items/c.txt", "", false},
                                       {"Items/Sub/z.bin", std::string(3000, '\0'), true}};
    t.write("x.p00", p2m::build_pak(files));
    auto pak = p2m::Pak::open((t.path / "x.p00").string());
    CHECK(pak->entries().size() == 4);
    const auto* a = pak->find("items/SUB/A.tple");
    CHECK(a && a->compressed() && a->size == 505 && a->path == "Items/Sub/a.tple");
    CHECK(a && pak->read(*a) == files[0].data);
    const auto* b = pak->find("b._xmsh");
    CHECK(b && !b->compressed() && pak->read(*b) == files[1].data);
    const auto* c = pak->find("Items/c.txt");
    CHECK(c && c->size == 0 && pak->read(*c).empty());
    const auto* z = pak->find("Items/Sub/z.bin");
    CHECK(z && z->compressed() && pak->read(*z) == files[3].data);
    CHECK(pak->find("nothing") == nullptr);
}

TEST(p2m_pak_rejects_garbage) {
    TempDir t;
    t.write("bad.pak", std::string(200, 'x'));
    t.write("short.pak", "G3V0");
    bool threw1 = false, threw2 = false, threw3 = false;
    try { p2m::Pak::open((t.path / "bad.pak").string()); } catch (const p2m::Error&) { threw1 = true; }
    try { p2m::Pak::open((t.path / "short.pak").string()); } catch (const p2m::Error&) { threw2 = true; }
    try { p2m::Pak::open((t.path / "missing.pak").string()); } catch (const p2m::Error&) { threw3 = true; }
    CHECK(threw1 && threw2 && threw3);
    // a damaged member: the stored bytes are no zlib stream
    auto bytes = p2m::build_pak({{"a.bin", std::string(1000, 'q'), true}});
    bytes[0x30 + 4] ^= 0x55;
    bytes[0x30 + 5] ^= 0x55;
    t.write("corrupt.p00", bytes);
    auto pak = p2m::Pak::open((t.path / "corrupt.p00").string());
    bool threw4 = false;
    try { pak->read(pak->entries()[0]); } catch (const p2m::Error&) { threw4 = true; }
    CHECK(threw4);
}

TEST(p2m_tab_round_trip) {
    const p2m::Bytes b = p2m::build_tab({"German_Text", "English_Text"}, {{"FO_A", {"K\xC3\xA4se", "Cheese"}}, {"FO_B", {"x", "Emoji \xF0\x9F\x98\x80"}}});
    const p2m::TabTable t = p2m::read_tab(b);
    CHECK(t.columns.size() == 3 && t.columns[0] == "ID" && t.columns[2] == "English_Text");
    CHECK(t.ids.size() == 2 && t.has("FO_A") && !t.has("FO_C"));
    CHECK(t.cell("FO_A", "German_Text") && *t.cell("FO_A", "German_Text") == "K\xC3\xA4se");
    CHECK(t.cell("FO_B", "English_Text") && *t.cell("FO_B", "English_Text") == "Emoji \xF0\x9F\x98\x80");
    CHECK(t.cell("FO_B", "Nope") == nullptr && t.cell("FO_X", "English_Text") == nullptr);
    bool threw = false;
    try { p2m::read_tab("TAB0"); } catch (const p2m::Error&) { threw = true; }
    CHECK(threw);
    bool threw2 = false;
    try { p2m::read_tab(b + "x"); } catch (const p2m::Error&) { threw2 = true; }
    CHECK(threw2);
}

TEST(p2m_template_parse) {
    const p2m::Bytes data = item_tpl("It_Test", 3, 1, 50, 15, {{7, 10}, {8, 5}}, "It_Wpn_Test.xmsh");
    const p2m::Template t = p2m::parse_template(data);
    CHECK(t.name == "It_Test.tple" && t.base_name() == "It_Test");
    CHECK(t.guids.size() >= 2 && t.guids[0] == "@" + p2m::hex_of(guid_bytes(3)));
    auto find = [&](const std::string& key) -> const p2m::Prop* {
        for (const auto& p : t.props)
            if (p.key() == key) return &p;
        return nullptr;
    };
    const auto* gold = find("gCItem_PS.GoldValue");
    CHECK(gold && gold->value.is_int() && gold->value.i == 50 && gold->type == "long");
    const auto* use = find("gCItem_PS.UseType");
    CHECK(use && use->value.is_int() && use->value.i == 1);
    const auto* mesh = find("eCMesh_PS.MeshFileName");
    CHECK(mesh && mesh->value.is_str() && mesh->value.s == "It_Wpn_Test.xmsh");
    const auto* mul = find("gCDamage_PS.DamageMultiplier");
    CHECK(mul && mul->value.k == p2m::Val::Float && mul->value.d == 1.5);
    CHECK(find("gCItem_PS.RequiredSkills[].Skill") && find("gCItem_PS.RequiredSkills[].Amount"));
    int arr = 0;
    for (const auto& p : t.props) arr += p.key() == "gCItem_PS.RequiredSkills[].Amount";
    CHECK(arr == 2);
    bool threw = false;
    try { p2m::parse_template("GENOMFLE"); } catch (const p2m::Error&) { threw = true; }
    CHECK(threw);
}

TEST(p2m_template_guid_reference) {
    const p2m::Bytes data = make_tpl("It_Rec", guid_bytes(1), guid_bytes(2), [](TplBuilder& b) {
        b.set("gCRecipe_PS");
        b.guid_ref("ResultItem", "eCTemplateEntityProxy", guid_bytes(7));
    });
    const p2m::Template t = p2m::parse_template(data);
    bool found = false;
    for (const auto& p : t.props)
        if (p.name == "ResultItem") found = p.value.is_str() && p.value.s == "@" + p2m::hex_of(guid_bytes(7));
    CHECK(found);
}

TEST(p2m_val_equality_follows_python) {
    using p2m::Val;
    CHECK(Val::of_int(1) == Val::of_float(1.0));
    CHECK(Val::of_bool(true) == Val::of_int(1));
    CHECK(Val::of_int(2) != Val::of_float(2.5));
    CHECK(Val::of_str("a") != Val::of_int(1));
    CHECK(Val() == Val() && Val() != Val::of_int(0));
    CHECK(Val::of_vec({1, 2}) == Val::of_vec({1, 2}) && Val::of_vec({1, 2}) != Val::of_vec({1, 3}));
}

// ---- combat table -------------------------------------------------------------------------------------------------------------------------

p2m::CsObj cs_obj(const std::string& cls, std::vector<p2m::CsProp> props, std::array<uint32_t, 7> acc) {
    p2m::CsObj o;
    o.cls = cls;
    o.v0 = 3;
    o.v1 = 1;
    o.props = std::move(props);
    o.acc = acc;
    return o;
}
p2m::CsProp cs_prop(const std::string& n, const std::string& t, p2m::CsVal v, int enc = 0) {
    p2m::CsProp p;
    p.n = n;
    p.t = t;
    p.pv = 0x1E;
    p.v = std::move(v);
    p.enc = enc;
    return p;
}

TEST(p2m_combat_table_round_trip) {
    p2m::CsDoc d;
    d.strs = {"Human.xcs", "gCCombatSpecies", "HitRange", "float", "Name", "bCString", "gCCombatStyleMelee", "Style_1H"};
    p2m::CsVal range;
    range.kind = p2m::CsVal::Float;
    range.f = 1.5;
    p2m::CsVal name;
    name.kind = p2m::CsVal::Str;
    name.s = "Style_1H";
    p2m::CsSpecies sp;
    sp.name = "Human.xcs";
    sp.ft_lo = 7;
    sp.ft_hi = 9;
    sp.ver = 2;
    sp.obj = cs_obj("gCCombatSpecies", {cs_prop("HitRange", "float", range)}, {1, 1, 4, 0, 3, 0, 1});
    sp.styles.push_back(cs_obj("gCCombatStyleMelee", {cs_prop("Name", "bCString", name, 5), cs_prop("HitRange", "float", range)}, {1, 1, 5, 0, 3, 0, 1}));
    d.species.push_back(sp);
    const p2m::Bytes b1 = p2m::write_combat(d);
    const p2m::CsDoc back = p2m::parse_combat(b1);
    CHECK(back.species.size() == 1 && back.species[0].name == "Human.xcs" && back.species[0].styles.size() == 1);
    CHECK(back.species[0].styles[0].props[0].v.s && *back.species[0].styles[0].props[0].v.s == "Style_1H");
    CHECK(p2m::write_combat(back) == b1);  // byte for byte
    CHECK(p2m::cs_equal(back.species[0].styles, d.species[0].styles));
    p2m::CsDoc other = back;
    other.species[0].styles[0].props[1].v.f = 2.0;
    CHECK(!p2m::cs_equal(other.species[0].styles, back.species[0].styles));
    bool threw = false;
    try { p2m::parse_combat(b1.substr(0, b1.size() - 3)); } catch (const p2m::Error&) { threw = true; }
    CHECK(threw);
}

// ---- text helpers and the Lua emitter -------------------------------------------------------------------------------------------------

TEST(p2m_text_helpers) {
    using namespace p2m;
    CHECK(norm_text("  'Hello'  ") == "Hello");
    CHECK(norm_text("Cole`s bow") == "Cole's bow" && norm_text("Cole\xC2\xB4s bow") == "Cole's bow");
    CHECK(norm_cell("Aric''s sword'").value() == "Aric's sword");
    CHECK(norm_cell("'Hello'").value() == "Hello");
    CHECK(norm_cell("Cole's bow").value() == "Cole's bow");
    CHECK(!norm_cell("-") && !norm_cell("") && !norm_cell("...") && !norm_cell("?") && !norm_cell("  "));
    CHECK(csv_field("Ruby Blade") == "Ruby Blade");
    CHECK(csv_field("He said \"hi\"; ok") == "\"He said \"\"hi\"\"; ok\"");
    CHECK(csv_field("x ") == "\"x \"" && csv_field("") == "\"\"" && csv_field("a#b") == "\"a#b\"");
    CHECK(norm_mesh("Item_Pearl._xmsh") == "Item_Pearl.xmsh" && norm_mesh("Item_Pearl.xmsh") == "Item_Pearl.xmsh");
    CHECK(norm_mesh("a._x") == "a._x" && norm_mesh("a._xMsh") == "a._xMsh");
    CHECK(vec_text({0, 12, -15.5}) == "0; 12; -15.5");
    CHECK(is_asset_name("It_Wpn_Axe._xmsh") && is_asset_name("A._XIMG") && !is_asset_name("a.tple") && !is_asset_name("a._x") && !is_asset_name("b.xmsh"));
    CHECK(make_mod_id("Risen_Extended_Mod") == "risen_extended_mod" && make_mod_id("My Mod!") == "my_mod" && make_mod_id("123") == "mod_123");
    CHECK(py_strip("\xC2\xA0 x \t") == "x" && py_round(0.55000001, 5) == 0.55 && fmt_g(15.5) == "15.5");
}

TEST(p2m_lua_emit_layout_matches_the_python_generator) {
    using p2m::LVal;
    CHECK(p2m::to_lua(LVal::integer(5)) == "5");
    CHECK(p2m::to_lua(LVal::real(1500.0)) == "1500" && p2m::to_lua(LVal::real(0.5)) == "0.5" && p2m::to_lua(LVal::real(-0.25)) == "-0.25");
    CHECK(p2m::to_lua(LVal::str("a\"b\\c\n")) == "\"a\\\"b\\\\c\\n\"");
    LVal l = LVal::list();
    l.push(LVal::integer(1));
    l.push(LVal::integer(2));
    CHECK(p2m::to_lua(l) == "{ 1, 2 }" && p2m::to_lua(LVal::list()) == "{}");
    LVal d = LVal::dict();
    d.set("b", LVal::integer(1));
    d.set("a", LVal::integer(2));
    CHECK(p2m::to_lua(d) == "{ a = 2, b = 1 }");  // plain dicts print sorted
    LVal o = LVal::dict(true);
    o.set("b", LVal::integer(1));
    o.set("a", LVal::integer(2));
    CHECK(p2m::to_lua(o) == "{ b = 1, a = 2 }");  // ordered dicts keep the insertion order
    CHECK(p2m::lua_key("end") == "[\"end\"]" && p2m::lua_key("a b") == "[\"a b\"]" && p2m::lua_key("ok_1") == "ok_1");
    LVal n = LVal::dict();
    LVal inner = LVal::dict();
    inner.set("x", LVal::integer(1));
    n.set("a", inner);
    CHECK(p2m::to_lua(n) == "{\n    a = { x = 1 },\n}");
    LVal nested = LVal::list();
    LVal e = LVal::dict();
    e.set("amount", LVal::integer(80));
    e.set("skill", LVal::str("Strength"));
    nested.push(e);
    CHECK(p2m::to_lua(nested) == "{\n    { amount = 80, skill = \"Strength\" },\n}");
    CHECK(p2m::to_lua(LVal::nil()) == "nil");
    LVal j = LVal::dict(true);
    j.set("n", LVal::integer(1));
    j.set("l", LVal::list());
    CHECK(p2m::to_json(j) == "{\n \"n\": 1,\n \"l\": []\n}");
}

TEST(p2m_json_reader) {
    const p2m::LVal v = p2m::parse_json("\xEF\xBB\xBF{ \"a\": [1, 2.5, \"x\\n\\u00e4\"], \"b\": { \"c\": true, \"d\": null }, \"e\": -3 }");
    CHECK(v.get("a") && v.get("a")->items.size() == 3 && v.get("a")->items[1].d == 2.5);
    CHECK(v.get("a")->items[2].s == "x\n\xC3\xA4");
    CHECK(v.get("b")->get("c")->b && v.get("b")->get("d")->kind == p2m::LVal::Nil && v.get("e")->i == -3);
    bool threw = false;
    try { p2m::parse_json("{ \"a\": }"); } catch (const p2m::Error&) { threw = true; }
    CHECK(threw);
}

// ---- end to end: synthetic game and mod archives -> converted mod ------------------------------------------------------------------------------

TEST(p2m_end_to_end_conversion) {
    TempDir t;
    // the user's game: Items/ directories in the template archive, a strings table, two materials, an image list
    t.write("game/data/common/templates.pak",
            p2m::build_pak({{"Items/Weapons/It_Sword.tple", item_tpl("It_Sword", 1, 1, 50, 15, {{7, 10}}, "It_Wpn_Sword.xmsh"), true},
                            {"Items/Weapons/It_Spear.tple", item_tpl("It_Spear", 2, 2, 90, 30, {{7, 40}, {8, 10}}, "It_Wpn_Spear.xmsh"), true},
                            {"Items/Weapons/It_Same.tple", item_tpl("It_Same", 4, 1, 10, 5, {}, "It_Wpn_Same.xmsh"), true}}));
    t.write("game/data/common/materials.pak", p2m::build_pak({{"Mat/Same._xmat", "same material", true}, {"Mat/Old._xmat", "old material", true}}));
    t.write("game/data/compiled/strings.pak",
            p2m::build_pak({{"items.tab",
                             p2m::build_tab({"German_Text", "English_Text"}, {{"FO_It_Sword", {"Schwert", "Sword"}}, {"FO_It_Spear", {"Speer", "Spear"}}, {"FO_Other", {"Anderes", "Other"}}}),
                             true}}));
    // the mod: flat archives. Sword gets better, Same is re-saved unchanged, Axe is new, one material is replaced, one is identical, a mesh is new
    t.write("mod/common/templates.p00",
            p2m::build_pak({{"It_Sword.tple", item_tpl("It_Sword", 1, 1, 75, 20, {{7, 12}}, "It_Wpn_Sword.xmsh"), true},
                            {"It_Same.tple", item_tpl("It_Same", 4, 1, 10, 5, {}, "It_Wpn_Same.xmsh"), true},
                            {"It_Axe.tple", item_tpl("It_Axe", 5, 1, 120, 33, {{7, 25}}, "It_Wpn_Axe.xmsh"), true}}));
    t.write("mod/common/materials.p00", p2m::build_pak({{"Same._xmat", "same material", false}, {"Old._xmat", "new material", false}}));
    t.write("mod/common/meshes.p00", p2m::build_pak({{"It_Wpn_Axe._xmsh", "mesh data", false}}));
    t.write("mod/compiled/strings.p00",
            p2m::build_pak({{"items.tab",
                             p2m::build_tab({"German_Text", "English_Text"},
                                            {{"FO_It_Sword", {"Langschwert", "Longsword"}},   // a changed row of a converted item
                                             {"FO_It_Axe", {"Axt", "Axe"}},                     // new
                                             {"FO_Other", {"Etwas anderes", "Something else"}}, // changed, not related to a converted item
                                             {"FO_Spear", {"-", "'Spear''s'"}}}),               // new with a placeholder and a spreadsheet artefact
                             false}}));
    t.write("mod/readme.txt", "hello");

    p2m::Options o;
    o.inputs = {(t.path / "mod").string()};
    o.baseline = (t.path / "game/data").string();
    o.out = (t.path / "out").string();
    o.id = "synthetic_mod";
    std::ostringstream log;
    p2m::Counts c;
    CHECK(p2m::run_convert(o, log, &c) == 0);
    CHECK(c.items_changed == 1 && c.new_items == 1 && c.template_patches == 0);
    CHECK(c.assets_new == 1 && c.assets_replaced == 1 && c.assets_identical == 1);

    const auto read = [&](const std::string& rel) { return p2m::read_file((t.path / "out" / rel).string()); };
    const std::string items = read("data/items.lua");
    CHECK(items.find("It_Sword = {") != std::string::npos && items.find("value = 75,") != std::string::npos && items.find("damage = 20,") != std::string::npos);
    CHECK(items.find("{ amount = 12, skill = \"Strength\" }") != std::string::npos);
    CHECK(items.find("It_Same") == std::string::npos && items.find("It_Spear") == std::string::npos);
    const std::string nw = read("data/new_items.lua");
    CHECK(nw.find("It_Axe = {") != std::string::npos && nw.find("base = \"It_Same\"") != std::string::npos);
    CHECK(nw.find("value = 120,") != std::string::npos && nw.find("damage = 33,") != std::string::npos && nw.find("mesh = \"It_Wpn_Axe.xmsh\"") != std::string::npos);
    CHECK(nw.find("{ amount = 25, skill = \"Strength\" }") != std::string::npos);
    // strings: the new rows and the changed row of a converted item, not the unrelated changed row
    const std::string en = read("strings/all.csv");
    CHECK(en.find("FO_It_Axe;Axe") != std::string::npos && en.find("FO_It_Sword;Longsword") != std::string::npos);
    CHECK(en.find("FO_Other") == std::string::npos && en.find("FO_Spear;Spear's") != std::string::npos);
    const std::string de = read("strings/German.csv");
    CHECK(de.find("FO_It_Axe;Axt") != std::string::npos && de.find("FO_Spear") == std::string::npos);
    CHECK(en.find("\r\n") != std::string::npos);
    // assets: a flat directory, only the differing files
    CHECK(fs::exists(t.path / "out/assets/Old._xmat") && fs::exists(t.path / "out/assets/It_Wpn_Axe._xmsh") && !fs::exists(t.path / "out/assets/Same._xmat"));
    CHECK(read("assets/Old._xmat") == "new material");
    // the mod files and the report
    const std::string toml = read("mod.toml");
    CHECK(toml.find("id          = \"synthetic_mod\"") != std::string::npos && toml.find("assets      = \"assets\"") != std::string::npos);
    const std::string main_lua = read("main.lua");
    CHECK(main_lua.find("synthetic_mod: ") != std::string::npos && main_lua.find("risen_extended") == std::string::npos);
    CHECK(main_lua.find("SYNTHETIC_MOD_WORLD") != std::string::npos);
    const std::string report = read("report.md");
    CHECK(report.find("Conversion report") != std::string::npos && report.find("Not converted") != std::string::npos && report.find("changed rows are not applied") != std::string::npos);
    const std::string manifest = read("manifest.json");
    CHECK(manifest.find("\"items_changed\": 1") != std::string::npos);
    CHECK(read(".gitignore") == "*\n" && fs::exists(t.path / "out/NOTICE.txt"));

    // a rerun replaces the generated folders and keeps a user edited config.lua
    t.write("out/config.lua", "return { mine = true }\n");
    std::ostringstream log2;
    CHECK(p2m::run_convert(o, log2) == 0);
    CHECK(read("config.lua") == "return { mine = true }\n");
    CHECK(read("data/items.lua") == items);  // deterministic

    // refuses to write into a folder that is not its own and into the game
    t.write("other/file.txt", "x");
    p2m::Options bad = o;
    bad.out = (t.path / "other").string();
    std::ostringstream log3;
    CHECK(p2m::run_convert(bad, log3) == 1 && log3.str().find("not generated by risen-pack2mod") != std::string::npos);
    bad.out = (t.path / "game/data/out").string();
    std::ostringstream log4;
    CHECK(p2m::run_convert(bad, log4) == 1 && log4.str().find("inside") != std::string::npos);
    CHECK(fs::exists(t.path / "other/file.txt") && !fs::exists(t.path / "game/data/out"));

    // --dry-run writes nothing, --list lists
    p2m::Options dry = o;
    dry.out = (t.path / "dry").string();
    dry.dry_run = true;
    std::ostringstream log5;
    CHECK(p2m::run_convert(dry, log5) == 0 && !fs::exists(t.path / "dry"));
    p2m::Options lst = o;
    lst.list = true;
    std::ostringstream log6;
    CHECK(p2m::run_convert(lst, log6) == 0 && log6.str().find("It_Axe.tple") != std::string::npos && log6.str().find("same\t") != std::string::npos);
}

TEST(p2m_entity_name_decides_the_template_name) {
    // the engine knows a template by the entity name inside the file (string 1), not by the file name (docs/RE.md section 48): a mod that renames files in its editor
    TempDir t;
    auto named = [](const std::string& file, const std::string& entity, int g, int gold, int damage) {
        TplBuilder b;
        b.strs = {file + ".tple", entity};
        b.set("gCItem_PS");
        b.lng("GoldValue", gold);
        b.enm("UseType", "gEItemUseType", 1);
        b.enm("Category", "gEItemCategory", 1);
        b.set("gCDamage_PS");
        b.lng("DamageAmount", damage);
        return b.build(guid_bytes(g), guid_bytes(g + 100));
    };
    t.write("game/data/common/templates.pak", p2m::build_pak({{"Items/It_Pick.tple", named("It_Pick", "It_Pick", 1, 10, 5), true}}));
    t.write("game/data/compiled/strings.pak", p2m::build_pak({{"x.tab", p2m::build_tab({"English_Text"}, {{"A", {"a"}}}), true}}));
    // the file It_Pick_Axe holds the template the engine calls It_Pick (the game's GUID); the file It_Pick holds the old data under a new name and a new GUID
    t.write("mod/common/templates.p00", p2m::build_pak({{"It_Pick_Axe.tple", named("It_Pick_Axe", "It_Pick", 1, 50, 15), false}, {"It_Pick.tple", named("It_Pick", "It_Pick_Misc", 2, 10, 0), false}}));
    p2m::Options o;
    o.inputs = {(t.path / "mod").string()};
    o.baseline = (t.path / "game/data").string();
    o.out = (t.path / "out").string();
    std::ostringstream log;
    p2m::Counts c;
    CHECK(p2m::run_convert(o, log, &c) == 0);
    CHECK(c.items_changed == 1 && c.new_items == 1);
    const std::string items = p2m::read_file((t.path / "out/data/items.lua").string());
    CHECK(items.find("It_Pick = ") != std::string::npos && items.find("value = 50") != std::string::npos && items.find("damage = 15") != std::string::npos);
    CHECK(items.find("It_Pick_Axe") == std::string::npos);
    const std::string nw = p2m::read_file((t.path / "out/data/new_items.lua").string());
    CHECK(nw.find("It_Pick_Misc = ") != std::string::npos && nw.find("base = \"It_Pick\"") != std::string::npos && nw.find("It_Pick_Axe") == std::string::npos);
    // same GUID, same engine name: no GUID reuse is reported
    CHECK(p2m::read_file((t.path / "out/report.md").string()).find("reuse the GUID") == std::string::npos);
}

TEST(p2m_layering_later_volume_wins_and_flat_names) {
    TempDir t;
    t.write("game/data/common/templates.pak", p2m::build_pak({{"Items/A/It_Sword.tple", item_tpl("It_Sword", 1, 1, 50, 15, {}, "m.xmsh"), true}}));
    t.write("game/data/compiled/strings.pak", p2m::build_pak({{"x.tab", p2m::build_tab({"English_Text"}, {{"A", {"a"}}}), false}}));
    // three volumes of one archive: .p02 is listed first on purpose; the numbers decide
    t.write("mod/common/templates.p02", p2m::build_pak({{"It_Sword.tple", item_tpl("It_Sword", 1, 1, 300, 15, {}, "m.xmsh"), false}}));
    t.write("mod/common/templates.p00", p2m::build_pak({{"sub/It_Sword.tple", item_tpl("It_Sword", 1, 1, 100, 15, {}, "m.xmsh"), false}}));
    t.write("mod/common/templates.p01", p2m::build_pak({{"It_Sword.tple", item_tpl("It_Sword", 1, 1, 200, 15, {}, "m.xmsh"), false}}));
    p2m::Options o;
    o.inputs = {(t.path / "mod/common/templates.p02").string(), (t.path / "mod/common/templates.p00").string(), (t.path / "mod/common/templates.p01").string()};
    o.baseline = (t.path / "game/data").string();
    o.out = (t.path / "out").string();
    std::ostringstream log;
    CHECK(p2m::run_convert(o, log) == 0);
    const std::string items = p2m::read_file((t.path / "out/data/items.lua").string());
    CHECK(items.find("value = 300") != std::string::npos && items.find("value = 100") == std::string::npos && items.find("value = 200") == std::string::npos);
}

TEST(p2m_command_line) {
    std::ostringstream out, err;
    const char* none[] = {"risen-pack2mod"};
    CHECK(p2m::pack2mod_main(1, none, out, err) == 1 && err.str().find("usage:") != std::string::npos);
    const char* help[] = {"risen-pack2mod", "--help"};
    std::ostringstream o2, e2;
    CHECK(p2m::pack2mod_main(2, help, o2, e2) == 0 && o2.str().find("--baseline") != std::string::npos);
    const char* bad[] = {"risen-pack2mod", "x", "--bogus"};
    std::ostringstream o3, e3;
    CHECK(p2m::pack2mod_main(3, bad, o3, e3) == 1 && e3.str().find("unknown option") != std::string::npos);
    const char* nobase[] = {"risen-pack2mod", "x", "--out", "y"};
    std::ostringstream o4, e4;
    CHECK(p2m::pack2mod_main(4, nobase, o4, e4) == 1 && o4.str().find("--baseline") != std::string::npos);
}
