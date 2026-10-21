// Tests of the dialogue playthrough (core/dialog_sim.h). Included by modkit_tests.cpp.

#include "core/dialog_sim.h"

static json sim_infos() {
    return json::parse(R"([
      {"name":"d_hello","owner":"N","lines":[{"speaker":"player","listener":"npc","text":"Hello"},{"speaker":"npc","listener":"player","text":"Hi"},{"command":"End"}]},
      {"name":"d_work","owner":"N","permanent":true,"lines":[{"speaker":"player","listener":"npc","text":"Work?"},{"speaker":"npc","listener":"player","text":"Maybe"}]},
      {"name":"d_ore","owner":"N","parent":"d_work","when":["quest","Q_Ore","Running"],"lines":[{"speaker":"player","listener":"npc","text":"The ore"},{"script":"MK_Q_Ore_succeed"},{"command":"End"}]},
      {"name":"d_start","owner":"N","parent":"d_work","when":["not",["quest","Q_Ore","Running"]],"lines":[{"speaker":"player","listener":"npc","text":"Give me work"},{"script":"MK_Q_Ore_start"},{"command":"Give","Entity1":"npc","Entity2":"player","Item":"It_Apple","Amount":2}]},
      {"name":"d_rich","owner":"N","when":["all",[["item","It_Gold",100],["skill","Smith",2,">="]]],"lines":[{"speaker":"player","listener":"npc","text":"I am rich"},{"command":"End"}]},
      {"name":"d_orphan","owner":"N","parent":"nowhere","lines":[{"speaker":"player","listener":"npc","text":"x"},{"command":"End"}]}
    ])");
}

TEST(dialog_sim_conditions) {
    SimWorld w;
    CHECK(eval_condition(json(), w));  // null = true
    CHECK(!eval_condition(json::parse(R"(["quest","Q","Running"])"), w));
    w.quests["Q"] = "Running";
    CHECK(eval_condition(json::parse(R"(["quest","Q","Running"])"), w));
    CHECK(!eval_condition(json::parse(R"(["quest","Q"])"), w));  // the default status is Success
    CHECK(eval_condition(json::parse(R"(["not",["quest","Q","Success"]])"), w));
    CHECK(!eval_condition(json::parse(R"(["item","It_Apple",3])"), w));
    w.items["It_Apple"] = 3;
    CHECK(eval_condition(json::parse(R"(["item","It_Apple",3])"), w) && !eval_condition(json::parse(R"(["item","It_Apple",4])"), w));
    CHECK(!eval_condition(json::parse(R"(["item","It_Apple",1,"npc"])"), w));  // another holder: only a pin can say yes
    CHECK(!eval_condition(json::parse(R"(["var","v"])"), w));
    w.vars["v"] = 5;
    CHECK(eval_condition(json::parse(R"(["var","v"])"), w) && eval_condition(json::parse(R"(["var","v",5])"), w));
    CHECK(eval_condition(json::parse(R"(["var","v",3,">"])"), w) && !eval_condition(json::parse(R"(["var","v",9,">="])"), w));
    CHECK(eval_condition(json::parse(R"(["alive","Bob"])"), w) && !eval_condition(json::parse(R"(["dead","Bob"])"), w));
    w.dead.insert("Bob");
    CHECK(eval_condition(json::parse(R"(["dead","Bob"])"), w));
    CHECK(eval_condition(json::parse(R"(["any",[["dead","Nobody"],["dead","Bob"]]])"), w));
    CHECK(!eval_condition(json::parse(R"(["all",[["dead","Nobody"],["dead","Bob"]]])"), w));
    const json leaf = json::parse(R"(["event","gate_open"])");
    CHECK(!eval_condition(leaf, w));
    w.forced[leaf_key(leaf)] = true;  // a pin beats the world
    CHECK(eval_condition(leaf, w));
    std::vector<json> leaves;
    collect_leaves(json::parse(R"(["all",[["quest","A","Running"],["not",["event","e"]],["quest","A","Running"]]])"), leaves);
    CHECK_EQ(leaves.size(), size_t(2));  // distinct leaves only
}

