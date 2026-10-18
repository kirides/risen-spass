// Tests of what the editor keeps besides its panels' objects: requirement / modifier order and modes, free string rows, template patches, edits of shipped
// dialogue infos, teaching limits (model, project file, export, import from the data files, validation, the generated runtime against a stand-in API).
// Included by modkit_tests.cpp.

static Project extras_project() {
    Project p;
    p.mod.id = "extras_test";
    p.mod.languages = {"English", "German"};
    ItemDef a;
    a.uid = "i1";
    a.mode = Mode::Edit;
    a.name = "It_Ring_Test";
    set_requirement_entries(a, {{"Strength", 20}, {"Dexterity", 10}, {"Axe", 3}});  // not alphabetical: the order has to be kept
    set_modifier_entries(a, {{"HitPoints", 50, "set"}, {"ProtEdge", 5, "add"}, {"Strength", 10, "percent"}});
    p.items.push_back(a);
    ItemDef plain;
    plain.uid = "i2";
    plain.mode = Mode::Edit;
    plain.name = "It_Plain_Test";
    set_requirement_entries(plain, {{"Dexterity", 10}, {"Strength", 20}});  // alphabetical, all add: stays a plain map
    set_modifier_entries(plain, {{"HitPoints", 5, "add"}});
    p.items.push_back(plain);
    p.strings.push_back({"s1", "HUD_Test_Button", {{"English", "Quest map"}, {"German", "Questkarte"}}, ""});
    p.strings.push_back({"s2", "DOC_Test", {{"English", "line one\r\nline two"}}, "a note"});
    p.strings.push_back({"s3", "ONLY_German", {{"German", "Nur deutsch"}}, ""});
    TemplatePatch t;
    t.uid = "t1";
    t.template_name = "It_Cast_Light";
    t.properties["eCPointLight_PS"]["Range"] = 1500;
    t.properties["eCPointLight_PS"]["Color"] = "1; 0.5; 0";
    p.patches.push_back(t);
    DialogEdit e;
    e.uid = "d1";
    e.info = "FILLERX2_00029";
    e.xp_add = 40;
    p.dialog_edits.push_back(e);
    DialogEdit e2;
    e2.uid = "d2";
    e2.info = "FILLERX2_00792";
    e2.xp_old = 10;
    e2.xp_new = 25;
    e2.chapter = 1;
    e2.refills = {{"Oscar", "It_Axe_Test", 2}, {"Cyrus", "It_Staff_Test", 1}};
    p.dialog_edits.push_back(e2);
    TeacherLimit tl;
    tl.uid = "l1";
    tl.guid = "11111111-2222-3333-4444-555555555555";
    tl.entity_name = "Sam";
    tl.skill = "Dexterity";
    tl.max = 150;
    p.teachers.push_back(tl);
    return p;
}

TEST(skill_lists_keep_order_and_modes) {
    Project p = extras_project();
    const ItemDef& a = p.items[0];
    CHECK((a.requirement_order == std::vector<std::string>{"Strength", "Dexterity", "Axe"}));
    CHECK(a.modifier_modes.size() == 2 && a.modifier_modes.at("HitPoints") == "set" && a.modifier_modes.at("Strength") == "percent" && !a.modifier_modes.count("ProtEdge"));
    const auto req = requirement_entries(a);
    CHECK(req.size() == 3 && req[0].skill == "Strength" && req[1].skill == "Dexterity" && req[2].skill == "Axe" && req[0].amount == 20);
    const auto mod = modifier_entries(a);
    CHECK(mod.size() == 3 && mod[0].skill == "HitPoints" && mod[0].mode == "set" && mod[1].mode == "add" && mod[2].mode == "percent");
    // an alphabetical list of plain additions needs no extra data: the export keeps writing a plain table
    CHECK(p.items[1].requirement_order.empty() && p.items[1].modifier_order.empty() && p.items[1].modifier_modes.empty());
    // the export: lists with the engine's order and the modes where they matter, plain tables otherwise
    const json b = balance_spec(p);
    const json& ring = b["items"]["It_Ring_Test"];
    CHECK(ring["requires"].is_array() && ring["requires"][0]["skill"] == "Strength" && ring["requires"][2]["skill"] == "Axe");
    CHECK(ring["modifies"].is_array() && ring["modifies"][0]["mode"] == "set" && !ring["modifies"][1].contains("mode") && ring["modifies"][2]["mode"] == "percent");
    CHECK(b["items"]["It_Plain_Test"]["requires"].is_object() && b["items"]["It_Plain_Test"]["modifies"].is_object());
    // the project file
    const std::string text = dump_project(p);
    std::vector<std::string> warnings;
    const Project q = project_from_json(json::parse(text), &warnings);
    CHECK(warnings.empty());
    CHECK_EQ(dump_project(q), text);
    CHECK(q.items[0].requirement_order == a.requirement_order && q.items[0].modifier_modes == a.modifier_modes);
    // editing a row (the panel's way: entries in, entries out)
    Project r = p;
    auto m2 = modifier_entries(r.items[0]);
    m2[1].mode = "max";
    m2.erase(m2.begin());
    set_modifier_entries(r.items[0], m2);
    CHECK(r.items[0].modifies->size() == 2 && !r.items[0].modifier_modes.count("HitPoints") && r.items[0].modifier_modes.at("ProtEdge") == "max");
    // validation: an unknown mode
    r.items[0].modifier_modes["Strength"] = "multiply";
    bool unknown = false;
    for (auto& x : validate(r, nullptr)) unknown = unknown || x.message.find("unknown mode 'multiply'") != std::string::npos;
    CHECK(unknown);
}

