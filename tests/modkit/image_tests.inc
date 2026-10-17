// Pictures -> compiled images and icons (core/image.h): the ._ximg writer against the editor's own reader and (with --data) against a shipped
// file's header, the icon atlas, and pictures through export and validation.

// an uncompressed 32-bit TGA (top-left origin) of a w x h gradient; `alpha` < 255 makes it transparent
static std::string test_tga(int w, int h, int alpha = 255) {
    std::string t(18, '\0');
    t[2] = 2;  // true colour
    t[12] = static_cast<char>(w & 0xff);
    t[13] = static_cast<char>(w >> 8);
    t[14] = static_cast<char>(h & 0xff);
    t[15] = static_cast<char>(h >> 8);
    t[16] = 32;
    t[17] = 0x28;  // 8 alpha bits, top-left
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            t.push_back(static_cast<char>(x * 255 / std::max(1, w - 1)));  // B
            t.push_back(static_cast<char>(y * 255 / std::max(1, h - 1)));  // G
            t.push_back(static_cast<char>(200));                          // R
            t.push_back(static_cast<char>(alpha));
        }
    return t;
}

TEST(picture_to_ximg) {
    std::string why;
    auto pic = load_picture(test_tga(100, 60), why);
    CHECK(pic.has_value());
    if (!pic) return;
    CHECK_EQ(pic->width, 100);
    CHECK(!pic->has_alpha());
    CHECK_EQ(int(pic->rgba[0]), 200);  // R of the first pixel
    // a texture: powers of two, DXT1 without alpha, the whole mip chain; the editor's reader takes it
    const std::string tex = build_ximg(*pic, XimgKind::Texture, ".tga");
    CHECK(tex.compare(0, 8, "GR01IM04") == 0);
    const ImageData d = decode_image(tex);
    CHECK_EQ(d.width, 128u);
    CHECK_EQ(d.height, 64u);
    CHECK(d.format == ImageData::DXT1);
    CHECK_EQ(d.levels.size(), size_t(8));
    // with alpha: DXT5
    auto see_through = load_picture(test_tga(64, 64, 128), why);
    CHECK(see_through && see_through->has_alpha());
    CHECK(decode_image(build_ximg(*see_through, XimgKind::Texture)).format == ImageData::DXT5);
    // a GUI image: A8R8G8B8 as it is, B G R A in memory
    const ImageData g = decode_image(build_ximg(*pic, XimgKind::Gui));
    CHECK_EQ(g.width, 100u);
    CHECK(g.format == ImageData::BGRA8 || g.format == ImageData::BGRX8);
    CHECK(g.data.size() >= 4 && uint8_t(g.data[2]) == 200 && uint8_t(g.data[3]) == 255);
    // the same picture gives the same file (fixed file time)
    CHECK(build_ximg(*pic, XimgKind::Texture, ".tga") == tex);
    CHECK(!load_picture("no picture", why).has_value());
    CHECK(is_picture("A.PNG") && is_picture("b.jpeg") && !is_picture("c._ximg"));
    CHECK_EQ(asset_kind("knife.png"), std::string("picture"));
    CHECK_EQ(asset_kind("Mod_Icons.ximl"), std::string("iconlist"));
    CHECK_EQ(project_icon_name("my_mod", "pics/Knife.png"), std::string("IC_my_mod_Knife"));
    CHECK_EQ(picture_compiled_name("ItWpn_SwordMisc_01_Diffuse_01.png"), std::string("ItWpn_SwordMisc_01_Diffuse_01._ximg"));
}

TEST(icon_atlas) {
    std::string why;
    std::vector<std::pair<std::string, Picture>> icons;
    for (int i = 0; i < 3; ++i) icons.push_back({"IC_T_" + std::to_string(i), *load_picture(test_tga(64 + i, 64), why)});
    const IconAtlas a = build_icon_atlas(icons, "T_IconAtlas.dds");
    CHECK_EQ(a.texture.width, 128);  // 3 cells of 40 need 128 x 128
    CHECK_EQ(a.cells.size(), size_t(3));
    CHECK((a.cells[1].second == std::array<int, 4>{40, 0, 80, 40}));
    CHECK((a.cells[2].second == std::array<int, 4>{80, 0, 120, 40}));
    CHECK(a.ximl.find("TextureName=\"T_IconAtlas.dds\"") != std::string::npos);
    CHECK(a.ximl.find("ImageName=\"IC_T_2\"") != std::string::npos);
    CHECK(a.ximl.find("RectOnTexture=\"40; 0; 80; 40\"") != std::string::npos);
    // the pixels of a cell are the scaled picture (R = 200 everywhere it was drawn)
    CHECK_EQ(int(a.texture.rgba[(static_cast<size_t>(10) * 128 + 50) * 4]), 200);
    CHECK_EQ(int(a.texture.rgba[(static_cast<size_t>(100) * 128 + 100) * 4 + 3]), 0);  // outside the cells: transparent
}

