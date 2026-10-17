// ---- risen.options: the options a mod declares for the in-game menu of the mod manager (src/game/lua_options.cpp) -----------------------------------------------
// Included by test_main.cpp inside its anonymous namespace, after mods_tests.inc (`probe`, `toml`).

namespace {

std::string slurp(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

}  // namespace

TEST(options_define_check_step_store_and_forget) {
    TempDir t;
    auto& rt = spass::lua::Runtime::instance();
    rt.addBinding(spass::game::bindOptions);
    rt.addTeardown(spass::game::optionsReleaseOwner);
    t.write("mods/optmod/mod.toml", toml("optmod"));
    t.write("mods/optmod/main.lua", R"LUA(
        local log = {}
        local spec, why = risen.options.define{
            title = "Opts",
            { id = "on", name = "On", value = true },
            { id = "mode", name = "Mode", type = "string", values = { "a", "b", "c" }, value = "a" },
            { id = "n", name = "N", type = "number", min = 0, max = 100, step = 25, value = 50 },
            { id = "key", name = "Key", type = "string", values = { SecondaryAction = "Right mouse button", MButton = "Middle" }, default = "MButton", value = "MButton" },
            on_change = function(id, v, old) log[#log + 1] = id .. "=" .. tostring(v) .. "<" .. tostring(old) end,
        }
        local function row(...) return table.concat({ ... }, " ") end
        risen.on("t.values", function() return row(tostring(spec and spec[1].value), spec[2].value, spec[3].value, spec[4].value) end)
        risen.on("t.run1", function()
            local out = {}
            local ok, rep = risen.options.set("optmod", "on", false)
            out[#out + 1] = row("set", tostring(ok), tostring(rep.changed), tostring(spec[1].value))
            local a, b = risen.options.set("optmod", "on", "x")
            out[#out + 1] = row("badtype", tostring(a), (b:find("must be a boolean", 1, true)) and "reason" or b)
            a, b = risen.options.set("optmod", "mode", "z")
            out[#out + 1] = row("notlisted", tostring(a), (b:find("not one of the values", 1, true)) and "reason" or b)
            local seq = {}
            for _, dir in ipairs({ 1, 1, 1, -1 }) do risen.options.step("optmod", "mode", dir) seq[#seq + 1] = spec[2].value end
            out[#out + 1] = "mode " .. table.concat(seq, ",")
            ok, rep = risen.options.set("optmod", "n", 60)
            out[#out + 1] = row("snap", tostring(spec[3].value))
            seq = {}
            for _ = 1, 4 do risen.options.step("optmod", "n", 1) seq[#seq + 1] = spec[3].value end
            out[#out + 1] = "slider " .. table.concat(seq, ",")
            a, b = risen.options.set("optmod", "n", 200)
            out[#out + 1] = row("range", tostring(a), (b:find("between", 1, true)) and "reason" or b)
            out[#out + 1] = row("unknown", tostring((risen.options.get("optmod", "nope"))), tostring((risen.options.get("ghost", "on"))))
            out[#out + 1] = "log " .. table.concat(log, ";")
            local list = risen.options.list()
            local e = list[1].entries
            out[#out + 1] = row("list", #list, list[1].mod, list[1].title, #e, e[4].text, e[4].default_text, e[4].values[1].name, tostring(e[1].changed), e[1].text)
            return table.concat(out, "|")
        end)
        risen.on("t.run2", function()
            local ok, rep = risen.options.reset("optmod", "n")
            local ok2, all = risen.options.reset("optmod")
            return row(tostring(ok), tostring(rep.value), tostring(ok2), tostring(all.count), tostring(spec[1].value), spec[2].value, tostring(spec[3].value))
        end)
        risen.on("t.bad", function()
            local out = {}
            local function try(s) local a, b = risen.options.define(s) out[#out + 1] = a == nil and b or "ok" end
            try(5)
            try({})
            try({ { id = "bad id", value = 1 } })
            try({ { id = "a", value = 1 }, { id = "a", value = 2 } })
            try({ { id = "a", type = "string", kind = "dropdown", value = "x" } })
            try({ { id = "a", type = "number", kind = "slider", value = 1 } })
            try({ { id = "a", value = true }, on_change = 5 })
            try({ { id = "a", type = "boolean", value = "yes" } })
            try({ entries = { { id = "a", value = true } } })
            return table.concat(out, "|")
        end)
    )LUA");
    t.write("mods/other/mod.toml", toml("other"));
    t.write("mods/other/main.lua", "risen.on('t.count', function() return tostring(#risen.options.list()) end)\n");
    const auto cfgFile = t.path / "mods.toml";
    const auto store = t.path / "options" / "optmod.toml";
    CHECK(rt.start(t.path / "mods", false, cfgFile));

    CHECK(probe(rt, "t.values") == "true a 50 MButton");
    const std::string run1 = probe(rt, "t.run1");
    CHECK(run1.find("set true true false|") == 0);
    CHECK(run1.find("|badtype nil reason|notlisted nil reason|") != std::string::npos);
    CHECK(run1.find("|mode b,c,a,c|") != std::string::npos);        // next, next, wraps around, back
    CHECK(run1.find("|snap 50|") != std::string::npos);             // 60 is rounded to the step grid
    CHECK(run1.find("|slider 75,100,100,100|") != std::string::npos);  // clamped at max
    CHECK(run1.find("|range nil reason|") != std::string::npos);
    CHECK(run1.find("|unknown nil nil|") != std::string::npos);
    CHECK(run1.find("|log on=false<true;mode=b<a;mode=c<b;mode=a<c;mode=c<a;n=75<50;n=100<75|") != std::string::npos);
    CHECK(run1.find("|list 1 optmod Opts 4 Middle Middle Middle true off") != std::string::npos);   // a map of values is sorted by label: "Middle" < "Right mouse button"
    // only what differs from the defaults is stored
    const std::string stored = slurp(store);
    CHECK(stored.find("on = false") != std::string::npos && stored.find("mode = ") != std::string::npos && stored.find("n = 100") != std::string::npos);
    CHECK(stored.find("key") == std::string::npos);
    CHECK(probe(rt, "t.count") == "1");
    rt.stop();

    // the next start reads the choices back, before the mod looks at its entries
    CHECK(rt.start(t.path / "mods", false, cfgFile));
    CHECK(probe(rt, "t.values") == "false c 100 MButton");
    // and risen.config merges the same file on top of config.toml
    {
        sol::state lua;
        sol::table defaults = lua.create_table_with("on", true, "mode", "a", "n", 50, "key", "MButton");
        std::vector<std::string> problems;
        t.write("cfg.toml", "n = 10\n");
        sol::table merged = spass::game::loadModConfig(lua, t.path / "cfg.toml", defaults, problems, store);
        CHECK(problems.empty());
        CHECK(merged["on"].get<bool>() == false && merged["mode"].get<std::string>() == "c" && merged["n"].get<int>() == 100);
    }
    // back to the defaults: the store is gone
    CHECK(probe(rt, "t.run2") == "true 50 true 2 true a 50");
    CHECK(!fs::exists(store));
    // invalid declarations register nothing and say why
    const std::string bad = probe(rt, "t.bad");
    CHECK(bad.find("define expects a table") == 0);
    CHECK(bad.find("define needs at least one entry") != std::string::npos);
    CHECK(bad.find("option 'bad id'") == std::string::npos && bad.find("entry 1: id must be a string") != std::string::npos);
    CHECK(bad.find("option 'a' is defined twice") != std::string::npos);
    CHECK(bad.find("option 'a': a drop-down needs values") != std::string::npos);
    CHECK(bad.find("option 'a': a slider needs min < max") != std::string::npos);
    CHECK(bad.find("on_change must be a function") != std::string::npos);
    CHECK(bad.find("option 'a': default is a string, the option is a boolean") != std::string::npos);
    CHECK(bad.substr(bad.rfind('|') + 1) == "ok");                  // `entries = {...}` is accepted as well
    // a reload forgets the old declaration; unloading removes it
    CHECK(rt.reload("optmod", true).ok);
    CHECK(probe(rt, "t.count") == "1");
    CHECK(rt.unload("optmod", false).ok);
    CHECK(probe(rt, "t.count") == "0");
    rt.stop();
}