TEST(dialog_sim_playthrough) {
    Project p;
    p.mod.languages = {"English"};
    json actions = json::array({json::object({{"script", "MK_Q_Ore_start"}, {"quest", "Q_Ore"}, {"action", "start"}}),
                                json::object({{"script", "MK_Q_Ore_succeed"}, {"quest", "Q_Ore"}, {"action", "succeed"}})});
    DialogSim s(p, sim_infos(), actions);
    auto names = [&] {
        std::vector<std::string> v;
        for (auto& o : s.options()) v.push_back(o.info);
        return v;
    };
    const std::vector<std::string> hello_work = {"d_hello", "d_work"};
    CHECK(names() == hello_work);  // d_rich needs gold and skill, d_ore/d_orphan have parents
    CHECK_EQ(s.leaves().size(), size_t(3));  // quest, item, skill (the "not" shares the quest leaf)
    CHECK(s.unreachable() == std::vector<std::string>{"d_orphan"});
    CHECK(!s.choose("d_ore"));  // not on offer
    CHECK(s.choose("d_work") && !s.ended());
    CHECK(names() == std::vector<std::string>{"d_start"});  // the ore option waits for the quest
    CHECK(s.transcript().size() == 2 && s.transcript()[0].kind == SimLine::Kind::Player && s.transcript()[1].text == "Maybe");
    CHECK(s.choose("d_start"));
    CHECK_EQ(s.world.quests["Q_Ore"], std::string("Running"));
    CHECK_EQ(s.world.items["It_Apple"], 2);
    // d_start has no sub-options and no End or Back: the sub-menu stays (as in the engine), now with the ore option
    CHECK(names() == std::vector<std::string>{"d_ore"});
    CHECK(s.choose("d_ore") && s.ended());
    CHECK_EQ(s.world.quests["Q_Ore"], std::string("Success"));
    CHECK(s.options().empty());
    s.restart();  // talk again: the world stays, used options stay used
    CHECK(names() == hello_work);  // d_work is permanent, d_hello was never used
    CHECK(s.choose("d_hello") && s.ended());
    s.restart();
    CHECK(names() == std::vector<std::string>{"d_work"});  // d_hello is used up now
    s.world.items["It_Gold"] = 100;
    s.world.skills["Smith"] = 2;
    CHECK((names() == std::vector<std::string>{"d_work", "d_rich"}));
    s.reset_all();
    CHECK(names() == hello_work && s.world.items.empty());
    s.leave();
    CHECK(s.ended() && s.options().empty());
}

TEST(dialog_sim_important_opens_the_conversation) {
    Project p;
    p.mod.languages = {"English"};
    json infos = json::parse(R"([
      {"name":"i_greet","type":"Important","lines":[{"speaker":"player","text":"Hey"},{"speaker":"npc","text":"Welcome back"}]},
      {"name":"i_bye","lines":[{"speaker":"player","text":"Bye"},{"command":"End"}]}])");
    DialogSim s(p, infos);
    CHECK(s.transcript().size() == 2 && s.transcript()[1].text == "Welcome back");  // played by itself
    CHECK(s.options().size() == 1 && s.options()[0].info == "i_bye");                 // and not listed
}

TEST(dialog_sim_back_returns_to_the_start) {
    Project p;
    p.mod.languages = {"English"};
    json infos = json::parse(R"([
      {"name":"t","permanent":true,"lines":[{"speaker":"player","text":"Town?"}]},
      {"name":"t_a","parent":"t","permanent":true,"lines":[{"speaker":"player","text":"Harbor?"}]},
      {"name":"t_b","parent":"t","permanent":true,"lines":[{"speaker":"player","text":"Never mind"},{"command":"Back"}]},
      {"name":"lore","permanent":true,"lines":[{"speaker":"player","text":"Lore?"}]}])");
    DialogSim s(p, infos);
    CHECK(s.choose("t") && s.options().size() == 2);
    CHECK(s.choose("t_a") && s.options().size() == 2 && s.options()[0].info == "t_a");  // the sub-menu stays
    CHECK(s.choose("t_b") && !s.ended() && s.options().size() == 2 && s.options()[0].info == "t");  // Back: the top level
    CHECK(s.choose("lore") && !s.ended() && s.options().size() == 2);  // a top-level option without End: the list again
}

TEST(dialog_sim_follows_a_compiled_project_dialogue) {
    Project p = load_showcase();
    std::vector<DialogIssue> issues;
    json actions = json::array();
    DialogSim s(p, compile_dialog(p, p.dialogs[0], issues, &actions), actions);
    CHECK(!s.options().empty());
    // every option that is on offer can be taken and leaves the sim in a consistent state (no crash, a transcript)
    for (auto& o : s.options()) {
        DialogSim t(p, compile_dialog(p, p.dialogs[0], issues, &actions), actions);
        CHECK(t.choose(o.info));
        CHECK(!t.transcript().empty());
    }
}