TEST(pictures_export) {
    const fs::path root = temp_dir("pictures");
    write_text(root / "knife.tga", test_tga(48, 48));
    write_text(root / "Hob_Cloth_Diffuse_01.tga", test_tga(256, 256));
    Project p;
    p.mod.id = "pic_test";
    p.mod.languages = {"English"};
    p.assets.push_back({"a1", "knife.tga", "picture"});
    p.assets.push_back({"a2", "Hob_Cloth_Diffuse_01.tga", "picture"});
    ItemDef it;
    it.uid = "i1";
    it.mode = Mode::New;
    it.name = "PIC_Knife";
    it.base = "It_1H_Machete";
    it.icon = "knife.tga";
    p.items.push_back(it);
    CHECK_EQ(icon_pictures(p), std::vector<std::string>{"knife.tga"});
    CHECK_EQ(icon_lists(p), std::vector<std::string>{"pic_test_Icons"});
    CHECK_EQ(balance_spec(p)["new_items"]["PIC_Knife"]["icon"].get<std::string>(), std::string("IC_pic_test_knife"));
    ExportOptions o;
    o.project_dir = u8str(root);
    const fs::path out = root / "mod";
    const ExportResult r = export_mod(p, u8str(out), o);
    for (auto& e : r.errors) std::printf("  export: %s\n", e.c_str());
    CHECK(r.ok);
    CHECK(fs::exists(out / "assets" / "Hob_Cloth_Diffuse_01._ximg"));
    CHECK(!fs::exists(out / "assets" / "knife._ximg") && !fs::exists(out / "assets" / "knife.tga"));
    CHECK(fs::exists(out / "assets" / "pic_test_IconAtlas._ximg"));
    const std::string ximl = read_text(out / "assets" / "pic_test_Icons.ximl");
    CHECK(ximl.find("ImageName=\"IC_pic_test_knife\"") != std::string::npos && ximl.find("pic_test_IconAtlas.dds") != std::string::npos);
    CHECK(decode_image(read_text(out / "assets" / "Hob_Cloth_Diffuse_01._ximg")).levels.size() == 9);
    CHECK(read_text(out / "mod.toml").find("assets      = \"assets\"") != std::string::npos);
    const json assets = lua_data_to_json(read_text(out / "data" / "assets.lua"), "assets.lua");
    CHECK_EQ(assets["icons"][0].get<std::string>(), std::string("pic_test_Icons"));
    CHECK(read_text(out / "main.lua").find("require(\"data.assets\")") != std::string::npos);
    // validation: an icon picture that is no asset, two pictures that become the same texture, a project file named like a generated one
    Project q = p;
    q.items[0].icon = "missing.png";
    q.assets.push_back({"a3", "other/Hob_Cloth_Diffuse_01.png", "picture"});
    const auto probs = validate(q, nullptr);
    auto has = [&](const std::string& part) {
        return std::any_of(probs.begin(), probs.end(), [&](const Problem& pr) { return pr.message.find(part) != std::string::npos; });
    };
    CHECK(has("icon picture 'missing.png'"));
    CHECK(has("becomes Hob_Cloth_Diffuse_01._ximg like"));
    Project v = p;
    v.assets.push_back({"a4", "pic_test_Icons.ximl", "iconlist"});
    const auto probs2 = validate(v, nullptr);
    CHECK(std::any_of(probs2.begin(), probs2.end(), [](const Problem& pr) { return pr.message.find("makes this file") != std::string::npos; }));
    CHECK(error_count(validate(p, nullptr)) == 0);
}

TEST(ximg_header_matches_game) {
    auto g = game_data();
    if (!g) {
        std::printf("  (skipped: no --data)\n");
        return;
    }
    // the property stream of a written GUI image is the shipped icon atlas's, apart from the size and the object's trailing data
    const std::string shipped = g->read("GUI_IconSets01._ximg");
    std::string why;
    const std::string mine = build_ximg(*load_picture(test_tga(1024, 1024), why), XimgKind::Gui);
    CHECK(shipped.size() > 0x109 && mine.size() > 0x109);
    CHECK(shipped.compare(0, 8, mine, 0, 8) == 0);
    CHECK(shipped.compare(8, 16, mine, 8, 16) == 0);       // header sizes and offsets (payload size included: same size, same format)
    CHECK(shipped.compare(0x28, 0xc1, mine, 0x28, 0xc1) == 0);  // properties up to the trailing data
    // the DDS headers agree
    CHECK(shipped.compare(0x109, 128, mine, 0x109, 128) == 0);
    // and a shipped DXT texture's properties up to the format, with its own size and mip skip
    const std::string tex = g->read("ItWpn_SwordMisc_01_Diffuse_01._ximg");
    const std::string mine_tex = build_ximg(*load_picture(test_tga(1024, 1024), why), XimgKind::Texture, ".tga");
    CHECK(tex.compare(0x28, 0x6c - 0x0, mine_tex, 0x28, 0x6c) == 0);  // up to the SkipMips value (the shipped one skips 2)
    CHECK(tex.compare(0x28 + 0x70, 0xc1 - 0x70, mine_tex, 0x28 + 0x70, 0xc1 - 0x70) == 0);
}
