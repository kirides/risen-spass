// Tests of the sector start state and its triggers (core/sectors.h, docs/RE.md section 55). Included by modkit_tests.cpp.

#include "core/sectors.h"

TEST(world_sectors_start_state_and_triggers) {
    auto g = game_data();
    if (!g) {
        std::printf("  (skipped: no --data)\n");
        return;
    }
    auto w = load_world(*g, "World");
    CHECK(w != nullptr);
    if (!w) return;
    size_t off = 0, off_with_layer = 0, off_with_trigger = 0, layers_linked = 0;
    std::string untriggered;
    for (auto& s : w->sectors) {
        if (s.enabled_on_startup) continue;
        ++off;
        bool has_layer = false;
        for (auto& L : w->layers)
            if (L.sector >= 0 && w->sectors[static_cast<size_t>(L.sector)].name == s.name) has_layer = true;
        off_with_layer += has_layer;
        bool turns_on = false;
        for (auto& t : s.triggers) turns_on |= t.enable;
        off_with_trigger += turns_on;
        if (!turns_on) untriggered += " " + s.name;
    }
    for (auto& L : w->layers) layers_linked += L.sector >= 0;
    std::printf("  %zu sectors, %zu start switched off (%zu with a layer, %zu with a trigger that switches them on), %zu of %zu layers linked to a sector;\n  never switched on:%s\n",
                w->sectors.size(), off, off_with_layer, off_with_trigger, layers_linked, w->layers.size(), untriggered.c_str());
    CHECK_EQ(off, size_t(21));
    CHECK_EQ(off_with_layer, size_t(21));
    CHECK_EQ(off_with_trigger, size_t(20));  // OutMain_Demo is the only one nothing switches on
    CHECK(untriggered.find("OutMain_Demo") != std::string::npos);
    // a chapter sector names its chapter script, a dialogue-driven one its info
    bool chapter = false, dialogue = false;
    for (auto& s : w->sectors) {
        if (s.name == "OutMain_Story_Chapter3_Lizards_Off")
            for (auto& t : s.triggers) chapter |= t.what == "OnQuestSuccess_ChangeToChapter3";
        if (s.name == "OutMain_Story_Chapter2_MonsterForFollowPatty_Off")
            for (auto& t : s.triggers) dialogue |= t.kind == mk::SectorTrigger::Dialogue && t.what == "PANKRATZX2_00610";
    }
    CHECK(chapter);
    CHECK(dialogue);
}