TEST(extras_export_and_import_from_data_files) {
    const Project p = extras_project();
    const fs::path dir = temp_dir("extras") / "m";
    ExportOptions o;
    CHECK(export_mod(p, u8str(dir), o).ok);
    const auto files = render_mod(p);
    CHECK(files.count("data/patches.lua") == 1 && files.count("data/dialog_edits.lua") == 1);
    CHECK(files.at("main.lua").find("patches = require(\"data.patches\")") != std::string::npos && files.at("main.lua").find("dialog_edits = require(\"data.dialog_edits\")") != std::string::npos);
    // the showcase has none of them: its main.lua and data files are unchanged (the golden test checks the files themselves)
    CHECK(render_mod(load_showcase()).count("data/patches.lua") == 0);
    const json w = world_data(p);
    CHECK(w["teachers"].size() == 1 && w["teachers"][0]["skill"] == "Dexterity" && w["teachers"][0]["max"] == 150);
    // texts: free rows next to the objects' own; \r\n kept; a row without the first language stays in its own file
    const std::string all = files.at("strings/all.csv"), german = files.at("strings/German.csv");
    CHECK(all.find("HUD_Test_Button;\"Quest map\"") != std::string::npos && all.find("DOC_Test;\"line one\r\nline two\"") != std::string::npos);
    CHECK(all.find("ONLY_German") == std::string::npos && german.find("ONLY_German;\"Nur deutsch\"") != std::string::npos && german.find("HUD_Test_Button;\"Questkarte\"") != std::string::npos);

    const ImportResult r = import_mod(u8str(dir), true);
    for (auto& x : r.warnings) std::printf("  import: %s\n", x.c_str());
    CHECK(r.ok);
    if (!r.ok) return;
    const Project& q = r.project;
    CHECK(q.patches.size() == 1 && q.patches[0].template_name == "It_Cast_Light" && q.patches[0].properties.at("eCPointLight_PS").at("Range") == 1500);
    CHECK(q.dialog_edits.size() == 2 && q.dialog_edits[0].xp_add == 40 && q.dialog_edits[1].xp_old == 10 && q.dialog_edits[1].xp_new == 25 && q.dialog_edits[1].chapter == 1 &&
          q.dialog_edits[1].refills.size() == 2 && q.dialog_edits[1].refills[0].trader == "Oscar" && q.dialog_edits[1].refills[1].count == 1);
    CHECK(q.teachers.size() == 1 && q.teachers[0].guid == p.teachers[0].guid && q.teachers[0].skill == "Dexterity" && q.teachers[0].max == 150);
    CHECK(q.strings.size() == 3);
    for (auto& s : q.strings) {
        if (s.id == "DOC_Test") CHECK(s.text.at("English") == "line one\r\nline two");
        if (s.id == "ONLY_German") CHECK(s.text.size() == 1 && s.text.count("German") == 1);
        if (s.id == "HUD_Test_Button") CHECK(s.text.at("German") == "Questkarte");
    }
    // the lists with their order and modes
    const ItemDef* ring = nullptr;
    for (auto& i : q.items)
        if (i.name == "It_Ring_Test") ring = &i;
    CHECK(ring != nullptr);
    if (ring) {
        CHECK((ring->requirement_order == p.items[0].requirement_order) && ring->modifier_modes == p.items[0].modifier_modes && ring->modifier_order == p.items[0].modifier_order);
        CHECK(*ring->requirements == *p.items[0].requirements && *ring->modifies == *p.items[0].modifies);
    }
    // export -> import -> export: the generated files are the same
    const auto again = render_mod(q);
    for (const char* f : {"data/patches.lua", "data/dialog_edits.lua", "data/world.lua", "data/balance.lua", "strings/all.csv", "strings/German.csv"}) {
        CHECK(again.count(f) == 1);
        if (again.count(f) && files.count(f) && again.at(f) != files.at(f)) std::printf("  differs after the import: %s\n", f);
        CHECK(again.count(f) && again.at(f) == files.at(f));
    }
}

