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
