// Tests of the game log (core/gamelog.h) and the pure parts of Play (core/play.h). Included by modkit_tests.cpp.

#include "core/gamelog.h"
#include "core/play.h"

TEST(game_log_line_parsing) {
    GameLogLine l;
    CHECK(parse_game_log_line("[12:34:56.789] [error] [t4242] lua: [modkit_showcase] boom", l));
    CHECK_EQ(l.time, std::string("12:34:56.789"));
    CHECK_EQ(l.level, std::string("error"));
    CHECK_EQ(l.text, std::string("lua: [modkit_showcase] boom"));
    CHECK(parse_game_log_line("[00:00:01.001] [warning] [t1] x\r", l) && l.text == "x");
    CHECK(!parse_game_log_line("  stack traceback:", l));
    CHECK(!parse_game_log_line("[no time] [error] [t1] x yyyyyyyyyyyyy", l));
    CHECK(!parse_game_log_line("", l));
}

TEST(game_log_problems_link_to_objects) {
    Project p = load_showcase();
    const std::string option_info = info_name(p, *std::find_if(p.dialogs[0].graph.nodes.begin(), p.dialogs[0].graph.nodes.end(),
                                                               [](const GraphNode& n) { return n.type == "option"; }));
    auto line = [](const char* level, const std::string& text) { return GameLogLine{"10:00:00.000", level, text}; };
    std::vector<GameLogLine> lines = {
        line("info", "lua: [modkit_showcase] loaded"),                                        // not a problem
        line("error", "lua: [other_mod] unrelated"),                                          // another mod
        line("error", "engine: something " + option_info + " failed"),                       // names a dialogue option
        line("warning", "script MK_" + p.quests[0].name + "_done raised"),                    // a generated quest script
        line("error", "lua: [modkit_showcase] no object here"),                               // ours, but names nothing
        line("error", "lua: [modkit_showcase] no object here"),                               // merged with the previous
        line("error", "template MK_Hob_Knife_Plan_X missing"),                                // "MK_Hob_Knife" is not a whole word there
        line("error", "item MK_Hob_Knife can not be created"),                               // an item
    };
    const std::vector<Problem> v = game_log_problems(p, lines);
    CHECK_EQ(v.size(), size_t(4));
    if (v.size() == 4) {
        CHECK(v[0].area == "Dialogues" && v[0].uid == p.dialogs[0].uid && v[0].severity == Severity::Error);
        CHECK(v[1].area == "Quests" && v[1].uid == p.quests[0].uid && v[1].severity == Severity::Warning);
        CHECK(v[2].area == "Game" && v[2].uid.empty());
        CHECK(v[3].area == "Items" && v[3].subject == "MK_Hob_Knife");
    }
}

TEST(game_log_tail_follows_the_file) {
    const fs::path dir = temp_dir("gamelog");
    const fs::path f = dir / "spass.log";
    auto append = [&](const std::string& s) { std::ofstream(f, std::ios::binary | std::ios::app) << s; };
    LogTail t(u8str(f));
    std::vector<GameLogLine> out;
    bool reset = true;
    t.poll(out, reset);
    CHECK(out.empty() && !reset);                       // no file yet
    append("[10:00:00.000] [info] [t1] one\n[10:00:00.001] [err");
    t.poll(out, reset);
    CHECK(out.size() == 1 && !reset);                   // the half line waits
    append("or] [t1] two\n");
    t.poll(out, reset);
    CHECK(out.size() == 2 && out[1].text == "two" && out[1].level == "error");
    std::error_code ec;
    fs::remove(f, ec);
    append("[10:01:00.000] [info] [t1] again\n");       // the game started again: a shorter file
    std::vector<GameLogLine> out2;
    t.poll(out2, reset);
    CHECK(reset && out2.size() == 1 && out2[0].text == "again");
}

TEST(play_config_helpers) {
    CHECK_EQ(config_with_watch(""), std::string("[mods]\nwatch = true\n"));
    CHECK_EQ(config_with_watch("log_level = \"info\"\n"), std::string("log_level = \"info\"\n\n[mods]\nwatch = true\n"));
    CHECK_EQ(config_with_watch("[mods]\nwatch = false\nx = 1\n"), std::string("[mods]\nwatch = true\nx = 1\n"));
    CHECK_EQ(config_with_watch("[debug]\nx = 1\n[mods]\nx = 2\n"), std::string("[debug]\nx = 1\n[mods]\nwatch = true\nx = 2\n"));
    const std::string on = "[mods]\nwatch = true\n";
    CHECK_EQ(config_with_watch(on), on);
    bool changed = false;
    CHECK_EQ(config_local_windowed("<Video FullScreen=\"true\" />", changed), std::string("<Video FullScreen=\"false\" />"));
    CHECK(changed);
    config_local_windowed("<Video FullScreen=\"false\" />", changed);
    CHECK(!changed);
    const auto files = autostart_mod_files(PlayStart::QuickLoad);
    CHECK(files.size() == 2 && files[1].second.find("quick_load") != std::string::npos && files[0].second.find(kAutostartModId) != std::string::npos);
    CHECK(autostart_mod_files(PlayStart::NewGame)[1].second.find("new_game") != std::string::npos);
    CHECK(!play_plan("").problem.empty() && !play_plan("Z:\\no\\such\\folder").problem.empty());
}