TEST(extras_validation) {
    Project p = extras_project();
    auto messages = [&](const Project& x) {
        std::string all;
        for (auto& v : validate(x, nullptr)) all += std::string(v.severity == Severity::Error ? "E " : "W ") + v.area + ": " + v.message + "\n";
        return all;
    };
    CHECK(messages(p).find("E ") == std::string::npos);  // the made-up project is fine without game data
    // the German-only row is a warning: no text in the fallback language
    CHECK(messages(p).find("no text in English") != std::string::npos);
    Project q = p;
    q.strings.push_back({"s4", "hud_test_button", {{"English", "x"}}, ""});
    q.strings.push_back({"s5", "", {{"English", "x"}}, ""});
    q.strings.push_back({"s6", "A;B", {{"English", "x"}}, ""});
    std::string m = messages(q);
    CHECK(m.find("twice") != std::string::npos && m.find("needs an id") != std::string::npos && m.find("must not contain") != std::string::npos);
    q = p;
    q.patches.push_back({"t2", "", {}, ""});
    q.patches.push_back({"t3", "It_Cast_Light", {}, ""});
    m = messages(q);
    CHECK(m.find("choose the template to patch") != std::string::npos && m.find("patched twice") != std::string::npos && m.find("changes nothing") != std::string::npos);
    q = p;
    q.dialog_edits[0].xp_old = 5;  // old without new, and added together with changed
    q.dialog_edits.push_back({"d3", "", std::nullopt, std::nullopt, std::nullopt, {{"", "", -1}}, std::nullopt, ""});
    m = messages(q);
    CHECK(m.find("either added or changed") != std::string::npos && m.find("old and the new amount") != std::string::npos && m.find("choose the shipped dialogue info") != std::string::npos &&
          m.find("needs the trader's name") != std::string::npos && m.find("must not be negative") != std::string::npos);
    q = p;
    q.teachers[0].guid = "short";
    q.teachers[0].skill = "Strenght";
    m = messages(q);
    CHECK(m.find("entity ID is not") != std::string::npos);
}

