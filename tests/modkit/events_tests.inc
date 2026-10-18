// Tests of the event graphs (stage E21: core/events.*, the node kinds, export, import, validation and a run of modkit/events.lua against a stand-in
// API). Included by modkit_tests.cpp.

#include "core/events.h"

namespace {

struct EvBuilder {
    EventGraphDef g;
    EvBuilder(const char* name) {
        g.uid = new_uid();
        g.name = name;
    }
    std::string add(const char* type, double x, double y, json data = json::object()) {
        GraphNode n = make_node(GraphKind::Event, type, x, y);
        for (auto& [k, v] : data.items()) n.data[k] = v;
        g.graph.nodes.push_back(n);
        return n.uid;
    }
    void link(const std::string& a, const char* pin, const std::string& b) {
        std::string why;
        const std::string id = add_link(GraphKind::Event, g.graph, a, pin, b, "in", &why);
        if (id.empty()) std::printf("  link refused: %s\n", why.c_str());
        CHECK(!id.empty());
    }
};

// [on chapter 3] -> [condition: intro = 0] -> [set intro = 1] -> [hud message] -> [end]
EventGraphDef example_graph() {
    EvBuilder b("Chapter three");
    const std::string trig = b.add("on_chapter", 0, 0, json::object({{"chapter", 3}}));
    json test = event_default_test("var");
    test["name"] = "intro";
    test["value"] = 0;
    const std::string cond = b.add("condition", 300, 0, json::object({{"tests", json::array({test})}}));
    const std::string set = b.add("set_variable", 600, 0, json::object({{"name", "intro"}, {"value", 1}}));
    const std::string hud = b.add("hud_message", 900, 0, json::object({{"text", json::object({{"English", "Chapter {chapter}"}})}}));
    const std::string end = b.add("end", 1200, 0);
    b.link(trig, "next", cond);
    b.link(cond, "then", set);
    b.link(set, "next", hud);
    b.link(hud, "next", end);
    return b.g;
}

Project events_project() {
    Project p;
    p.mod.id = "ev_test";
    p.mod.languages = {"English", "German"};
    p.events.push_back(example_graph());
    return p;
}

size_t count_errors(const Project& p, const EventGraphDef& g) {
    std::vector<DialogIssue> issues;
    compile_events(p, g, issues);
    size_t n = 0;
    for (auto& i : issues) {
        if (i.error) ++n;
        if (i.error) std::printf("  issue: %s\n", i.message.c_str());
    }
    return n;
}

}  // namespace

TEST(events_every_kind_has_a_valid_default) {
    // every node kind can be made, its pins are flow pins, and the default data of a trigger / action has no out-of-range or unknown value
    CHECK(event_specs().size() >= 30);
    for (auto& s : event_specs()) {
        const NodeTypeDef* t = node_type(GraphKind::Event, s.type);
        CHECK(t != nullptr);
        if (!t) continue;
        GraphNode n = make_node(GraphKind::Event, s.type, 0, 0);
        CHECK(n.data.is_object());
        CHECK_EQ(t->pins.size(), s.outs.size() + (s.trigger || std::string(s.type) == "comment" ? 0u : 1u));
        for (auto& f : s.fields)
            if (f.kind == EvField::Combo) CHECK(std::find(f.options.begin(), f.options.end(), n.data.value(f.key, std::string())) != f.options.end());
    }
    for (auto& t : event_tests()) {
        const json d = event_default_test(t.kind);
        CHECK_EQ(d["kind"].get<std::string>(), std::string(t.kind));
    }
}

