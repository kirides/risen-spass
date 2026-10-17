// ---- localized option texts: "@id" string ids in risen.options, the manager's own texts, the CSV files of modmanager and quickloot ---------------------------------
// Included by test_main.cpp inside its anonymous namespace, after options_tests.inc.

namespace {

// "id;text" lines of a strings CSV (the layer's own parser has more features; the shipped files only use quoted texts), by id
std::map<std::string, std::string> readStrings(const fs::path& file) {
    std::map<std::string, std::string> out;
    std::ifstream f(file, std::ios::binary);
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#' || line.rfind("id;text", 0) == 0) continue;
        const auto semi = line.find(';');
        if (semi == std::string::npos) continue;
        std::string text = line.substr(semi + 1);
        if (text.size() >= 2 && text.front() == '"' && text.back() == '"') text = text.substr(1, text.size() - 2);
        out[line.substr(0, semi)] = text;
    }
    return out;
}

std::map<std::string, std::string>& fakeStrings() {
    static std::map<std::string, std::string> m;
    return m;
}

}  // namespace

TEST(options_texts_given_as_string_ids_are_looked_up_when_listed) {
    TempDir t;
    auto& rt = spass::lua::Runtime::instance();
    rt.addBinding(spass::game::bindOptions);
    rt.addTeardown(spass::game::optionsReleaseOwner);
    rt.addBinding([](sol::state_view lua, sol::table risen) {   // a stand-in for risen.loc (the real one needs the engine)
        sol::table loc = lua.create_table();
        loc.set_function("get", [](const std::string& id) -> sol::optional<std::string> {
            const auto it = fakeStrings().find(id);
            if (it == fakeStrings().end()) return sol::nullopt;
            return it->second;
        });
        risen["loc"] = loc;
    });
    fakeStrings() = { { "T_Title", "Titel" }, { "T_Group", "Gruppe" }, { "T_Name", "Taste" }, { "T_Desc", "Beschreibung" }, { "T_Mode_a", "Alpha" }, { "SPASS_Opt_On", "an" } };
    t.write("mods/txt/mod.toml", toml("txt"));
    t.write("mods/txt/main.lua", R"LUA(
        local def, why = risen.options.define{
            title = "@T_Title",
            { id = "on", name = "@T_Name", description = "@T_Desc", group = "@T_Group", value = true },
            { id = "mode", name = "@T_Missing", description = "@@literal", type = "string", values = { { value = "a", name = "@T_Mode_a" }, { value = "b", name = "@T_Mode_b" }, "c" }, value = "a" },
            { id = "plain", name = "Plain text", value = false },
        }
        risen.on("t.view", function() if not def then return tostring(why) end local ok, r = pcall(function()
            local item = risen.options.list()[1]
            local e = item.entries
            return table.concat({ item.title, e[1].name, e[1].description, e[1].group, e[1].text, e[2].name, e[2].description, e[2].text, e[2].values[2].name, e[2].values[3].name, e[3].name, e[3].text }, "|") end) return tostring(r)
        end)
    )LUA");
    CHECK(rt.start(t.path / "mods", false, t.path / "mods.toml"));
    CHECK(probe(rt, "t.view") == "Titel|Taste|Beschreibung|Gruppe|an|T_Missing|@literal|Alpha|T_Mode_b|c|Plain text|off");
    // the language changes while the game runs: the next listing follows (ids are looked up, not copied)
    fakeStrings()["T_Name"] = "Key";
    fakeStrings()["SPASS_Opt_On"] = "on!";
    CHECK(probe(rt, "t.view").find("|Key|") != std::string::npos && probe(rt, "t.view").find("|on!|") != std::string::npos);
    rt.stop();
}

