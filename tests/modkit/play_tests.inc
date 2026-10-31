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

// ---- the live link (core/link_client.h): config helper and the client against a fake game on a named pipe ----------------------------

#include "core/link_client.h"

TEST(config_with_link_sets_the_pipe_on) {
    CHECK_EQ(config_with_link(""), std::string("[link]\nenabled = true\n"));
    CHECK_EQ(config_with_link("log_level = \"info\"\n"), std::string("log_level = \"info\"\n\n[link]\nenabled = true\n"));
    const std::string off = "[mods]\nwatch = true\n[link]\nenabled = false\nname = \"x\"\n[test]\nenabled = true\n";
    const std::string on = config_with_link(off);
    CHECK(on.find("[link]\nenabled = true\nname = \"x\"\n[test]\nenabled = true\n") != std::string::npos);
    CHECK_EQ(config_with_link(on), on);  // idempotent
    CHECK(on.find("enabled = false") == std::string::npos);
}

#ifdef _WIN32
#include <windows.h>
#undef near  // windows.h macros that would break the later test files
#undef far

namespace {
// A fake game: one named pipe instance answering `reload` like the layer does, after pushing a log event.
struct FakeGame {
    std::string path;
    std::thread thread;
    bool fail_reload = false;
    FakeGame(const std::string& name, bool fail) : path("\\\\.\\pipe\\" + name), fail_reload(fail) {
        HANDLE pipe = CreateNamedPipeA(path.c_str(), PIPE_ACCESS_DUPLEX, PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1, 65536, 65536, 0, nullptr);
        thread = std::thread([this, pipe] {
            if (ConnectNamedPipe(pipe, nullptr) || GetLastError() == ERROR_PIPE_CONNECTED) {
                char buf[65536];
                DWORD n = 0;
                while (ReadFile(pipe, buf, sizeof buf, &n, nullptr)) {
                    const nlohmann::json req = nlohmann::json::parse(std::string(buf, n), nullptr, false);
                    auto send = [&](const nlohmann::json& j) {
                        const std::string t = j.dump();
                        DWORD w = 0;
                        WriteFile(pipe, t.data(), static_cast<DWORD>(t.size()), &w, nullptr);
                    };
                    send({{"event", "log"}, {"data", {{"level", "info"}, {"text", "hello from the game"}}}});
                    if (req.value("cmd", "") == "reload") {
                        nlohmann::json action = {{"id", req["args"]["mod"]}, {"action", fail_reload ? "failed" : "reloaded"}, {"handlers_removed", 3}, {"handlers_added", 4}};
                        if (fail_reload) action["error"] = "main.lua:7: boom";
                        send({{"id", req["id"]}, {"ok", !fail_reload}, {"result", {{"reports", nlohmann::json::array({{{"actions", nlohmann::json::array({action})}}})}}}});
                    } else {
                        send({{"id", req["id"]}, {"ok", false}, {"error", "unknown command"}});
                    }
                }
            }
            DisconnectNamedPipe(pipe);
            CloseHandle(pipe);
        });
    }
    ~FakeGame() {
        if (thread.joinable()) thread.join();
    }
};
}  // namespace

TEST(link_client_reload_against_a_fake_game) {
    const std::string name = "mk-test-link-" + std::to_string(GetCurrentProcessId());
    {
        FakeGame game(name, false);
        LinkClient c;
        std::string err;
        CHECK(c.connect_pipe(game.path, err));
        if (!err.empty()) std::printf("  link client: %s\n", err.c_str());
        const LinkReply r = c.request("reload", {{"mod", "my_mod"}});
        CHECK(r.ok && r.result["reports"][0]["actions"][0]["action"] == "reloaded");
        const auto events = c.take_events();
        CHECK(events.size() == 1 && events[0]["event"] == "log" && events[0]["data"]["text"] == "hello from the game");
        const LinkReply bad = c.request("nope");
        CHECK(!bad.ok && bad.error == "unknown command");
        c.close();
    }
    // link_reload through link.json of a game folder
    const std::string name2 = name + "-b";
    for (const bool fail : {false, true}) {
        FakeGame game(name2, fail);
        const fs::path dir = temp_dir(fail ? "link_fail" : "link_ok");
        write_text(dir / "bin" / "spass" / "link.json", nlohmann::json{{"pipe", game.path}, {"pid", 1}, {"protocol", 1}}.dump());
        const LinkReload lr = link_reload(u8str(dir), "my_mod");
        CHECK(lr.reachable);
        if (lr.message.find(fail ? "boom" : "reloaded") == std::string::npos) std::printf("  link_reload said: %s\n", lr.message.c_str());
        CHECK_EQ(lr.ok, !fail);
        CHECK(fail ? lr.message.find("boom") != std::string::npos : lr.message.find("reloaded") != std::string::npos);
    }
    // no link.json: not reachable (the caller falls back to the file watcher)
    CHECK(!link_reload(u8str(temp_dir("link_none")), "my_mod").reachable);
    LinkClient closed;
    CHECK(!closed.request("hello").ok);
}
#endif