TEST(events_example_graph_compiles) {
    const Project p = events_project();
    std::vector<DialogIssue> issues;
    const json d = compile_events(p, p.events[0], issues);
    CHECK(issues.empty());
    CHECK_EQ(d["nodes"].size(), size_t(5));
    CHECK_EQ(d["nodes"][0]["type"].get<std::string>(), std::string("on_chapter"));
    CHECK_EQ(d["nodes"][0]["chapter"].get<int>(), 3);
    const std::string cond_id = d["nodes"][1]["id"].get<std::string>();
    CHECK(d["nodes"][0]["outs"]["next"] == json::array({cond_id}));
    CHECK(d["nodes"][1]["outs"].contains("then") && !d["nodes"][1]["outs"].contains("else"));
    CHECK_EQ(d["nodes"][2]["name"].get<std::string>(), std::string("intro"));
    CHECK(d["nodes"][4]["outs"].empty());
    CHECK_EQ(d["key"].get<std::string>().size(), size_t(10));
}

TEST(events_validation) {
    Project p = events_project();
    CHECK_EQ(count_errors(p, p.events[0]), size_t(0));
    // no trigger
    EventGraphDef g = example_graph();
    remove_node(g.graph, g.graph.nodes[0].uid);
    CHECK(count_errors(p, g) >= 1);
    // an empty required field
    g = example_graph();
    g.graph.nodes[2].data["name"] = "";
    CHECK(count_errors(p, g) >= 1);
    // a bad variable name, a chapter out of range, a trigger that moves less than a metre
    g = example_graph();
    g.graph.nodes[2].data["name"] = "a b";
    CHECK(count_errors(p, g) >= 1);
    EvBuilder b("Move");
    const std::string mv = b.add("on_move", 0, 0, json::object({{"min_m", 0.2}}));
    const std::string lg = b.add("log", 300, 0);
    b.link(mv, "next", lg);
    CHECK(count_errors(p, b.g) >= 1);
    // a loop needs a Delay
    EvBuilder l("Loop");
    const std::string t = l.add("on_signal", 0, 0, json::object({{"name", "x"}}));
    const std::string a1 = l.add("log", 300, 0), a2 = l.add("log", 600, 0);
    l.link(t, "next", a1);
    l.link(a1, "next", a2);
    l.link(a2, "next", a1);
    CHECK(count_errors(p, l.g) >= 1);
    const std::string dl = l.add("delay", 450, 100);
    l.g.graph.links.pop_back();
    l.link(a2, "next", dl);
    l.link(dl, "next", a1);
    CHECK_EQ(count_errors(p, l.g), size_t(0));
    // warnings: a node no trigger reaches
    EventGraphDef w = example_graph();
    w.graph.nodes.push_back(make_node(GraphKind::Event, "log", 0, 400));
    std::vector<DialogIssue> issues;
    compile_events(p, w, issues);
    bool warned = false;
    for (auto& i : issues) warned |= !i.error && i.message.find("not reached") != std::string::npos;
    CHECK(warned);
    // the project-level validation reports an unknown item and counts the graph's errors
    EventGraphDef r = example_graph();
    const std::string give = r.graph.nodes.back().uid;
    GraphNode gi = make_node(GraphKind::Event, "give_item", 1500, 0);
    gi.data["item"] = "It_Missing_Thing";
    r.graph.nodes.push_back(gi);
    Project q = p;
    q.events = {r};
    const auto problems = validate(q, nullptr);
    bool any = false;
    for (auto& pr : problems) any |= pr.area == "Events";
    CHECK(any);  // the empty item of the Give item node
    EventGraphDef d2 = example_graph();
    d2.name = "";
    q.events = {d2};
    CHECK(error_count(validate(q, nullptr)) >= 1);
}

TEST(events_project_json_round_trip) {
    const Project p = events_project();
    const std::string text = dump_project(p);
    std::vector<std::string> warnings;
    const Project q = project_from_json(json::parse(text), &warnings);
    CHECK(warnings.empty());
    CHECK_EQ(q.events.size(), size_t(1));
    CHECK_EQ(q.events[0].graph.nodes.size(), size_t(5));
    CHECK_EQ(q.events[0].graph.links.size(), size_t(4));
    CHECK_EQ(dump_project(q), text);
    // a project without event graphs does not even mention the section
    CHECK(dump_project(Project{}).find("\"events\"") == std::string::npos);
}

