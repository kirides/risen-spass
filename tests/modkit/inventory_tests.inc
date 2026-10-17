// Tests of the NPC templates' starting inventory (core/tpl_inventory.h). Included by modkit_tests.cpp.

// the engine's own answer: risen.npc.template_inventory of every NPC / monster template that carries something (mods/npcdump, "LOOT" lines, 2026-10-17)
static const struct { const char* npc; const char* item; int amount; } kEngineLoot[] = {
    {"Beach_Officer_Dead", "It_Grog", 1},
    {"Beach_Officer_Dead", "It_Gold", 3},
    {"Beach_Officer_Dead", "It_Ring_Gold", 1},
    {"Beach_Seaman_Dead", "It_Apple", 1},
    {"Beach_Seaman_Dead", "It_Flute", 1},
    {"Costa", "It_Purse_Costa", 1},
    {"Inq_Guard_Dead", "It_Shell", 1},
    {"Inq_Guard_Dead", "It_Meat_Raw", 2},
    {"Inq_Guard_Dead", "It_Gold", 3},
    {"Inquisitor", "It_Staff_TwoBladed", 1},
    {"Jasmin", "It_1H_Rapier", 1},
    {"Skeleton_SitDead_Berengir", "It_Key_Berengir", 1},
    {"Skeleton_SitDead_Berengir", "It_Gold", 43},
    {"Skeleton_SitDead_Berengir", "It_Scr_MinorHeal", 1},
    {"Skeleton_SitDead_Berengir", "It_Bone", 2},
    {"ch", "It_1H_Sword", 1},
    {"mh", "It_1H_Sword", 1},
    {"sh", "It_BS_TitanSword", 1},
    {"sh", "It_Recipe_Health_01", 1},
    {"sh", "It_Recipe_Mana_01", 1},
    {"sh", "It_EmptyFlask", 10},
    {"sh", "It_Sapphire", 1},
    {"Gnome", "It_1H_Sickle", 1},
    {"Gnome_Chief", "It_1H_Sickle", 1},
    {"Gnome_Chief_WithFeather", "It_1H_Sickle", 1},
    {"Gnome_Gamestart", "It_1H_Club", 1},
    {"Gnome_WithFeather", "It_1H_Sickle", 1},
    {"LizardPriest", "It_2H_Obsidian_Lizard", 1},
    {"LizardPriest_DruidDungeon", "It_2H_Obsidian_Lizard", 1},
    {"LizardPriest_Elite", "It_2H_Obsidian_Lizard", 1},
    {"LizardPriest_Oldmine", "It_2H_Obsidian_Lizard", 1},
    {"LizardPriest_Portal", "It_2H_Obsidian_Lizard", 1},
    {"LizardPriest_Portal_01", "It_2H_Obsidian_Lizard", 1},
    {"LizardPriest_Portal_02", "It_2H_Obsidian_Lizard", 1},
    {"LizardPriest_Undead", "It_2H_Obsidian_Lizard", 1},
    {"LizardPriest_Undead_StoryUnikat", "It_2H_Obsidian_Lizard", 1},
    {"LizardScout", "It_2H_Obsidian_Lizard", 1},
    {"LizardScout_DeadHunter", "It_2H_Obsidian_Lizard", 1},
    {"LizardScout_Hunter", "It_2H_Obsidian_Lizard", 1},
    {"LizardScout_HunterJasmin", "It_2H_Obsidian_Lizard", 1},
    {"LizardScout_Monastery", "It_2H_Obsidian_Lizard", 1},
    {"LizardScout_MonasterySchool", "It_2H_Obsidian_Lizard", 1},
    {"LizardScout_Monastery_Library", "It_2H_Obsidian_Lizard", 1},
    {"LizardScout_Portal", "It_2H_Obsidian_Lizard", 1},
    {"LizardScout_Portal_01", "It_2H_Obsidian_Lizard", 1},
    {"LizardScout_Portal_02", "It_2H_Obsidian_Lizard", 1},
    {"LizardScout_Portal_03", "It_2H_Obsidian_Lizard", 1},
    {"LizardScout_Portal_04", "It_2H_Obsidian_Lizard", 1},
    {"LizardScout_Spy", "It_2H_Obsidian_Lizard", 1},
    {"LizardWarrior", "It_2H_Obsidian_Lizard", 1},
    {"LizardWarrior_Crypt", "It_2H_Obsidian_Lizard", 1},
    {"LizardWarrior_Elite", "It_2H_Obsidian_Lizard", 1},
    {"LizardWarrior_EliteBoss_01", "It_2H_Obsidian_Lizard", 1},
    {"LizardWarrior_EliteBoss_02", "It_2H_Obsidian_Lizard", 1},
    {"LizardWarrior_EliteBoss_03", "It_2H_Obsidian_Lizard", 1},
    {"LizardWarrior_EliteBoss_04", "It_2H_Obsidian_Lizard", 1},
    {"LizardWarrior_HunterBoss", "It_2H_Obsidian_Lizard", 1},
    {"LizardWarrior_Keeper", "It_2H_Obsidian_Lizard", 1},
    {"LizardWarrior_KeyMaster", "It_2H_Obsidian_Lizard", 1},
    {"LizardWarrior_Portal", "It_2H_Obsidian_Lizard", 1},
    {"LizardWarrior_Portal_01", "It_2H_Obsidian_Lizard", 1},
    {"LizardWarrior_Portal_02", "It_2H_Obsidian_Lizard", 1},
    {"LizardWarrior_Portal_03", "It_2H_Obsidian_Lizard", 1},
    {"LizardWarrior_Portal_04", "It_2H_Obsidian_Lizard", 1},
    {"LizardWarrior_Portal_05", "It_2H_Obsidian_Lizard", 1},
    {"LizardWarrior_Portal_06", "It_2H_Obsidian_Lizard", 1},
    {"LizardWarrior_Severin", "It_2H_Obsidian_Lizard", 1},
    {"LizardWarrior_TitanMainGate", "It_2H_Obsidian_Lizard", 1},
    {"LizardWarrior_Undead", "It_2H_Obsidian_Lizard", 1},
    {"LizardWarrior_Undead_StoryUnikat", "It_2H_Obsidian_Lizard", 1},
    {"LizardWarrior_VulcanLeft", "It_2H_Obsidian_Lizard", 1},
    {"Skeleton", "It_1H_Sword_Rusty", 1},
    {"Skeleton_BDDon_Boss", "It_Bone", 6},
    {"Skeleton_BDDon_Boss", "It_Goldplate", 2},
    {"Skeleton_BDDon_Boss", "It_Gold", 78},
    {"Skeleton_BDDon_Boss", "It_Axe_TitanHammer", 1},
    {"Skeleton_Nude", "It_1H_Sword_Rusty", 1},
    {"Skeleton_Patroscon", "It_2H_TwoHandedSword_Rusty", 1},
    {"Skeleton_PatrosconsGuy", "It_2H_TwoHandedSword_Rusty", 1},
    {"SummonedSkeleton", "It_1H_Sword_Rusty", 1},
    {"SwampMummy", "It_Staff_Branch", 1},
    {"SwampMummy_Don_01", "It_Staff_Branch", 1},
};

