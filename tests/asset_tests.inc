// ---- resource names (common/asset_names.h): compiled names, kinds, confined paths, loose-file detection ---------------------------------------
// Included by test_main.cpp inside its anonymous namespace.

TEST(asset_compiled_names) {
    using spass::assets::compiledName;
    CHECK(compiledName("It_Wpn_1H_Club.xmsh") == std::string("It_Wpn_1H_Club._xmsh"));
    CHECK(compiledName("It_Wpn_1H_Club._xmsh") == std::string("It_Wpn_1H_Club._xmsh"));
    CHECK(compiledName("IT_WPN.XMSH") == std::string("IT_WPN._xmsh"));
    CHECK(compiledName("ItWpn_SwordMisc_01_Diffuse_01.tga") == std::string("ItWpn_SwordMisc_01_Diffuse_01._ximg"));
    CHECK(compiledName("Icon.dds") == std::string("Icon._ximg"));
    CHECK(compiledName("Icon.ximg") == std::string("Icon._ximg"));
    CHECK(compiledName("Mat.xmat") == std::string("Mat._xmat"));
    CHECK(compiledName("Ani_Wolf_Monster_Wolf.xac") == std::string("Ani_Wolf_Monster_Wolf._xmac"));
    CHECK(compiledName("a.b.c.xmsh") == std::string("a.b.c._xmsh"));
    CHECK(!compiledName("").has_value());
    CHECK(!compiledName("noext").has_value());
    CHECK(!compiledName("x.txt").has_value());
    CHECK(!compiledName("dir/x.xmsh").has_value());  // a resource name is a bare file name
    CHECK(!compiledName("dir\\x.xmsh").has_value());
    CHECK(!compiledName("x.").has_value());
}

TEST(asset_kinds_and_loose_paths) {
    using namespace spass::assets;
    CHECK(kindOf("a.xmsh") == Kind::Mesh && kindOf("a._xmsh") == Kind::Mesh);
    CHECK(kindOf("a.tga") == Kind::Image && kindOf("a._ximg") == Kind::Image);
    CHECK(kindOf("a.xac") == Kind::Actor && kindOf("a.lua") == Kind::Unknown);
    CHECK(std::string(kindName(Kind::Material)) == "material");
    CHECK(isLoosePhysicalPath("B:\\risen\\bin\\spass\\mods\\x\\assets\\a._xmsh"));
    CHECK(!isLoosePhysicalPath("B:\\risen\\bin\\..\\data\\common\\meshes.pak\\Items_01\\a._xmsh"));
    CHECK(!isLoosePhysicalPath("B:/risen/data/common/meshes.pak/Items_01/a._xmsh"));
    CHECK(!isLoosePhysicalPath(""));
}

TEST(asset_clean_relative_path) {
    using spass::assets::cleanRelativePath;
    CHECK(cleanRelativePath("a._xmsh") == std::string("a._xmsh"));
    CHECK(cleanRelativePath("meshes\\sub/a._xmsh") == std::string("meshes/sub/a._xmsh"));
    CHECK(!cleanRelativePath("").has_value());
    CHECK(!cleanRelativePath("../a").has_value());
    CHECK(!cleanRelativePath("a/../b").has_value());
    CHECK(!cleanRelativePath("/abs").has_value());
    CHECK(!cleanRelativePath("\\\\server\\share").has_value());
    CHECK(!cleanRelativePath("C:/x").has_value());
    CHECK(!cleanRelativePath("a//b").has_value());
    CHECK(!cleanRelativePath("a/").has_value());
    CHECK(!cleanRelativePath("a/./b").has_value());
    CHECK(!cleanRelativePath("a/b.").has_value());
    CHECK(!cleanRelativePath("con?").has_value());
    CHECK(!cleanRelativePath(std::string(300, 'a')).has_value());
}