// the generated runtime against a stand-in for the layer's API: template patches (a bad property costs only itself), the dialogue edits on shipped infos, the new
// game's chapter 1 stock, the teaching limits
static const char* kExtrasScript = R"LUA(
local failures = {}
local function check(name, cond) if not cond then failures[#failures + 1] = name end end
local handlers, patches, logs, calls, teach, scripts, spawned = {}, {}, {}, {}, {}, {}, {}
local gamevars = {}
local function new_info(name)
    local inf = { name = name, commands = {} }
    function inf:add_command(kind) self.commands[#self.commands + 1] = { kind = kind, props = {} } return #self.commands end
    function inf:add_script_command() self.commands[#self.commands + 1] = { kind = "RunScript", props = {} } return #self.commands end
    function inf:set_script(i, s) self.commands[i].script = s return true end
    function inf:set_command_property(i, k, v) self.commands[i].props[k] = v return true end
    function inf:get_command_property(i, k) return self.commands[i].props[k] end
    function inf:command_count() return #self.commands end
    return inf
end
local infos = { FILLERX2_00029 = new_info("FILLERX2_00029"), FILLERX2_00792 = new_info("FILLERX2_00792") }
infos.FILLERX2_00792.commands[1] = { kind = "GiveXP", props = { Amount = "10" } }
local player = { position = function() return 0, 0, 0 end }
local traders = { Oscar = {}, Cyrus = {} }
risen = {
    on = function(ev, fn) handlers[ev] = handlers[ev] or {} handlers[ev][#handlers[ev] + 1] = fn end,
    emit = function() end,
    log = { info = function(m) logs[#logs + 1] = "I " .. m end, warn = function(m) logs[#logs + 1] = "W " .. m end, error = function(m) logs[#logs + 1] = "E " .. m end },
    template = { patch = function(name, props, opts)
        patches[#patches + 1] = { name = name, props = props, instances = opts and opts.instances }
        for set, list in pairs(props) do for k in pairs(list) do if k == "Bad" then return nil, "no such property" end end end
        return true
    end },
    dialog = { get = function(name) return infos[name] end },
    entity = { player = function() return player end, get = function(n) return traders[n] end,
               spawn = function(item) local e = { item = item, kill = function(self) self.dead = true end } spawned[#spawned + 1] = e return e end,
               by_id = function(id) return id == "11111111-2222-3333-4444-555555555555" and { id = id } or nil end },
    script = { call = function(name, trader, e, n) calls[#calls + 1] = { name = name, item = e.item, n = n } end },
    gamevar = { get = function(k) return gamevars[k] end, set = function(k, v) gamevars[k] = v end },
    balance = { skill_enum = function(n) return ({ Dexterity = "gESkill_Stat_DEX" })[n] end },
    npc = { set_teach_limit = function(e, skill, max) teach[#teach + 1] = { id = e.id, skill = skill, max = max } return true end },
    world = {},
}
mod = { id = "extras_test" }
local function fire(ev, ...) for _, fn in ipairs(handlers[ev] or {}) do fn(...) end end
local M = assert(loadstring(RUNTIME_SOURCE, "runtime"))()
check("version", M.VERSION >= 11)
M.install({
    patches = { { template = "It_Cast_Light", properties = { eCPointLight_PS = { Range = 1500 } } },
                { template = "It_Mixed", properties = { gCItem_PS = { Bad = 1, SortValue = 5 } } } },
    dialog_edits = { { info = "FILLERX2_00029", xp_add = 40 },
                     { info = "FILLERX2_00792", xp_old = 10, xp_new = 25, chapter = 1, refills = { { trader = "Oscar", item = "It_Axe_Test", count = 2 }, { trader = "Cyrus", item = "It_Staff_Test", count = 1 } } },
                     { info = "NO_SUCH_INFO", xp_add = 1 } },
    world = { teachers = { { id = "11111111-2222-3333-4444-555555555555", name = "Sam", skill = "Dexterity", max = 150 },
                           { id = "99999999-2222-3333-4444-555555555555", name = "Ghost", skill = "Dexterity", max = 150 },
                           { id = "11111111-2222-3333-4444-555555555555", name = "Sam", skill = "Nonsense", max = 10 } } },
})
fire("tick")
-- patches: the whole set at once; the mixed one falls back to single properties (the good one is written, the bad one warned about)
check("first tick patches written", #patches >= 2 and patches[1].name == "It_Cast_Light" and patches[1].props.eCPointLight_PS.Range == 1500 and patches[1].instances == false)
local single, bad_warned = 0, false
for _, p in ipairs(patches) do if p.name == "It_Mixed" and p.props.gCItem_PS.SortValue == 5 and not p.props.gCItem_PS.Bad then single = single + 1 end end
for _, l in ipairs(logs) do if l:find("W ") and l:find("It_Mixed.gCItem_PS.Bad") then bad_warned = true end end
check("the good property of a mixed patch is written alone", single == 1)
check("the bad one is warned about", bad_warned)
-- dialogue edits
local c29, c792 = infos.FILLERX2_00029.commands, infos.FILLERX2_00792.commands
check("XP reward added", #c29 == 1 and c29[1].kind == "GiveXP" and c29[1].props.Amount == "40")
check("XP reward changed", c792[1].props.Amount == "25")
check("refills added as RunScript commands", #c792 == 3 and c792[2].script == "OnTradeInfo_RefillTradeItems" and c792[2].props.Self == "Oscar" and c792[2].props.Other == "It_Axe_Test" and
      c792[2].props.OtherType == "gEOtherType_TemplateEntity" and c792[2].props.Param == "2" and c792[3].props.Other == "It_Staff_Test")
local missing = false
for _, l in ipairs(logs) do if l:find("W ") and l:find("NO_SUCH_INFO") then missing = true end end
check("an unknown info is warned about, the rest still applies", missing)
fire("tick")
check("dialogue edits only once", #c29 == 1 and #c792 == 3)
-- the session start writes the live entities again
local before = #patches
fire("session.start", "new")
check("session.start patches with instances", #patches > before and patches[#patches].instances == true)
-- chapter 1's stock when a new game starts (once per savegame)
fire("world.ready", "new")
check("stock refilled through the script", #calls == 2 and calls[1].name == "OnTradeInfo_RefillTradeItems" and calls[1].item == "It_Axe_Test" and calls[1].n == 2 and calls[2].item == "It_Staff_Test")
check("the helper entities are removed again", #spawned == 2 and spawned[1].dead and spawned[2].dead)
fire("world.ready", "new")
check("the stock is handed out once", #calls == 2)
-- teaching limits on every world.ready; the unknown NPC and the unknown skill are warned about once
check("teaching limit written", #teach >= 1 and teach[1].id == "11111111-2222-3333-4444-555555555555" and teach[1].skill == "gESkill_Stat_DEX" and teach[1].max == 150)
local n = #teach
fire("world.ready", "load")
check("applied again on every world.ready", #teach == n + 1)
local ghost, nonsense = 0, 0
for _, l in ipairs(logs) do
    if l:find("W ") and l:find("Ghost") then ghost = ghost + 1 end
    if l:find("W ") and l:find("unknown skill") then nonsense = nonsense + 1 end
end
check("warned once per problem", ghost == 1 and nonsense == 1)
for _, l in ipairs(logs) do if l:find("E ") then failures[#failures + 1] = "log: " .. l end end
return table.concat(failures, "; ")
)LUA";

TEST(extras_run_in_the_runtime) {
    lua_State* L = luaL_newstate();
    luaL_openlibs(L);
    const std::string runtime = runtime_lua();
    lua_pushlstring(L, runtime.data(), runtime.size());
    lua_setglobal(L, "RUNTIME_SOURCE");
    std::string result;
    if (luaL_loadstring(L, kExtrasScript) != 0 || lua_pcall(L, 0, 1, 0) != 0) result = std::string("lua error: ") + lua_tostring(L, -1);
    else result = lua_tostring(L, -1) ? lua_tostring(L, -1) : "";
    lua_close(L);
    if (!result.empty()) std::printf("  runtime test failed: %s\n", result.c_str());
    CHECK(result.empty());
}

TEST(text_tables_carry_the_fallback) {
    Text t = {{"English", "Sword"}, {"German", "Schwert"}};
    const json v = text_value_export(t, {"English", "German", "French"});
    CHECK(v.is_object() && v["English"] == "Sword" && v["German"] == "Schwert" && v["default"] == "Sword");
    CHECK(text_value_export({{"English", "Only"}}, {"English", "German"}) == json("Only"));
    // a text only in another language applies to that language alone
    const json g = text_value_export({{"German", "Nur"}}, {"English", "German"});
    CHECK(g.is_object() && g["German"] == "Nur" && !g.contains("default"));
    CHECK(text_value_export({{"German", "Nur"}}, {"German"}) == json("Nur"));
}

TEST(export_golden_extras) {
    // mods/modkit_extras is the committed export of examples/modkit/extras.rmk.json (the live scenario `extras` runs it): re-export with
    // `risen-modkit examples/modkit/extras.rmk.json --export mods/modkit_extras` after a change of the exporter
    const Project p = load_project_file(std::string(MODKIT_SOURCE_DIR) + "/examples/modkit/extras.rmk.json");
    const auto files = render_mod(p);
    const fs::path dir = fs::path(MODKIT_SOURCE_DIR) / "mods" / "modkit_extras";
    for (auto& [rel, content] : files) {
        const fs::path f = dir / u8(rel);
        if (!fs::exists(f)) {
            ++g_failures;
            std::printf("  golden file missing: %s\n", rel.c_str());
            continue;
        }
        if (lf(read_text(f)) != lf(content)) {
            ++g_failures;
            std::printf("  golden file differs: %s (re-export the extras example)\n", rel.c_str());
        }
        ++g_checks;
    }
    CHECK(files.count("data/patches.lua") == 1 && files.count("data/dialog_edits.lua") == 1);
    // the project is valid
    for (auto& x : validate(p, nullptr))
        if (x.severity == Severity::Error) std::printf("  problem: %s %s\n", x.subject.c_str(), x.message.c_str());
    CHECK_EQ(error_count(validate(p, nullptr)), size_t(0));
}
