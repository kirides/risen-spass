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

TEST(game_data_lists_the_dialogue_infos) {
    auto g = game_data();
    if (!g) {
        std::printf("  (skipped: no --data)\n");
        return;
    }
    const auto& infos = g->infos();
    std::printf("  %zu dialogue infos\n", infos.size());
    CHECK(infos.size() > 4000);
    const mk::GameInfo* patty = nullptr;
    for (auto& i : infos)
        if (i.name == "PANKRATZX2_00610") patty = &i;
    CHECK(patty != nullptr);
    if (patty) {
        CHECK_EQ(patty->owner, std::string("Patty"));
        CHECK_EQ(patty->quest, std::string("Patty_FollowToGregsCasketChest"));
    }
    // sorted by name, the first line of a spoken info is in infos.tab
    for (size_t i = 1; i < infos.size(); ++i)
        if (p2m::lower(infos[i - 1].name) > p2m::lower(infos[i].name)) {
            CHECK(false);
            break;
        }
    size_t with_text = 0;
    for (auto& i : infos) with_text += !g->info_text(i.name).empty();
    std::printf("  %zu of them have a line in infos.tab\n", with_text);
    CHECK(with_text > 4000);
    // commands and conditions with their values: the sector command of an info is the first command of the Druid info
    const mk::GameInfo* druid = g->info("filLERx2_00941");  // case-insensitive
    CHECK(druid != nullptr);
    if (druid) {
        bool sector = false;
        for (auto& c : druid->commands)
            if (c.cls == "SetSectorStatus")
                for (auto& pv : c.props) sector |= pv.first == "SectorName" && pv.second == "Druid_Outdoor_Story_LizardHunter_Off";
        CHECK(sector);
        CHECK(!druid->props.empty() && druid->props[0].first == "Name");
    }
    CHECK(g->command_classes().count("Say") == 1 && g->command_classes().count("GiveXP") == 1);
    CHECK(g->condition_classes().count("PlayerKnows") == 1);
    const auto& say = g->command_classes().at("Say");
    CHECK(std::find(say.begin(), say.end(), "Speaker") != say.end());
}
