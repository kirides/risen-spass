// Tests of the wizards (core/wizards.h) and the palette's matching (core/search.h). Included by modkit_tests.cpp.

#include "core/dialog_sim.h"
#include "core/search.h"
#include "core/wizards.h"

static std::string error_text(const Project& p, const GameData* g) {
    std::string all;
    for (auto& pr : validate(p, g))
        if (pr.severity == Severity::Error) all += pr.area + " " + pr.subject + ": " + pr.message + "\n";
    return all;
}

static Project wizard_base() {
    Project p;
    p.mod.id = "wiz";
    p.mod.languages = {"English", "German"};
    PlacedNpc n;
    n.uid = "n1";
    n.name = "Wiz_Hob";
    n.template_name = "Peasant_1_01";
    p.npcs.push_back(n);
    return p;
}

TEST(search_score_ranks_sensibly) {
    CHECK(search_score("knife", "MK_Hob_Knife") > 0);
    CHECK_EQ(search_score("sword", "MK_Hob_Knife"), 0);
    CHECK_EQ(search_score("", "anything"), 0);
    CHECK(search_score("hob knife", "MK_Hob_Knife \"Hob's Knife\"") > 0);        // every word, in any order
    CHECK(search_score("knife hob", "MK_Hob_Knife") > 0);
    CHECK_EQ(search_score("hob sword", "MK_Hob_Knife"), 0);                      // one word missing = no match
    CHECK(search_score("mk", "MK_Hob_Knife") > search_score("hob", "MK_Hob_Knife"));   // the start of the text beats a later word
    CHECK(search_score("hob", "MK_Hob_Knife") > search_score("ob_k", "MK_Hob_Knife"));  // a word start beats the middle of a word
    CHECK(search_score("KNIFE", "mk_hob_knife") > 0);                             // case-insensitive
}

TEST(wizard_fetch_quest) {
    Project p = wizard_base();
    FetchQuestParams w;
    w.npc = "Wiz_Hob";
    w.quest = "Q_Wiz_Ore";
    w.item = "It_IronOre";
    w.count = 2;
    w.xp = 150;
    w.gold = 20;
    w.reward_items = {{"It_Apple", 3}};
    const WizardResult r = wizard_fetch_quest(p, w);
    CHECK(r.ok());
    if (!r.ok()) {
        std::printf("  error: %s\n", r.error.c_str());
        return;
    }
    CHECK_EQ(p.quests.size(), size_t(1));
    CHECK_EQ(p.dialogs.size(), size_t(1));
    CHECK_EQ(p.dialogs[0].owner, std::string("Wiz_Hob"));
    // the quest compiles without a word to say
    std::vector<DialogIssue> issues;
    const json flow = compile_quest(p, p.quests[0], issues);
    for (auto& i : issues) std::printf("  quest issue: %s\n", i.message.c_str());
    CHECK(issues.empty());
    CHECK(flow["steps"].size() == 4 && flow["steps"][0]["objective"] == "items" && flow["steps"][1]["objective"] == "talk");
    CHECK(flow["steps"][2]["xp"] == 150 && flow["steps"][2]["gold"] == 20 && flow["steps"][2]["items"][0][0] == "It_Apple");
    // ... and so does the dialogue
    json actions = json::array();
    issues.clear();
    const json infos = compile_dialog(p, p.dialogs[0], issues, &actions);
    for (auto& i : issues) std::printf("  dialogue issue: %s\n", i.message.c_str());
    CHECK(issues.empty() && infos.size() == 2);
    // the talk objective names the turn-in option
    CHECK_EQ(flow["steps"][1]["info"].get<std::string>(), infos[1]["name"].get<std::string>());
    // played like a player: the job is offered once, the turn-in only with the quest running and the items in the bag
    DialogSim sim(p, infos, actions);
    auto offered = [&] {
        std::vector<std::string> v;
        for (auto& o : sim.options()) v.push_back(o.info);
        return v;
    };
    CHECK_EQ(offered().size(), size_t(1));
    CHECK(sim.choose(offered()[0]) && sim.ended());
    CHECK_EQ(sim.world.quests["Q_Wiz_Ore"], std::string("Running"));
    sim.restart();
    CHECK(offered().empty());  // the job was taken (non-permanent) and the turn-in waits for the items
    sim.world.items["It_IronOre"] = 2;
    CHECK_EQ(offered().size(), size_t(1));
    CHECK(sim.choose(offered()[0]) && sim.ended());
    // a second dialogue option set on an NPC that has one: the same graph is extended
    FetchQuestParams w2 = w;
    w2.quest = "Q_Wiz_Ore_2";
    CHECK(wizard_fetch_quest(p, w2).ok());
    CHECK_EQ(p.dialogs.size(), size_t(1));
    CHECK_EQ(compile_dialog(p, p.dialogs[0], issues).size(), size_t(4));
    // refusals
    auto refused = [&](auto&& edit) {
        FetchQuestParams x = w;
        x.quest = "Q_Wiz_Fresh";
        edit(x);
        Project q = wizard_base();
        const WizardResult res = wizard_fetch_quest(q, x);
        return !res.ok() && q.quests.empty() && q.dialogs.empty();
    };
    CHECK(refused([](FetchQuestParams& x) { x.npc = "bad name"; }));
    CHECK(refused([](FetchQuestParams& x) { x.count = 0; }));
    CHECK(refused([](FetchQuestParams& x) { x.item.clear(); }));
    CHECK(refused([](FetchQuestParams& x) { x.quest = "bad id!"; }));
    CHECK(!wizard_fetch_quest(p, w).ok());  // the quest id exists
}

