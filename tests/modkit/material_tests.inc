// Tests of the material codec (core/material.h, docs/RE.md section 60). Included by modkit_tests.cpp.

#include "core/material.h"

namespace {
bool material_refused(const p2m::Bytes& b) {
    try {
        mk::Material::parse(b);
    } catch (const std::exception&) {
        return true;
    }
    return false;
}
}  // namespace

TEST(material_reads_and_writes_every_shipped_material) {
    auto g = game_data();
    if (!g) {
        std::printf("  (skipped: no --data)\n");
        return;
    }
    size_t n = 0, bad = 0, textured = 0;
    std::string failures;
    for (const auto& a : g->assets("_xmat")) {
        ++n;
        try {
            const p2m::Bytes raw = g->read(a.name);
            const mk::Material m = mk::Material::parse(raw);
            if (m.write() != raw) throw p2m::Error("written bytes differ");
            if (!m.sampler_images().empty()) ++textured;
        } catch (const std::exception& e) {
            if (++bad < 8) failures += "\n    " + a.name + ": " + e.what();
        }
    }
    std::printf("  %zu materials, %zu with samplers\n", n, textured);
    CHECK(n > 400);
    if (bad) std::printf("  failures: %zu%s\n", bad, failures.c_str());
    CHECK(bad == 0);
}

TEST(material_edit_changes_a_name_to_any_length) {
    auto g = game_data();
    if (!g) {
        std::printf("  (skipped: no --data)\n");
        return;
    }
    const p2m::Bytes raw = g->read("ItWpn_SwordMisc_01_Diffuse_01._xmat");
    mk::Material m = mk::Material::parse(raw);
    auto samplers = m.of_class("eCColorSrcSampler");
    CHECK(samplers.size() == 2);
    std::string old;
    CHECK(samplers[1]->get_string("ImageFilePath", &old) && old == "ItWpn_SwordMisc_01_Normal_01.tga");
    CHECK(samplers[1]->set_string("ImageFilePath", "my_mod/a_much_longer_normal_map_name.tga"));
    auto shader = m.of_class("eCShaderDefault")[0];
    CHECK(shader->get_bool("EnableSpecular"));
    CHECK(shader->set_bool("EnableSpecular", false));
    const p2m::Bytes out = m.write();
    CHECK(out.size() == raw.size() + std::string("my_mod/a_much_longer_normal_map_name.tga").size() - old.size());
    const mk::Material again = mk::Material::parse(out);  // every size field is consistent again
    CHECK(again.sampler_images().size() == 2 && again.sampler_images()[1] == "my_mod/a_much_longer_normal_map_name.tga");
    CHECK(again.sampler_images()[0] == "ItWpn_SwordMisc_01_Diffuse_01.tga");
    CHECK(!again.objects[1].get_bool("EnableSpecular"));
    CHECK(again.objects.size() == m.objects.size());
    // the tails are untouched
    for (size_t i = 0; i < m.objects.size(); ++i) CHECK(again.objects[i].tail == m.objects[i].tail);
    // damaged input is refused, not half read
    CHECK(material_refused(raw.substr(0, raw.size() - 40)));
    CHECK(material_refused("GR01IM04"));
}

TEST(material_slots_link_the_shader_to_its_textures) {
    auto g = game_data();
    if (!g) {
        std::printf("  (skipped: no --data)\n");
        return;
    }
    const mk::Material m = mk::Material::parse(g->read("ItWpn_SwordMisc_01_Diffuse_01._xmat"));
    const auto slots = m.slots();
    CHECK(slots.size() == 8);
    CHECK(slots[0].name == "Diffuse" && slots[0].linked && m.slot_image(slots[0]) == "ItWpn_SwordMisc_01_Diffuse_01.tga");
    CHECK(slots[5].name == "Normal" && slots[5].linked && m.slot_image(slots[5]) == "ItWpn_SwordMisc_01_Normal_01.tga");
    CHECK(slots[3].linked && slots[3].node >= 0 && m.objects[static_cast<size_t>(slots[3].node)].cls == "eCColorSrcCombiner");
    CHECK(slots[4].linked && slots[4].component == 5);  // the constant's alpha
    CHECK(!slots[1].linked && !slots[2].linked && !slots[6].linked && !slots[7].linked);
    // every linked slot of every material finds its node
    size_t linked = 0, lost = 0;
    for (const auto& a : g->assets("_xmat")) {
        const mk::Material x = mk::Material::parse(g->read(a.name));
        for (const auto& s : x.slots())
            if (s.linked) {
                ++linked;
                if (s.node < 0) ++lost;
            }
    }
    std::printf("  %zu linked slots, %zu without a node\n", linked, lost);
    CHECK(linked > 400 && lost == 0);
}