TEST(inventory_decode_rejects_garbage) {
    std::vector<std::string> strings = {"x", "gCInventory_PS"};
    CHECK(!decode_inventory("", strings).found);
    CHECK(!decode_inventory("GENOMFLE-no-inventory-here", {"x"}).found);
    // the header pattern followed by nothing sensible
    std::string pat("\x01\x00\x01\x01\x00\x01\x01\x00", 8);
    CHECK(!decode_inventory(pat + std::string(20, '\xff'), strings).found);
    CHECK(!decode_inventory(pat, strings).found);
}

TEST(inventory_matches_the_engine) {
    auto g = game_data();
    if (!g) {
        std::printf("  (skipped: no --data)\n");
        return;
    }
    std::map<std::string, std::vector<std::pair<std::string, int>>> truth;
    for (auto& r : kEngineLoot) truth[r.npc].push_back({r.item, r.amount});
    size_t checked = 0, unknown = 0, unresolved = 0, worn = 0;
    std::string diffs;
    for (auto& t : g->templates()) {
        if ((t->kind != TplKind::Npc && t->kind != TplKind::Monster) || t->group.rfind("NPC/Test", 0) == 0 || t->group.rfind("NPC/_HumanParts", 0) == 0) continue;
        if (!t->inventory_known) {
            ++unknown;
            continue;
        }
        ++checked;
        std::vector<std::pair<std::string, int>> got;
        for (auto& s : t->inventory) {
            if (s.resolved) got.push_back({s.item, s.amount});
            else ++unresolved;
        }
        worn += t->worn.size();
        auto want = truth[t->name];
        std::sort(got.begin(), got.end());
        std::sort(want.begin(), want.end());
        if (got != want && diffs.size() < 300) diffs += " " + t->name;
    }
    std::printf("  NPC inventories: %zu read, %zu unreadable, %zu stacks name an item no template has, %zu worn items\n", checked, unknown, unresolved, worn);
    if (!diffs.empty()) std::printf("  differ from the engine:%s\n", diffs.c_str());
    CHECK(checked >= 300);
    CHECK(diffs.empty());
    // a known one
    const TemplateInfo* jasmin = g->tpl("Jasmin");
    CHECK(jasmin && jasmin->inventory.size() == 1 && jasmin->inventory[0].item == "It_1H_Rapier" && jasmin->inventory[0].amount == 1);
    const TemplateInfo* berengir = g->tpl("Skeleton_SitDead_Berengir");
    CHECK(berengir && berengir->inventory.size() == 4);
}