TEST(modmanager_model_texts_come_from_the_host_and_fall_back_to_english) {
    sol::state lua;
    lua.open_libraries(sol::lib::base, sol::lib::string, sol::lib::table, sol::lib::math, sol::lib::package);
    std::ifstream f(fs::path(SPASS_SOURCE_DIR) / "mods" / "modmanager" / "model.lua", std::ios::binary);
    const std::string src((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    sol::protected_function_result loaded = lua.safe_script(src, sol::script_pass_on_error);
    CHECK(loaded.valid());
    lua.set("Model", loaded.get<sol::table>());
    const auto german = readStrings(fs::path(SPASS_SOURCE_DIR) / "mods" / "modmanager" / "strings" / "German.csv");
    sol::table strings = lua.create_table();
    for (const auto& [k, v] : german) strings[k] = v;
    lua.set("GERMAN", strings);
    lua.script(R"LUA(
        local function mod(id) return { id = id, name = id, version = "1", state = "loaded", enabled = true, requires = {}, load_after = {}, needed_by = {}, reloadable = true,
            restart_required = {}, data = { present = false, keys = {} }, load_count = 1 } end
        local mods = {}
        function mods.list() return { mod("opt") } end
        function mods.order() return { current = { "opt" }, planned = { "opt" }, automatic = { "opt" }, custom = {} } end
        local entries = {
            { id = "n", name = "Zahl", kind = "slider", value = 100, default = 50, text = "100", default_text = "50", min = 0, max = 100, step = 25, changed = true, description = "" },
            { id = "k", name = "Taste", kind = "dropdown", value = "a", default = "a", text = "a", default_text = "a", values = { { value = "a", name = "a" } }, index = 1, changed = false, description = "", restart = true },
        }
        local options = {}
        function options.list() return { { mod = "opt", entries = entries } } end
        function options.step() return true, { changed = true, value = 1, restart = true } end
        function options.reset() return true, { changed = false, count = 0 } end
        local function snap(host)
            local m = Model.new(host, { rows = 5 })
            m:select("opt")
            m:press("options")
            return m
        end
        result = {}
        -- no text function: the English fallback
        local en = snap({ mods = mods, options = options, version = "1" })
        local s = en:snapshot()
        result.en = table.concat({ s.title, s.summary, s.prev_label, s.next_label, s.buttons.up.label, s.buttons.reset.label, s.hint }, "|")
        -- the German strings of the shipped file
        local de = snap({ mods = mods, options = options, version = "1", text = function(id) return GERMAN[id] end })
        s = de:snapshot()
        result.de = table.concat({ s.title, s.summary, s.prev_label, s.next_label, s.buttons.up.label, s.buttons.reset.label, s.hint }, "|")
        de:select("k")
        de:press("toggle")
        result.de_msg = de.message.text
        de:select("n")
        result.de_max = tostring(de:press("toggle")) .. ":" .. de.message.text
        -- a host whose text function fails or lacks an id: English again, nothing raises
        local broken = snap({ mods = mods, options = options, version = "1", text = function(id) error("boom") end })
        result.broken = broken:snapshot().buttons.down.label
        local partial = snap({ mods = mods, options = options, version = "1", text = function(id) if id == "SPASS_ModMgr_opt_default" then return "Standard!" end end })
        result.partial = partial:snapshot().buttons.up.label .. "|" .. partial:snapshot().buttons.down.label
    )LUA");
    sol::table r = lua["result"];
    const auto get = [&](const char* k) { sol::object o = r[k]; return o.is<std::string>() ? o.as<std::string>() : std::string("<missing ") + k + ">"; };
    CHECK(get("en") == "Options: opt|2 options, 1 changed|< Prev|Next >|Default value|Back to the mods|Default: 50 (0 to 100, step 25).");
    CHECK(get("de") == "Optionen: opt|2 Optionen, 1 geändert|< Vorige|Nächste >|Standardwert|Zurück zu den Mods|Standard: 50 (0 bis 100, Schritt 25).");
    CHECK(get("de_msg") == "Taste: a. Gilt ab dem nächsten Spielstart.");
    CHECK(get("de_max") == "false:Zahl ist am Maximum.");
    CHECK(get("broken") == "All defaults");
    CHECK(get("partial") == "Standard!|All defaults");
}

TEST(string_files_of_the_options_pages_are_complete_in_every_language) {
    const fs::path mods = fs::path(SPASS_SOURCE_DIR) / "mods";
    for (const char* mod : {"modmanager", "quickloot"}) {
        const auto english = readStrings(mods / mod / "strings" / "all.csv");
        const auto german = readStrings(mods / mod / "strings" / "German.csv");
        CHECK(!english.empty());
        for (const auto& [id, text] : english) {
            CHECK(german.count(id) == 1);                                            // every English text has a German one ...
            if (german.count(id) == 1) {
                const auto count = [](const std::string& s, char c) { return std::count(s.begin(), s.end(), c); };
                CHECK(count(german.at(id), '%') == count(text, '%'));               // ... with the same format fields
            }
        }
        for (const auto& [id, text] : german) CHECK(english.count(id) == 1);        // and nothing else
    }
    // every key of the model's TEXT table and every "@id" of quickloot has a string
    const auto manager = readStrings(mods / "modmanager" / "strings" / "all.csv");
    const std::string model = slurp(mods / "modmanager" / "model.lua");
    const auto from = model.find("local TEXT = {");
    const auto to = model.find("\n}\n", from);
    CHECK(from != std::string::npos && to != std::string::npos);
    int keys = 0;
    std::istringstream table(model.substr(from, to - from));
    for (std::string line; std::getline(table, line);) {
        const auto eq = line.find(" = \"");
        if (line.rfind("    ", 0) != 0 || eq == std::string::npos) continue;
        ++keys;
        CHECK(manager.count("SPASS_ModMgr_" + line.substr(4, eq - 4)) == 1);
    }
    CHECK(keys >= 30);
    const auto quick = readStrings(mods / "quickloot" / "strings" / "all.csv");
    const std::string main = slurp(mods / "quickloot" / "main.lua");
    for (const char* id : {"enabled", "key", "modifier", "items", "corpses", "chests", "keys", "max_distance", "print", "pickup_sound", "pickup_sound_gap", "hold_repeat", "repeat_ms", "witness_mode", "witness_veto"}) {
        CHECK(quick.count(std::string("SPASS_QL_") + id) == 1);
        CHECK(quick.count(std::string("SPASS_QL_") + id + "_desc") == 1);
        CHECK(main.find(std::string("entry(\"") + id + "\"") != std::string::npos);
    }
    for (const char* id : {"title", "group_general", "group_take", "group_feedback", "group_repeat", "group_witnesses", "key_SecondaryAction", "key_PrimaryAction", "key_Selection", "key_MButton",
                           "key_XButton1", "key_XButton2", "mod_none", "mod_shift", "mod_ctrl", "mod_alt", "mode_assess", "mode_veto", "mode_ignore"}) {
        CHECK(quick.count(std::string("SPASS_QL_") + id) == 1);
    }
}