TEST(material_edit_is_saved_validated_and_exported) {
    auto g = game_data();
    if (!g) {
        std::printf("  (skipped: no --data)\n");
        return;
    }
    Project p = load_showcase();
    mk::MaterialEdit e;
    e.uid = new_uid();
    e.material = "ItWpn_SwordMisc_01_Diffuse_01._xmat";
    e.slots["Normal"] = "my_normal_map_with_a_longer_name.tga";
    e.props["eCShaderDefault.EnableSpecular"] = 0;
    p.materials.push_back(e);
    // the project file keeps it
    const Project q = project_from_json(to_json(p));
    CHECK(q.materials.size() == 1 && q.materials[0].slots.at("Normal") == "my_normal_map_with_a_longer_name.tga" && q.materials[0].props.at("eCShaderDefault.EnableSpecular") == 0);
    // validation: fine as it is, an error for a slot without a sampler and for a material the game does not have
    auto errors = [&](const Project& x) {
        size_t n = 0;
        for (auto& pr : validate(x, g.get()))
            if (pr.area == "Materials" && pr.severity == Severity::Error) ++n;
        return n;
    };
    CHECK(errors(q) == 0);
    Project bad = q;
    bad.materials[0].slots["Bogus"] = "x.tga";
    CHECK(errors(bad) == 1);
    bad = q;
    bad.materials[0].material = "No_Such_Material._xmat";
    CHECK(errors(bad) == 1);
    // export: the game's material rewritten under its own name
    const fs::path out = temp_dir("matexport") / "mods" / "m";
    ExportOptions o;
    o.project_dir = showcase_dir();
    o.game = g.get();
    const ExportResult r = export_mod(q, u8str(out), o);
    CHECK(r.ok);
    const fs::path file = out / "assets" / "ItWpn_SwordMisc_01_Diffuse_01._xmat";
    CHECK(fs::exists(file));
    if (fs::exists(file)) {
        const mk::Material m = mk::Material::parse(read_text(file));
        CHECK(m.sampler_images().size() == 2 && m.sampler_images()[1] == "my_normal_map_with_a_longer_name.tga");
        CHECK(!m.objects[1].get_bool("EnableSpecular", true));
    }
    CHECK(read_text(out / "mod.toml").find("assets      = \"assets\"") != std::string::npos);
}

TEST(material_edit_adds_a_texture_to_an_empty_slot) {
    auto g = game_data();
    if (!g) {
        std::printf("  (skipped: no --data)\n");
        return;
    }
    const p2m::Bytes raw = g->read("ItWpn_SwordMisc_01_Diffuse_01._xmat");
    mk::Material m = mk::Material::parse(raw);
    CHECK(!m.slots()[1].linked && !m.slots()[2].linked);  // Opacity, SelfIllumination are empty
    mk::MaterialEdit e;
    e.material = "ItWpn_SwordMisc_01_Diffuse_01._xmat";
    e.slots["Opacity"] = "my_orm_map.tga";
    e.slots["SelfIllumination"] = "my_orm_map.tga";
    e.channels["Opacity"] = 3;           // .g
    e.channels["SelfIllumination"] = 4;  // .b
    e.channels["Specular"] = 2;          // the existing slot reads .r now
    std::string why;
    CHECK(mk::apply_material_edit(m, e, why));
    const p2m::Bytes out = m.write();
    const mk::Material again = mk::Material::parse(out);  // sizes, node count and covers are consistent
    CHECK(again.objects.size() == m.objects.size() && again.objects.size() == 8);
    const auto s = again.slots();
    CHECK(s[1].linked && s[1].node >= 0 && again.slot_image(s[1]) == "my_orm_map.tga" && s[1].component == 3);
    CHECK(s[2].linked && again.slot_image(s[2]) == "my_orm_map.tga" && s[2].component == 4);
    CHECK(s[3].component == 2);
    CHECK(again.slot_image(s[0]) == "ItWpn_SwordMisc_01_Diffuse_01.tga" && again.slot_image(s[5]) == "ItWpn_SwordMisc_01_Normal_01.tga");
    CHECK(again.write() == out);  // stable
    CHECK(again.guid_of(6) != again.guid_of(7));
    // an unknown slot or a channel of an empty slot is refused
    mk::Material n = mk::Material::parse(raw);
    mk::MaterialEdit bad;
    bad.material = e.material;
    bad.channels["Opacity"] = 3;
    CHECK(!mk::apply_material_edit(n, bad, why));
    bad.channels.clear();
    bad.slots["Nope"] = "x.tga";
    CHECK(!mk::apply_material_edit(n, bad, why));
}
