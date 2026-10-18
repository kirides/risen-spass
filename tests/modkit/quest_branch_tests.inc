// Tests of the Either / or node of the quest graph (core/quest.cpp, the `race` step of modkit/runtime.lua). Included by modkit_tests.cpp.

#include <luajit/lua.hpp>

// start -> branch -> { objective A (carry ore) -> reward -> outcome success ; objective B (kill wolves) -> outcome fail }
static Project branching_project(bool second_path_has_outcome = true) {
    Project p;
    p.mod.id = "branch_test";
    p.mod.languages = {"English"};
    QuestDef q;
    q.uid = "quest1";
    q.name = "Q_Race";
    auto add = [&](const char* type, double x, double y) {
        GraphNode n = make_node(GraphKind::Quest, type, x, y);
        q.graph.nodes.push_back(n);
        return q.graph.nodes.back().uid;
    };
    auto set = [&](const std::string& uid, const char* key, json v) { q.graph.find(uid)->data[key] = std::move(v); };
    const std::string start = add("start", 0, 0), branch = add("branch", 300, 0);
    const std::string a = add("objective", 600, 0), reward = add("reward", 900, 0), win = add("outcome", 1200, 0);
    const std::string b = add("objective", 600, 220), lose = add("outcome", 900, 220);
    set(a, "kind", "items");
    set(a, "item", "It_IronOre");
    set(a, "count", 1);
    set(a, "text", json::object({{"English", "Bring ore"}}));
    set(reward, "xp", 10);
    set(b, "kind", "kill");
    set(b, "target", "Wolf");
    set(b, "count", 2);
    set(b, "text", json::object({{"English", "Kill the wolves"}}));
    set(lose, "result", "fail");
    add_link(GraphKind::Quest, q.graph, start, "next", branch, "in");
    add_link(GraphKind::Quest, q.graph, branch, "next", a, "in");
    add_link(GraphKind::Quest, q.graph, branch, "next", b, "in");
    add_link(GraphKind::Quest, q.graph, a, "next", reward, "in");
    add_link(GraphKind::Quest, q.graph, reward, "next", win, "in");
    if (second_path_has_outcome) add_link(GraphKind::Quest, q.graph, b, "next", lose, "in");
    p.quests.push_back(q);
    return p;
}

TEST(quest_branch_compiles_to_a_race) {
    Project p = branching_project();
    std::vector<DialogIssue> issues;
    json flow = compile_quest(p, p.quests[0], issues);
    for (auto& i : issues) std::printf("  issue: %s\n", i.message.c_str());
    CHECK(issues.empty());
    const json& steps = flow["steps"];
    CHECK_EQ(steps.size(), size_t(6));
    CHECK_EQ(steps[0]["kind"].get<std::string>(), std::string("race"));
    CHECK(steps[0]["paths"] == json::array({2, 5}));  // top to bottom: A (y 0) before B (y 220)
    CHECK(steps[1]["objective"] == "items" && steps[2]["kind"] == "reward" && steps[3]["kind"] == "outcome" && steps[3]["result"] == "success");
    CHECK(steps[4]["objective"] == "kill" && steps[4]["count"] == 2 && steps[5]["result"] == "fail");
    // a path that does not end in an Outcome is closed by a stop step, so the runtime does not run into the next path
    Project q = branching_project(false);
    issues.clear();
    flow = compile_quest(q, q.quests[0], issues);
    CHECK(flow["steps"].size() == 6 && flow["steps"][5]["kind"] == "stop");
    // problems
    auto problems = [&](const std::function<void(Project&)>& edit) {
        Project x = branching_project();
        edit(x);
        std::vector<DialogIssue> is;
        compile_quest(x, x.quests[0], is);
        std::string all;
        for (auto& i : is)
            if (i.error) all += i.message + "\n";
        return all;
    };
    CHECK(problems([](Project& x) {
              Graph& g = x.quests[0].graph;
              g.links.erase(std::remove_if(g.links.begin(), g.links.end(), [&](const GraphLink& l) { return l.from_node == g.nodes[1].uid && l.to_node == g.nodes[5].uid; }), g.links.end());
          }).find("at least two paths") != std::string::npos);
    CHECK(problems([](Project& x) {
              Graph& g = x.quests[0].graph;
              GraphNode r = make_node(GraphKind::Quest, "reward", 600, 500);
              g.nodes.push_back(r);
              add_link(GraphKind::Quest, g, g.nodes[1].uid, "next", r.uid, "in");
          }).find("has to begin with an Objective") != std::string::npos);
    CHECK(problems([](Project& x) {  // both paths lead to the same Outcome: paths do not join
              Graph& g = x.quests[0].graph;
              g.links.erase(std::remove_if(g.links.begin(), g.links.end(), [&](const GraphLink& l) { return l.from_node == g.nodes[5].uid; }), g.links.end());
              add_link(GraphKind::Quest, g, g.nodes[5].uid, "next", g.nodes[4].uid, "in");
          }).find("do not join") != std::string::npos);
}

