// ---- the options view of the mod manager's model (mods/modmanager/model.lua) against a fake `risen.options` ----------------------------------------------------
// Included by test_main.cpp inside its anonymous namespace.

TEST(modmanager_model_shows_and_changes_the_options_of_a_mod) {
    sol::state lua;
    lua.open_libraries(sol::lib::base, sol::lib::string, sol::lib::table, sol::lib::math, sol::lib::package);
    std::ifstream f(fs::path(SPASS_SOURCE_DIR) / "mods" / "modmanager" / "model.lua", std::ios::binary);
    CHECK(f.good());
    const std::string src((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    sol::protected_function_result loaded = lua.safe_script(src, sol::script_pass_on_error);
    CHECK(loaded.valid());
    lua.set("Model", loaded.get<sol::table>());
    lua.script(R"LUA(
        local function mod(id) return { id = id, name = id, version = "1.0.0", state = "loaded", enabled = true, requires = {}, load_after = {}, needed_by = {},
            reloadable = true, restart_required = {}, data = { present = false, keys = {} }, load_count = 1 } end
        local mods = {}
        function mods.list() return { mod("plain"), mod("opt") } end
        function mods.order() return { current = { "opt", "plain" }, planned = { "opt", "plain" }, automatic = { "opt", "plain" }, custom = {} } end
        -- a fake risen.options: one drop-down, one toggle, one slider, one text, one that needs a restart and 8 more toggles (pages of 5)
        local entries = {
            { id = "mode", name = "Mode", kind = "dropdown", value = "a", default = "a", text = "a", default_text = "a", values = { { value = "a", name = "a" }, { value = "b", name = "b" } }, index = 1, changed = false, description = "How." },
            { id = "on", name = "On", kind = "toggle", value = true, default = true, text = "on", default_text = "on", changed = false, description = "" },
            { id = "n", name = "N", kind = "slider", value = 100, default = 50, text = "100", default_text = "50", min = 0, max = 100, step = 25, changed = true, description = "" },
            { id = "free", name = "Free", kind = "text", value = "x", default = "x", text = "x", default_text = "x", changed = false, description = "" },
            { id = "late", name = "Late", kind = "toggle", value = false, default = false, text = "off", default_text = "off", changed = false, restart = true, description = "" },
        }
        for i = 1, 8 do entries[#entries + 1] = { id = "t" .. i, name = "T" .. i, kind = "toggle", value = true, default = true, text = "on", default_text = "on", changed = false, description = "" } end
        local calls = {}
        local options = {}
        function options.list() return { { mod = "opt", title = "Opt mod", own_save = false, entries = entries } } end
        local function find(id) for _, e in ipairs(entries) do if e.id == id then return e end end end
        function options.step(mod_id, id, dir)
            calls[#calls + 1] = ("step:%s:%s:%d"):format(mod_id, id, dir)
            local e = find(id)
            local old = e.value
            if e.kind == "toggle" then e.value = not e.value; e.text = e.value and "on" or "off"
            elseif e.kind == "dropdown" then e.index = e.index == 1 and 2 or 1; e.value = e.values[e.index].value; e.text = e.value
            else e.value = e.value + dir * e.step; e.text = tostring(e.value) end
            e.changed = e.value ~= e.default
            return true, { mod = mod_id, id = id, old = old, value = e.value, changed = true, restart = e.restart }
        end
        function options.reset(mod_id, id)
            calls[#calls + 1] = "reset:" .. mod_id .. ":" .. tostring(id)
            local count = 0
            for _, e in ipairs(entries) do
                if (id == nil or id == e.id) and e.value ~= e.default then e.value = e.default; e.text = tostring(e.default); e.changed = false; count = count + 1 end
            end
            return true, { mod = mod_id, changed = count > 0, count = count, value = id and find(id).value }
        end

        local m = Model.new({ mods = mods, options = options, version = "1" }, { rows = 5 })
        result = {}
        m:select("plain")
        local snap = m:snapshot()
        result.plain = snap.buttons.options.label .. ":" .. tostring(snap.buttons.options.enabled)
        result.open_plain = tostring(m:press("options")) .. ":" .. m.view
        m:select("opt")
        snap = m:snapshot()
        result.has = snap.buttons.options.label .. ":" .. tostring(snap.buttons.options.enabled)
        result.open = tostring(m:press("options")) .. ":" .. m.view
        snap = m:snapshot()
        result.title = snap.title .. "|" .. snap.view .. "|" .. snap.summary .. "|" .. snap.page .. "/" .. snap.pages .. "|" .. #snap.rows
        result.row1 = snap.rows[1].text .. "|" .. tostring(snap.rows[1].selected) .. "|" .. snap.rows[3].tone
        result.detail = snap.detail_title .. "|" .. snap.detail[1].text .. "|" .. snap.detail[3].text .. "|" .. snap.detail[#snap.detail].text
        result.labels = snap.buttons.toggle.label .. "|" .. snap.buttons.reload.label .. "|" .. snap.buttons.up.label .. "|" .. tostring(snap.buttons.up.enabled) .. "|" .. snap.buttons.down.label .. "|" .. snap.buttons.reset.label
        -- the drop-down: next value
        result.step = tostring(m:press("toggle")) .. ":" .. m.message.text .. ":" .. m.message.tone
        -- a toggle: "Switch off", no previous-value button
        m:select("on")
        snap = m:snapshot()
        result.toggle = snap.buttons.toggle.label .. ":" .. tostring(snap.buttons.reload.enabled)
        m:press("toggle")
        result.toggled = m:snapshot().buttons.toggle.label .. ":" .. m.message.text
        -- the slider at its maximum cannot go up; a text entry cannot be stepped; an option that needs a restart says so
        m:select("n")
        snap = m:snapshot()
        result.max = tostring(snap.buttons.toggle.enabled) .. ":" .. tostring(snap.buttons.reload.enabled) .. ":" .. tostring(m:press("toggle")) .. ":" .. m.message.text
        result.down = tostring(m:press("reload")) .. ":" .. m.message.text
        m:select("free")
        snap = m:snapshot()
        result.free = tostring(snap.buttons.toggle.enabled) .. ":" .. tostring(m:press("toggle")) .. ":" .. m.message.text
        m:select("late")
        m:press("toggle")
        result.restart = m.message.tone .. ":" .. m.message.text
        -- defaults
        m:select("n")
        result.default = tostring(m:press("up")) .. ":" .. m.message.text .. ":" .. tostring(find("n").value)
        result.all = tostring(m:press("down")) .. ":" .. m.message.text
        -- the second page
        m:page_to(m:current_page() + 1)
        snap = m:snapshot()
        result.page2 = snap.page .. "/" .. snap.pages .. ":" .. #snap.rows .. ":" .. snap.rows[1].id
        result.calls = table.concat(calls, ",")
        -- back to the list
        result.back = tostring(m:press("reset")) .. ":" .. m.view .. ":" .. m:snapshot().view
    )LUA");
    sol::table r = lua["result"];
    const auto get = [&](const char* k) { sol::object o = r[k]; return o.is<std::string>() ? o.as<std::string>() : std::string("<missing ") + k + ">"; };
    CHECK(get("plain") == "Options:false" && get("open_plain") == "false:mods" && get("has") == "Options (13):true");
    CHECK(get("open") == "true:options");
    CHECK(get("title") == "Options: Opt mod|options|13 options, 1 changed|1/3|5");
    CHECK(get("row1") == "Mode:  a|true|warn");
    CHECK(get("detail") == "Mode|How.|Value: a|Choice 1 of 2");
    CHECK(get("labels") == "Next value >|< Previous value|Default value|false|All defaults|Back to the mods");
    CHECK(get("step") == "true:Mode: b:info");
    CHECK(get("toggle") == "Switch off:false" && get("toggled") == "Switch on:On: off");
    CHECK(get("max") == "false:true:false:N is at its maximum.");
    CHECK(get("down") == "true:N: 75");
    CHECK(get("free") == "false:false:Free is free text: change it in the mod's config.toml.");
    CHECK(get("restart") == "warn:Late: on. Applies at the next start of the game.");
    CHECK(get("default").rfind("true:N: 50", 0) == 0);
    CHECK(get("all").rfind("true:Reset ", 0) == 0);
    CHECK(get("page2").rfind("2/3:5:", 0) == 0);
    CHECK(get("calls").find("step:opt:mode:1,step:opt:on:1") == 0 && get("calls").find("reset:opt:n") != std::string::npos && get("calls").find("reset:opt:nil") != std::string::npos);
    CHECK(get("back") == "true:mods:mods");
}
