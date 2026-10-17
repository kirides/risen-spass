// Tests of compiled_imagelists.bin (core/imagelist.h). Included by modkit_tests.cpp.

TEST(imagelist_rejects_garbage) {
    auto throws = [](const std::string& s) {
        try {
            ImageCatalog::parse(s);
        } catch (const p2m::Error&) {
            return true;
        }
        return false;
    };
    CHECK(throws(""));
    CHECK(throws("GENOMFLE"));
    CHECK(throws(std::string("not an archive at all, but long enough to have a header....", 60)));
    std::string almost = "GENOMFLE";
    almost.append(std::string(12, '\0'));
    CHECK(throws(almost));  // no string table
}

TEST(imagelist_matches_game) {
    auto g = game_data();
    if (!g) {
        std::printf("  (skipped: no --data)\n");
        return;
    }
    const ImageCatalog& c = g->images();
    CHECK_EQ(c.collections(), size_t(7));
    CHECK(c.all().size() >= 768);
    const ImageEntry* apple = c.find("ic_it_apple");  // case-insensitive, like the engine
    CHECK(apple != nullptr);
    if (apple) {
        CHECK_EQ(apple->collection, std::string("IMC_Icons"));
        CHECK_EQ(apple->texture_file(), std::string("GUI_IconSets01._ximg"));
        CHECK(apple->width() == 40 && apple->height() == 40);
        CHECK(g->exists(apple->texture_file()));
    }
    // every shipped item's icon resolves, and into a texture the archives hold
    std::string missing;
    size_t items = 0, with_icon = 0, found = 0, texture_ok = 0;
    for (auto& t : g->templates()) {
        if (t->kind != TplKind::Item) continue;
        ++items;
        if (t->icon.empty()) continue;
        ++with_icon;
        if (const ImageEntry* e = c.find(t->icon)) {
            ++found;
            if (g->exists(e->texture_file())) ++texture_ok;
        } else if (missing.size() < 400) {
            missing += " " + t->icon;
        }
    }
    std::printf("  item icons: %zu items, %zu name an icon, %zu resolve, %zu with a texture\n", items, with_icon, found, texture_ok);
    if (!missing.empty()) std::printf("  not resolved:%s\n", missing.c_str());
    CHECK(with_icon > 100);
    CHECK(found * 100 >= with_icon * 95);  // the few that do not resolve are the engine's own gaps
    CHECK_EQ(found, texture_ok);
}