TEST(events_export_and_import_round_trip) {
    Project p = events_project();
    // a richer second graph: every kind of node once
    EvBuilder b("Everything");
    const std::string t = b.add("on_kill", 0, 0, json::object({{"target", "Wolf"}}));
    std::string prev = t;
    std::string prev_pin = "next";
    double y = 0;
    for (auto& s : event_specs()) {
        if (s.trigger || std::string(s.type) == "comment" || std::string(s.type) == "end") continue;
        const std::string n = b.add(s.type, 300, y += 120);
        if (std::string(s.type) == "condition") b.link(n, "else", b.add("log", 700, y));
        b.link(prev, prev_pin.c_str(), n);
        prev = n;
        prev_pin = s.outs.empty() ? "next" : s.outs[0];
    }
    b.add("comment", 0, 800, json::object({{"text", "ignored"}}));
    p.events.push_back(b.g);
    const fs::path dir = temp_dir("events") / "ev_test";
    CHECK(export_mod(p, u8str(dir), ExportOptions{}).ok);
    CHECK(fs::exists(dir / "data" / "events.lua"));
    CHECK(fs::exists(dir / "modkit" / "events.lua"));
    CHECK(read_text(dir / "main.lua").find("modkit.events") != std::string::npos);
    const ImportResult via_project = import_mod(u8str(dir));
    CHECK(via_project.ok && via_project.from_project_file);
    CHECK_EQ(dump_project(via_project.project), dump_project(p));
    const ImportResult r = import_mod(u8str(dir), true);
    for (auto& w : r.warnings) std::printf("  import: %s\n", w.c_str());
    CHECK(r.ok && !r.from_project_file);
    CHECK_EQ(r.project.events.size(), size_t(2));
    const auto a = render_mod(p), c = render_mod(r.project);
    for (auto& [rel, content] : a) {
        if (rel == "modkit/project.json") continue;
        auto it = c.find(rel);
        CHECK(it != c.end());
        if (it != c.end() && it->second != content) {
            ++g_failures;
            std::printf("  differs after the import: %s\n", rel.c_str());
            write_text(fs::temp_directory_path() / "ev_a.lua", content);
            write_text(fs::temp_directory_path() / "ev_b.lua", it->second);
        }
    }
    // no event graphs: no event files, main.lua as before
    const auto none = render_mod(Project{});
    CHECK(!none.count("data/events.lua") && !none.count("modkit/events.lua"));
    CHECK(none.at("main.lua").find("modkit.events") == std::string::npos);
}

TEST(events_movement_filter_cpp) {
    MovementState s;
    CHECK(!movement_step(s, 0, 0, 0));          // the first position only sets the anchor
    CHECK(!movement_step(s, 30, 0, 0));
    CHECK(!movement_step(s, 99, 0, 0));         // 0.99 m
    CHECK(movement_step(s, 100, 0, 0));         // 1 m: a sample
    CHECK(!movement_step(s, 100, 99, 0));
    CHECK(movement_step(s, 100, 99, 100));      // 1.4 m from the anchor
    CHECK(!movement_step(s, 100, 99, 100));     // standing (a turn changes no position)
    CHECK(!movement_step(s, std::nan(""), 0, 0));
}