TEST(wizard_weapon_and_trader) {
    Project p = wizard_base();
    WeaponParams w;
    w.name = "Wiz_Blade";
    w.base = "It_1H_Sword";
    w.label = {{"English", "Wiz Blade"}};
    w.damage = 33;
    w.value = 400;
    w.requirements = IntMap{{"Strength", 12}};
    w.ingredients = {{"It_IronOre", 3}, {"It_Gold", 10}};
    w.skills = IntMap{{"Smith", 1}};
    const WizardResult r = wizard_weapon(p, w);
    CHECK(r.ok());
    CHECK(p.items.size() == 1 && p.items[0].mode == Mode::New && *p.items[0].damage == 33 && p.items[0].requirements->at("Strength") == 12);
    CHECK(p.recipes.size() == 1 && *p.recipes[0].result == "Wiz_Blade" && p.recipes[0].learn && p.recipes[0].ingredients->size() == 2);
    CHECK(!wizard_weapon(p, w).ok());  // the name exists now
    WeaponParams bad = w;
    bad.name = "Wiz_NoRecipe";
    bad.ingredients.clear();
    CHECK(!wizard_weapon(p, bad).ok());
    bad.recipe = false;
    CHECK(wizard_weapon(p, bad).ok() && p.recipes.size() == 1);  // no recipe asked for: none made

    TraderParams t;
    t.npc = "Wiz_Trader";
    t.template_name = "Peasant_1_01";
    t.stock = {{"It_Apple", 4, false}, {"Wiz_Blade", 1, false}};
    const WizardResult tr = wizard_trader(p, t);
    CHECK(tr.ok() && !tr.notes.empty());
    CHECK(p.npcs.size() == 2 && p.npcs[1].name == "Wiz_Trader" && p.npcs[1].items.size() == 2 && p.npcs[1].offset);
    CHECK(p.npc_templates.size() == 1 && p.npc_templates[0].properties["gCDialog_PS"]["TradeEnabled"] == true && p.npcs[1].template_name == p.npc_templates[0].name);
    std::vector<DialogIssue> issues;
    const json infos = compile_dialog(p, p.dialogs.back(), issues);
    CHECK(issues.empty() && infos.size() == 1);
    bool trades = false;
    for (auto& l : infos[0]["lines"])
        if (l.contains("command") && l["command"] == "DoTrade") trades = true;
    CHECK(trades);
    CHECK(!wizard_trader(p, t).ok());  // the NPC exists
    TraderParams empty = t;
    empty.npc = "Wiz_Other";
    empty.stock.clear();
    CHECK(!wizard_trader(p, empty).ok());

    // with the game's data everything the wizards made validates
    auto g = game_data();
    if (!g) {
        std::printf("  (skipped the validation against the game data: no --data)\n");
        return;
    }
    Project q = wizard_base();
    FetchQuestParams f;
    f.npc = "Wiz_Hob";
    f.quest = "Q_Wiz_Ore";
    f.item = "It_IronOre";
    f.count = 2;
    CHECK(wizard_fetch_quest(q, f).ok());
    CHECK(wizard_weapon(q, w).ok());
    CHECK(wizard_trader(q, t).ok());
    const std::string errors = error_text(q, g.get());
    if (!errors.empty()) std::printf("  problems after the wizards:\n%s", errors.c_str());
    CHECK(errors.empty());
}