TEST(quest_branch_survives_the_import_from_data_files) {
    Project p = branching_project();
    const fs::path dir = temp_dir("quest_branch") / "branch_test";
    ExportOptions o;
    CHECK(export_mod(p, u8str(dir), o).ok);
    const ImportResult r = import_mod(u8str(dir), true);
    CHECK(r.ok);
    if (!r.ok) return;
    CHECK_EQ(r.project.quests.size(), size_t(1));
    int branches = 0, objectives = 0;
    for (auto& n : r.project.quests[0].graph.nodes) {
        branches += n.type == "branch";
        objectives += n.type == "objective";
    }
    std::printf("  imported quest nodes: %zu (branches %d, objectives %d)\n",r.project.quests[0].graph.nodes.size(), branches, objectives);
    CHECK(branches == 1 && objectives == 2);
    std::vector<DialogIssue> i1, i2;
    CHECK_EQ(compile_quest(r.project, r.project.quests[0], i1).dump(), compile_quest(p, p.quests[0], i2).dump());  // the same flow comes out again
    CHECK(i1.empty());
}

// the generated runtime, driven by a stand-in for the layer's API: events are fired by hand, saves are copied like the engine's savegames
static const char* kRaceScript = R"LUA(
local failures = {}
local function check(name, cond) if not cond then failures[#failures + 1] = name end end
local function deep(v) if type(v) ~= "table" then return v end local t = {} for k, x in pairs(v) do t[k] = deep(x) end return t end

local saves, handlers, emitted, logs, quest_calls, xp = {}, {}, {}, {}, {}, 0
local player = { is_player = function() return true end }
local carried = 0
risen = {
    -- like the layer's risen.save: the table that was set comes back from get (the same one: a flow that mutates its state sees the change everywhere)
    save = { get = function(k) return saves[k] end, set = function(k, v) saves[k] = v end },
    on = function(ev, fn) handlers[ev] = handlers[ev] or {} handlers[ev][#handlers[ev] + 1] = fn end,
    emit = function(...) emitted[#emitted + 1] = { ... } end,
    log = { info = function() end, warn = function() end, error = function(m) failures[#failures + 1] = "log.error: " .. tostring(m) end },
    quest = {
        open = function(q) quest_calls[#quest_calls + 1] = "open " .. q end, run = function() end,
        add_log = function(q, t) logs[#logs + 1] = t end,
        succeed = function(q) quest_calls[#quest_calls + 1] = "succeed " .. q end, fail = function(q) quest_calls[#quest_calls + 1] = "fail " .. q end,
        close = function(q) quest_calls[#quest_calls + 1] = "close " .. q end, define = function() return true end,
        get = function() return nil end,
    },
    entity = { player = function() return player end },
    item = { count = function() return carried end, give = function() end },
    script = { call = function(name, a, b, amount) if name == "GiveXP" then xp = xp + amount end end },
    gamevar = { get = function() return nil end, set = function() end },
    world = {},
}
mod = { id = "t" }
local function fire(ev, ...) for _, fn in ipairs(handlers[ev] or {}) do fn(...) end end
local function wolf() return { name = function() return "Wolf_1" end, template = function() return { name = function() return "Wolf" end } end } end

local FLOW = { trigger = { kind = "dialog" }, steps = {
    { kind = "race", paths = { 2, 5 } },
    { kind = "objective", objective = "items", item = "It_IronOre", count = 1, log = "Bring ore" },
    { kind = "reward", xp = 10 },
    { kind = "outcome", result = "success" },
    { kind = "objective", objective = "kill", target = "Wolf", count = 2, log = "Kill the wolves" },
    { kind = "outcome", result = "fail" },
} }
local M = assert(loadstring(RUNTIME_SOURCE, "runtime"))()
check("version", M.VERSION >= 8)
local FLOW2 = { trigger = { kind = "dialog" }, steps = {
    { kind = "race", paths = { 2, 4 } },
    { kind = "objective", objective = "kill", target = "Wolf", count = 1 },
    { kind = "outcome", result = "success" },
    { kind = "objective", objective = "talk", info = "X" },
    { kind = "outcome", result = "fail" },
} }
M.install({ quests = { { name = "Q", title = "t", flow = FLOW }, { name = "Q2", title = "t2", flow = FLOW2 } } })

-- the race starts: both ways are announced, nothing is decided
M.start_quest("Q")
check("both ways are announced", #logs == 2 and logs[1] == "Bring ore" and logs[2] == "Kill the wolves")
check("waiting in the race", saves["modkit.quest.Q"].step == 1 and saves["modkit.quest.Q"].gates ~= nil and not saves["modkit.quest.Q"].done)
-- one wolf: progress inside its own way, no decision yet
fire("entity.died", wolf())
check("one wolf does not decide", saves["modkit.quest.Q"].step == 1 and saves["modkit.quest.Q"].gates["5"].n == 1)
local before = deep(saves)
-- the ore decides: reward once, success, the wolves' way is gone
carried = 1
fire("inventory.changed", player, "It_IronOre", 1)
check("the ore way was taken", saves["modkit.quest.Q"].done == true and saves["modkit.quest.Q"].result == "success")
check("reward paid once", xp == 10)
check("quest succeeded", quest_calls[#quest_calls] == "succeed Q")
fire("entity.died", wolf()) fire("entity.died", wolf()) fire("inventory.changed", player, "It_IronOre", 1)
check("nothing more after the decision", xp == 10 and #quest_calls == 2)
local objective_events = 0
for _, e in ipairs(emitted) do if e[1] == "modkit.objective" then objective_events = objective_events + 1 + (e[4] == 2 and 0 or 100) end end
check("the deciding objective was reported (step 2)", objective_events == 1)

-- a quick load rolls back to the undecided race; the other way wins this time
saves = before
xp, quest_calls, carried = 0, {}, 0   -- the ore is not in the bag any more
fire("entity.died", wolf())
check("the wolves' way was taken", saves["modkit.quest.Q"].done == true and saves["modkit.quest.Q"].result == "fail")
check("failed, no reward", quest_calls[#quest_calls] == "fail Q" and xp == 0)
carried = 1
fire("inventory.changed", player, "It_IronOre", 1)
check("the ore is too late", saves["modkit.quest.Q"].result == "fail" and xp == 0)

-- the world loads with the ore already in the bag: the race is looked at again (world.ready)
saves = {}
logs, quest_calls, xp = {}, {}, 0
M.start_quest("Q")
check("a race that is decided at once", saves["modkit.quest.Q"].result == "success" and xp == 10)

-- the FIRST way of a race decides while a later way is still waiting: the loop over the ways must not touch the gates that were just dropped (found by the
-- chronicle example, whose camp quest kills the warlord or pays the tithe)
saves, quest_calls = {}, {}
M.start_quest("Q2")
fire("entity.died", wolf())
check("the first way decides without an error", saves["modkit.quest.Q2"].done == true and saves["modkit.quest.Q2"].result == "success" and quest_calls[#quest_calls] == "succeed Q2")
return table.concat(failures, "; ")
)LUA";

TEST(quest_race_runs_in_the_runtime) {
    lua_State* L = luaL_newstate();
    luaL_openlibs(L);
    const std::string runtime = runtime_lua();
    lua_pushlstring(L, runtime.data(), runtime.size());
    lua_setglobal(L, "RUNTIME_SOURCE");
    std::string result;
    if (luaL_loadstring(L, kRaceScript) != 0 || lua_pcall(L, 0, 1, 0) != 0) result = std::string("lua error: ") + lua_tostring(L, -1);
    else result = lua_tostring(L, -1) ? lua_tostring(L, -1) : "";
    lua_close(L);
    if (!result.empty()) std::printf("  runtime test failed: %s\n", result.c_str());
    CHECK(result.empty());
}