static const char* kEventsScript = R"LUA(
local failures = {}
local function check(name, cond) if not cond then failures[#failures + 1] = name end end
local function deep(v) if type(v) ~= "table" then return v end local o = {} for k, x in pairs(v) do o[k] = deep(x) end return o end

local handlers, saves, emitted, hud, gv, warns = {}, {}, {}, {}, {}, {}
local now, chapter = 0, 1
local pos = { 0, 0, 0 }
local function fire(ev, ...) for _, fn in ipairs(handlers[ev] or {}) do fn(...) end end
local player = { position = function() return pos[1], pos[2], pos[3] end, is_player = function() return true end,
                 level = function() return 3 end, add_hp = function() end, add_mana = function() end }
risen = {
    on = function(ev, fn) handlers[ev] = handlers[ev] or {} table.insert(handlers[ev], fn) end,
    emit = function(...) emitted[#emitted + 1] = { ... } end,
    clock = function() return now end,
    log = { info = function() end, warn = function(m) warns[#warns + 1] = m end, error = function(m) warns[#warns + 1] = m end },
    save = { get = function(k) return deep(saves[k]) end, set = function(k, v) saves[k] = deep(v) end, remove = function(k) saves[k] = nil end },
    gamevar = { get = function(n) return gv[n] end,
                set = function(n, v) local prev = gv[n] gv[n] = v fire("gamevar.changed", n, v, prev) end,
                remove = function(n) gv[n] = nil end },
    quest = { chapter = function() return chapter end, add_log = function() end, set_chapter = function(n) chapter = n end },
    entity = { player = function() return player end, get = function() return nil end, npcs = function() return {} end },
    world = { time = function() return 8, 0 end },
    cond = { test = function(c) return c end },
    loc = { language = function() return "English" end },
    hud = { log = function(t) hud[#hud + 1] = t end, item_log = function() end },
    item = { give = function() end, take = function() end },
    script = { call = function() return 0 end },
    voice = { say = function() end },
    npc = { spawn = function() return {} end },
}
mod = { id = "t" }
local E = assert(loadstring(EVENTS_SOURCE, "events"))()
local data = assert(loadstring(DATA_SOURCE, "data"))()
E.install(data, { cond = function(d) return d end, quest_action = function() end })
local function tick(n) for _ = 1, n do now = now + 0.1 fire("tick") end end

-- pure helpers
local ms = {}
check("anchor", E.movement_step(ms, 0, 0, 0) == false)
check("0.5 m", E.movement_step(ms, 50, 0, 0) == false)
check("1 m", E.movement_step(ms, 100, 0, 0) == true)
check("hours", E.in_hours(23, 22, 6) and E.in_hours(3, 22, 6) and not E.in_hours(12, 22, 6) and E.in_hours(5, 5, 5))
check("compare", E.compare(nil, "==", 0) and E.compare(3, ">=", 3) and not E.compare(2, ">", 2))
check("format", E.format("a {x} {var:v} {nope}", { x = 7 }, function() return 4 end) == "a 7 4 {nope}")
check("text", E.pick_text({ German = "Hallo", English = "Hi" }, "English", "German") == "Hi" and E.pick_text({ German = "Hallo" }, "English", "English") == "Hallo")

fire("world.ready", "new")
tick(5)
-- A. the chapter graph: chapter 3 -> intro = 1 once, message with the chapter number
check("nothing yet", gv.intro == nil and #hud == 0)
chapter = 3
tick(31)
check("chapter 3 sets the variable", gv.intro == 1)
check("message", hud[1] == "Chapter 3")
chapter = 4 tick(31) chapter = 3 tick(31)
check("the condition keeps it from running twice", #hud == 1)

-- B. signal -> once -> delay; the pending delay is part of the state
E.fire_signal("go")
tick(1)
check("once passed", saves["modkit.ev.pending"] and #saves["modkit.ev.pending"] == 1)
local snap_saves, snap_gv = deep(saves), deep(gv)
tick(60)
check("the delay ran out", hud[#hud] == "late" and #saves["modkit.ev.pending"] == 0)
local late = 0 for _, h in ipairs(hud) do if h == "late" then late = late + 1 end end
check("late once", late == 1)
-- a quick load: the state of the moment of the snapshot comes back, the delay is pending again
saves, gv = deep(snap_saves), deep(snap_gv)
fire("load.after")
tick(60)
late = 0 for _, h in ipairs(hud) do if h == "late" then late = late + 1 end end
check("after the load the delay runs again, once", late == 2)
E.fire_signal("go")
tick(2)
check("the second pass goes to again", hud[#hud] == "again")

-- C. movement: once per metre, never for turning
local function moves() return saves["modkit.var.moves"] or 0 end
pos = { 0, 0, 0 }
fire("world.ready", "load")
tick(2)   -- anchor
check("no fire for the anchor", moves() == 0)
pos = { 40, 0, 0 } tick(3)
check("under a metre", moves() == 0)
for _ = 1, 20 do tick(1) end   -- turning: the position does not change
check("turning", moves() == 0)
pos = { 99, 0, 0 } tick(2)
check("0.99 m", moves() == 0)
pos = { 101, 0, 0 } tick(2)
check("a metre", moves() == 1)
pos = { 150, 0, 0 } tick(2) pos = { 190, 0, 0 } tick(2)
check("small steps add up only to the metre", moves() == 1)
pos = { 205, 0, 0 } tick(2)
check("the second metre", moves() == 2)
-- D. the area: 5 m around (3000, 0, 0)
local function inside() return gv.area_in end
pos = { 1500, 0, 0 } tick(2)
pos = { 1800, 0, 0 } tick(2)
check("outside", inside() == nil)
pos = { 2300, 0, 0 } tick(2)
check("not yet inside", inside() == nil)
pos = { 2600, 0, 0 } tick(2)
check("entered", inside() == 1)
pos = { 3000, 0, 0 } tick(2) pos = { 3100, 0, 0 } tick(2)
check("inside is one entry", gv.area_enters == 1)
pos = { 4000, 0, 0 } tick(2)
check("left", gv.area_in == 0)
check("no warnings", #warns == 0)
if #warns > 0 then failures[#failures + 1] = warns[1] end
return table.concat(failures, "; ")
)LUA";

TEST(events_runtime_runs_the_graphs) {
    Project p;
    p.mod.id = "ev_run";
    p.mod.languages = {"English"};
    p.events.push_back(example_graph());
    {
        // B: on signal go -> once -> (next) delay 5 -> "late"; (again) -> "again"
        EvBuilder b("Signal");
        const std::string t = b.add("on_signal", 0, 0, json::object({{"name", "go"}}));
        const std::string once = b.add("once", 300, 0);
        const std::string dl = b.add("delay", 600, 0, json::object({{"seconds", 5}}));
        const std::string late = b.add("hud_message", 900, 0, json::object({{"text", json::object({{"English", "late"}})}}));
        const std::string again = b.add("hud_message", 600, 200, json::object({{"text", json::object({{"English", "again"}})}}));
        b.link(t, "next", once);
        b.link(once, "next", dl);
        b.link(dl, "next", late);
        b.link(once, "again", again);
        // the first graph sends nothing: the signal comes from Lua
        p.events.push_back(b.g);
    }
    {
        // C: on move 1 m -> save variable moves += 1
        EvBuilder b("Move");
        const std::string t = b.add("on_move", 0, 0, json::object({{"min_m", 1}}));
        const std::string s = b.add("set_variable", 300, 0, json::object({{"scope", "save"}, {"name", "moves"}, {"op", "add"}, {"value", 1}}));
        b.link(t, "next", s);
        p.events.push_back(b.g);
    }
    {
        // D: area enter / leave
        EvBuilder b("Area");
        const std::string t = b.add("on_area", 0, 0, json::object({{"x", 3000}, {"radius", 5}, {"on", "enters or leaves"}}));
        const std::string c = b.add("condition", 300, 0, json::object({{"tests", json::array({json::object({{"kind", "var"}, {"name", "area_in"}, {"op", "=="}, {"value", 0}, {"scope", "game"}})})}}));
        const std::string in = b.add("set_variable", 600, -60, json::object({{"name", "area_in"}, {"value", 1}}));
        const std::string cnt = b.add("set_variable", 900, -60, json::object({{"name", "area_enters"}, {"op", "add"}, {"value", 1}}));
        const std::string out = b.add("set_variable", 600, 60, json::object({{"name", "area_in"}, {"value", 0}}));
        b.link(t, "next", c);
        // direction is not a condition kind: "enters" = area_in is 0 so far; otherwise it leaves
        b.link(c, "then", in);
        b.link(in, "next", cnt);
        b.link(c, "else", out);
        p.events.push_back(b.g);
    }
    for (auto& e : p.events) CHECK_EQ(count_errors(p, e), size_t(0));
    lua_State* L = luaL_newstate();
    luaL_openlibs(L);
    const std::string events = events_lua();
    lua_pushlstring(L, events.data(), events.size());
    lua_setglobal(L, "EVENTS_SOURCE");
    const std::string data = "return " + lua_value(events_data(p), 0, 60);
    lua_pushlstring(L, data.data(), data.size());
    lua_setglobal(L, "DATA_SOURCE");
    std::string result;
    if (luaL_loadstring(L, kEventsScript) != 0 || lua_pcall(L, 0, 1, 0) != 0) result = std::string("lua error: ") + lua_tostring(L, -1);
    else result = lua_tostring(L, -1) ? lua_tostring(L, -1) : "";
    lua_close(L);
    if (!result.empty()) std::printf("  events runtime test failed: %s\n", result.c_str());
    CHECK(result.empty());
}

// ---- mod layers (core/layers.*, modkit/layers.lua) -------------------------------------------------------------------------------------------------

#include "core/layers.h"

namespace {

Project layers_project() {
    Project p = events_project();
    p.mod.id = "layer_test";
    for (const char* name : {"Layer_Guard", "Layer_Cook"}) {
        PlacedNpc n;
        n.uid = new_uid();
        n.name = name;
        n.template_name = "Peasant_1_01";
        n.position = {100, 0, 100};
        p.npcs.push_back(n);
    }
    WorldSpawn s;
    s.uid = new_uid();
    s.template_name = "It_Apple";
    p.world.spawns.push_back(s);
    ContainerEdit c;
    c.uid = new_uid();
    c.guid = "01234567-89ab-cdef-0123-456789abcdef";
    c.items = {{"It_Apple", 2, false}};
    p.world.containers.push_back(c);
    LayerDef a;
    a.uid = new_uid();
    a.name = "Siege";
    a.enabled = false;
    a.npcs = {"Layer_Guard"};
    a.spawns = {s.uid};
    a.containers = {c.uid};
    json test = event_default_test("chapter");
    test["op"] = ">=";
    test["value"] = 2;
    a.enable_when = json::object({{"mode", "all"}, {"negate", false}, {"tests", json::array({test})}});
    LayerDef b;
    b.uid = new_uid();
    b.name = "Peace";
    b.npcs = {"Layer_Cook"};
    b.disable_when = event_default_condition_set();
    b.disable_when["tests"][0]["name"] = "war";
    p.layers = {a, b};
    // an event graph that switches the layer
    EvBuilder g("Siege switch");
    const std::string t = g.add("on_signal", 0, 0, json::object({{"name", "siege"}}));
    const std::string l = g.add("layer", 300, 0, json::object({{"layer", "Siege"}, {"action", "toggle"}}));
    const std::string sec = g.add("sector", 600, 0, json::object({{"sector", "Monastery_Story_LizardAttack_Off"}, {"action", "enable"}}));
    g.link(t, "next", l);
    g.link(l, "next", sec);
    p.events.push_back(g.g);
    return p;
}

}  // namespace

TEST(layers_export_import_round_trip) {
    const Project p = layers_project();
    for (auto& pr : validate(p, nullptr))
        if (pr.severity == Severity::Error) std::printf("  problem: %s: %s\n", pr.subject.c_str(), pr.message.c_str());
    CHECK_EQ(error_count(validate(p, nullptr)), size_t(0));
    const auto files = render_mod(p);
    CHECK(files.count("data/layers.lua") && files.count("modkit/layers.lua") && files.count("modkit/events.lua"));
    CHECK(files.at("data/npcs.lua").find("layer = \"Siege\"") != std::string::npos);
    CHECK(files.at("data/npcs.lua").find("layer = \"Peace\"") != std::string::npos);
    CHECK(files.at("data/world.lua").find("layer = \"Siege\"") != std::string::npos);
    CHECK(files.at("main.lua").find("layers = require(\"data.layers\")") != std::string::npos);
    const fs::path dir = temp_dir("layers") / "layer_test";
    CHECK(export_mod(p, u8str(dir), ExportOptions{}).ok);
    const ImportResult via_project = import_mod(u8str(dir));
    CHECK(via_project.ok && dump_project(via_project.project) == dump_project(p));
    const ImportResult r = import_mod(u8str(dir), true);
    CHECK(r.ok);
    CHECK_EQ(r.project.layers.size(), size_t(2));
    if (r.project.layers.size() == 2) {
        CHECK(r.project.layers[0].npcs == std::vector<std::string>{"Layer_Guard"});
        CHECK_EQ(r.project.layers[0].spawns.size(), size_t(1));
        CHECK_EQ(r.project.layers[0].containers.size(), size_t(1));
        CHECK(!r.project.layers[0].enabled && r.project.layers[1].enabled);
        CHECK(!r.project.layers[0].enable_when.is_null() && !r.project.layers[1].disable_when.is_null());
    }
    const auto a = render_mod(p), c = render_mod(r.project);
    for (auto& [rel, content] : a) {
        if (rel == "modkit/project.json") continue;
        auto it = c.find(rel);
        CHECK(it != c.end());
        if (it != c.end() && it->second != content) {
            ++g_failures;
            std::printf("  differs after the import: %s\n", rel.c_str());
            write_text(fs::temp_directory_path() / "ly_a.lua", content);
            write_text(fs::temp_directory_path() / "ly_b.lua", it->second);
        }
    }
    // without layers nothing of it is written
    const auto none = render_mod(events_project());
    CHECK(!none.count("data/layers.lua") && !none.count("modkit/layers.lua"));
    CHECK(none.at("main.lua").find("data.layers") == std::string::npos);
}

TEST(layers_validation) {
    Project p = layers_project();
    auto errors = [&](const Project& q) {
        size_t n = 0;
        for (auto& pr : validate(q, nullptr))
            if (pr.severity == Severity::Error && (pr.area == "Layers" || pr.area == "Events")) ++n;
        return n;
    };
    CHECK_EQ(errors(p), size_t(0));
    Project q = p;
    q.layers[1].name = "Siege";  // twice
    CHECK(errors(q) >= 1);
    q = p;
    q.layers[1].npcs = {"Layer_Guard"};  // an NPC in two layers
    CHECK(errors(q) >= 1);
    q = p;
    q.layers[0].npcs = {"Nobody"};
    CHECK(errors(q) >= 1);
    q = p;
    q.layers[0].name = "a b";
    CHECK(errors(q) >= 1);
    q = p;
    q.layers[0].enable_when["tests"] = json::array();  // a condition without checks
    CHECK(errors(q) >= 1);
    q = p;
    q.events[1].graph.nodes[1].data["layer"] = "Missing";  // a graph that names a layer the project does not have
    CHECK(errors(q) >= 1);
}

static const char* kLayersScript = R"LUA(
local failures = {}
local function check(name, cond) if not cond then failures[#failures + 1] = name end end
local function deep(v) if type(v) ~= "table" then return v end local o = {} for k, x in pairs(v) do o[k] = deep(x) end return o end
local handlers, saves, emitted, gv = {}, {}, {}, {}
local chapter = 1
local function fire(ev, ...) for _, fn in ipairs(handlers[ev] or {}) do fn(...) end end
risen = {
    on = function(ev, fn) handlers[ev] = handlers[ev] or {} table.insert(handlers[ev], fn) end,
    emit = function(...) emitted[#emitted + 1] = { ... } fire(...) end,
    clock = function() return 0 end,
    log = { info = function() end, warn = function() end, error = function() end },
    save = { get = function(k) return deep(saves[k]) end, set = function(k, v) saves[k] = deep(v) end, remove = function(k) saves[k] = nil end },
    gamevar = { get = function(n) return gv[n] end, set = function(n, v) gv[n] = v end, remove = function(n) gv[n] = nil end },
    quest = { chapter = function() return chapter end },
    entity = { player = function() return {} end },
    world = { time = function() return 8, 0 end, sector_enabled = function() return false end, set_sector = function() return true end },
    cond = { fn = function(f, o) return { fn = f, on = o and o.on } end },
    loc = { language = function() return "English" end },
}
mod = { id = "t" }
local E = assert(loadstring(EVENTS_SOURCE, "events"))()
local L = assert(loadstring(LAYERS_SOURCE, "layers"))()
local data = assert(loadstring(DATA_SOURCE, "data"))()
local rtm = { quest_action = function() end, cond = function(d) return d end }
rtm.layers = L
E.set_runtime(rtm)
L.install(data.layers, E)
E.install(data.events, rtm)
local function tick(n) for _ = 1, n do fire("tick") end end

check("start state", L.enabled("Siege") == false and L.enabled("Peace") == true and L.enabled("Nope") == false)
check("a layer condition object", L.cond("Siege").on == "modkit.layer_changed" and L.cond("Siege").fn() == false)
tick(40)
check("nothing yet", L.enabled("Siege") == false)
chapter = 2
tick(31)
check("enable_when switched it on", L.enabled("Siege") == true)
check("announced", emitted[#emitted][1] == "modkit.layer_changed" and emitted[#emitted][2] == "Siege" and emitted[#emitted][3] == true)
check("once: the fired flag is saved", saves["modkit.layer.Siege.on"] == true)
check("set(false) is a change", L.set("Siege", false) == true and L.enabled("Siege") == false)
check("set(false) again is none", L.set("Siege", false) == false)
chapter = 3
tick(61)
check("the condition fires only once per savegame", L.enabled("Siege") == false)
gv.war = 1
tick(31)
check("disable_when switched it off", L.enabled("Peace") == false)
-- the event graph's Layer node toggles; a quick load rolls the state back
local snap = deep(saves)
E.fire_signal("siege")
tick(2)
check("the Layer node toggled it on", L.enabled("Siege") == true)
saves = snap
check("a load rolls it back", L.enabled("Siege") == false)
E.fire_signal("siege")
tick(2)
check("toggled again after the load", L.enabled("Siege") == true)
return table.concat(failures, "; ")
)LUA";

TEST(layers_runtime_switches_by_condition_and_graph) {
    Project p = layers_project();
    lua_State* L = luaL_newstate();
    luaL_openlibs(L);
    auto set = [&](const char* name, const std::string& v) {
        lua_pushlstring(L, v.data(), v.size());
        lua_setglobal(L, name);
    };
    set("EVENTS_SOURCE", events_lua());
    set("LAYERS_SOURCE", layers_lua());
    json d = json::object();
    d["layers"] = layers_data(p);
    d["events"] = events_data(p);
    set("DATA_SOURCE", "return " + lua_value(d, 0, 60));
    std::string result;
    if (luaL_loadstring(L, kLayersScript) != 0 || lua_pcall(L, 0, 1, 0) != 0) result = std::string("lua error: ") + lua_tostring(L, -1);
    else result = lua_tostring(L, -1) ? lua_tostring(L, -1) : "";
    lua_close(L);
    if (!result.empty()) std::printf("  layers runtime test failed: %s\n", result.c_str());
    CHECK(result.empty());
}
