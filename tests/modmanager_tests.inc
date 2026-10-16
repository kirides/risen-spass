// ---- the pure model of the in-game mod manager (mods/modmanager/model.lua) against a fake `risen.mods` ----------------------------------------------------
// Included by test_main.cpp inside its anonymous namespace.

TEST(modmanager_model_lists_selects_pages_and_describes) {
    sol::state lua;
    lua.open_libraries(sol::lib::base, sol::lib::string, sol::lib::table, sol::lib::math, sol::lib::package);
    std::ifstream f(fs::path(SPASS_SOURCE_DIR) / "mods" / "modmanager" / "model.lua", std::ios::binary);
    CHECK(f.good());
    const std::string src((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    sol::protected_function_result loaded = lua.safe_script(src, sol::script_pass_on_error);
    CHECK(loaded.valid());
    lua.set("Model", loaded.get<sol::table>());
    lua.script(R"LUA(
        -- a fake risen.mods: five mods, one needs another, one failed, one is off, one left data in the savegame
        local function mod(id, o)
            local e = { id = id, name = o.name or id, version = o.version or "1.0.0", author = "a", state = o.state or "loaded", enabled = o.enabled ~= false,
                requires = o.requires or {}, load_after = {}, needed_by = o.needed_by or {}, reloadable = o.reloadable ~= false, restart_required = o.restart or {},
                reason = o.reason, reload_note = o.note, data = o.data or { present = false, keys = {} }, load_order = o.order, load_count = 1 }
            return e
        end
        local fake = { calls = {} }
        fake.list_value = {
            mod("alpha", { needed_by = { "beta" }, order = 1 }),
            mod("beta", { requires = { "alpha" }, order = 2 }),
            mod("gamma", { order = 3, data = { present = true, npcs = 2, spawn_rules = 1, ai_states = 0, routines = 0, keys = { persistent_npcs = 2, spawn_rules = 1, runs = 1 } } }),
            mod("broken", { state = "failed", reason = "error while loading: boom", reloadable = true }),
            mod("off1", { state = "disabled", enabled = false, reloadable = false, note = "switched off" }),
            mod("!folder", { state = "failed", reason = "bad manifest", reloadable = false }),
        }
        fake.order_value = { current = { "alpha", "beta", "gamma" }, planned = { "alpha", "beta", "gamma" }, automatic = { "alpha", "beta", "gamma" }, custom = {} }
        local mods = {}
        function mods.list() return fake.list_value end
        function mods.order() return fake.order_value end
        function mods.check_order(ids)
            fake.calls[#fake.calls + 1] = "check:" .. table.concat(ids, ",")
            local pos = {}
            for i, id in ipairs(ids) do pos[id] = i end
            if pos.beta and pos.alpha and pos.beta < pos.alpha then return nil, "'beta' cannot be loaded before 'alpha' (requires)" end
            return true
        end
        function mods.set_order(ids) fake.calls[#fake.calls + 1] = "set:" .. (ids and table.concat(ids, ",") or "nil") fake.order_value.custom = ids or {} fake.order_value.planned = ids or fake.order_value.automatic return true end
        function mods.disable(id, o) fake.calls[#fake.calls + 1] = ("disable:%s:%s"):format(id, tostring(o.cascade)) return { ok = true, summary = id .. " unloaded", actions = {} } end
        function mods.enable(id, o) fake.calls[#fake.calls + 1] = ("enable:%s:%s"):format(id, tostring(o.cascade)) return { ok = true, summary = id .. " loaded", actions = {} } end
        function mods.reload(id, o) fake.calls[#fake.calls + 1] = "reload:" .. id return { ok = true, summary = id .. " reloaded", actions = { { action = "reloaded", restart_required = { "a quest" } } } } end
        function mods.orphans(id) fake.calls[#fake.calls + 1] = "orphans:" .. id return { { kind = "undeclared", name = "N1", id = "x" } } end
        function mods.remove_orphans(id, list) fake.calls[#fake.calls + 1] = "remove:" .. id .. ":" .. #list return #list end

        local m = Model.new({ mods = mods, version = "9.9", self_id = "alpha" }, { rows = 3 })
        result = {}
        local snap = m:snapshot()
        result.count = snap.count
        result.pages = snap.pages
        result.first_page = #snap.rows .. ":" .. snap.rows[1].id .. ":" .. tostring(snap.rows[1].selected)
        result.summary = snap.summary
        result.version = snap.version
        -- the manifest-less entry is listed but cannot take part in an order
        result.orderable = table.concat(m:_orderable(), ",")
        m:page_to(2)
        local s2 = m:snapshot()
        result.page2 = s2.page .. ":" .. #s2.rows .. ":" .. s2.rows[1].id .. ":" .. s2.rows[1].tone
        result.tones = {}
        for _, r in ipairs(s2.rows) do result.tones[#result.tones + 1] = r.id .. "=" .. r.tone end
        result.tones = table.concat(result.tones, ",")
        result.bad_page = tostring(m:page_to("x")) .. tostring(m:page_to(99)) .. m.page
        -- selection
        result.select_unknown = tostring(m:select("nope"))
        m:select("gamma")
        local sg = m:snapshot()
        result.detail_title = sg.detail_title
        local text = {}
        for _, l in ipairs(sg.detail) do text[#text + 1] = l.text end
        result.detail = table.concat(text, "|")
        m:select("broken")
        text = {}
        for _, l in ipairs(m:snapshot().detail) do text[#text + 1] = l.tone and (l.tone .. ":" .. l.text) or l.text end
        result.broken = table.concat(text, "|")
        -- keyboard-like navigation moves the page with the selection
        m:select("alpha")
        result.nav = m:move_selection(4) .. "@" .. m.page
        result.nav_end = m:move_selection(99) .. "@" .. m.page
        result.nav_start = m:move_selection(-99) .. "@" .. m.page
        result.names_clip = Model.clip("0123456789012345678901234567890123456789", 12)
        _G.mm = m
        _G.fake = fake
    )LUA");
    sol::table r = lua["result"];
    const auto get = [&](const char* k) { sol::object o = r[k]; return o.is<std::string>() ? o.as<std::string>() : std::string("<missing ") + k + ">"; };
    CHECK(r.get<int>("count") == 6 && r.get<int>("pages") == 2);
    CHECK(get("first_page") == "3:alpha:true");
    CHECK(get("summary") == "6 mods: 3 loaded, 2 failed, 1 off" && get("version") == "9.9");
    CHECK(get("orderable") == "alpha,beta,gamma,broken,off1");
    CHECK(get("page2") == "2:3:broken:fail" && get("tones") == "broken=fail,off1=off,!folder=fail");
    CHECK(get("bad_page") == "falsefalse2" || get("bad_page") == "falsetrue2");
    CHECK(get("select_unknown") == "false");
    CHECK(get("detail_title") == "gamma");
    CHECK(get("detail").find("Savegame data: 2 persistent NPCs, 1 spawn rule, 1 saved value") != std::string::npos);
    CHECK(get("broken").find("fail:error while loading: boom") != std::string::npos && get("broken").find("State: failed") != std::string::npos);
    CHECK(get("nav") == "off1@2" && get("nav_end") == "!folder@2" && get("nav_start") == "alpha@1");
    CHECK(get("names_clip") == "0123456789..");
}

TEST(modmanager_model_orders_toggles_reloads_and_asks) {
    sol::state lua;
    lua.open_libraries(sol::lib::base, sol::lib::string, sol::lib::table, sol::lib::math, sol::lib::package);
    std::ifstream f(fs::path(SPASS_SOURCE_DIR) / "mods" / "modmanager" / "model.lua", std::ios::binary);
    const std::string src((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    sol::protected_function_result loaded = lua.safe_script(src, sol::script_pass_on_error);
    CHECK(loaded.valid());
    lua.set("Model", loaded.get<sol::table>());
    lua.script(R"LUA(
        local function mod(id, o)
            return { id = id, name = o.name or id, version = "1.0.0", author = "", state = o.state or "loaded", enabled = o.enabled ~= false, requires = o.requires or {}, load_after = {},
                needed_by = o.needed_by or {}, reloadable = true, restart_required = o.restart or {}, data = o.data or { present = false, keys = {} }, load_count = 1, load_order = o.order }
        end
        local state = {
            alpha = mod("alpha", { needed_by = { "beta" }, order = 1 }),
            beta = mod("beta", { requires = { "alpha" }, order = 2 }),
            gamma = mod("gamma", { order = 3, data = { present = true, npcs = 1, spawn_rules = 0, ai_states = 0, routines = 0, keys = {} } }),
            delta = mod("delta", { order = 4 }),
        }
        local ids = { "alpha", "beta", "gamma", "delta" }
        local calls = {}
        local order = { current = { "alpha", "beta", "gamma", "delta" }, planned = { "alpha", "beta", "gamma", "delta" }, automatic = { "alpha", "beta", "gamma", "delta" }, custom = {} }
        local fail_host = false
        local mods = {}
        function mods.list() if fail_host then error("host down") end local out = {} for _, id in ipairs(ids) do out[#out + 1] = state[id] end return out end
        function mods.order() return order end
        function mods.check_order(l)
            local pos = {}
            for i, id in ipairs(l) do pos[id] = i end
            if pos.beta < pos.alpha then return nil, "'beta' cannot be loaded before 'alpha' (requires)" end
            return true
        end
        function mods.set_order(l) calls[#calls + 1] = "set:" .. (l and table.concat(l, ",") or "nil") order.custom = l or {} order.planned = l or order.automatic return true end
        function mods.disable(id, o)
            calls[#calls + 1] = ("disable:%s:%s"):format(id, tostring(o.cascade))
            if not o.cascade and #state[id].needed_by > 0 and state[state[id].needed_by[1]].enabled then return nil, "needed by beta (use cascade)" end
            state[id].enabled = false state[id].state = "disabled"
            if o.cascade then for _, d in ipairs(state[id].needed_by) do state[d].enabled = false state[d].state = "disabled" end end
            return { ok = true, summary = id .. " unloaded", actions = {} }
        end
        function mods.enable(id, o)
            calls[#calls + 1] = ("enable:%s:%s"):format(id, tostring(o.cascade))
            for _, rq in ipairs(state[id].requires) do if not state[rq].enabled then if o.cascade then state[rq].enabled = true state[rq].state = "loaded" else return nil, "requires " .. rq end end end
            state[id].enabled = true state[id].state = "loaded"
            return { ok = true, summary = id .. " loaded", actions = {} }
        end
        function mods.reload(id) calls[#calls + 1] = "reload:" .. id return { ok = true, summary = id .. " reloaded", actions = { { id = id, action = "reloaded", restart_required = { "a quest" } } } } end
        function mods.orphans(id) calls[#calls + 1] = "orphans:" .. id return { { kind = "undeclared", name = "N1", id = "x" }, { kind = "stray", name = "", id = "y" } } end
        function mods.remove_orphans(id, l) calls[#calls + 1] = "remove:" .. id .. ":" .. #l return #l end
        local m = Model.new({ mods = mods, version = "1", self_id = "delta" }, { rows = 12 })
        result = {}
        local function msg() return m.message and (m.message.tone .. ":" .. m.message.text) or "-" end
        -- moving: refused with the host's reason, nothing saved
        m:select("beta")
        local up_ok, up_why = m:can_move("beta", -1)
        result.beta_up = tostring(up_ok) .. ":" .. tostring(up_why)
        result.beta_up_press = tostring(m:press("up")) .. " " .. msg()
        result.saved_after_refusal = #calls == 0 and "none" or table.concat(calls, ";")
        result.first_up = tostring(select(1, m:can_move("alpha", -1))) .. ":" .. tostring(select(2, m:can_move("alpha", -1)))
        result.alpha_down = tostring(m:select("alpha") and m:press("down")) .. " " .. msg()
        -- an allowed move saves the whole list and says it applies at the next start
        m:select("gamma")
        result.gamma_down = tostring(m:press("down")) .. " " .. msg()
        result.order_after = table.concat(order.custom, ",")
        result.pending = tostring(m:snapshot().restart_note ~= nil)
        result.reset = tostring(m:press("reset")) .. " " .. msg() .. " " .. #order.custom
        -- switch off a required mod: asks, the answer no changes nothing, yes cascades
        m:select("alpha")
        result.off_alpha = tostring(m:press("toggle")) .. " " .. m.prompt.kind
        result.prompt_text = table.concat(m.prompt.lines, "|")
        m:answer("no")
        result.after_no = state.alpha.state .. "," .. state.beta.state .. " " .. msg()
        m:press("toggle")
        m:answer("yes")
        result.after_yes = state.alpha.state .. "," .. state.beta.state
        result.disable_calls = table.concat(calls, ";"):gsub("^.*disable", "disable")
        -- switching beta on asks for alpha
        m:select("beta")
        result.on_beta = tostring(m:press("toggle")) .. " " .. m.prompt.kind
        m:answer("yes")
        result.after_on = state.alpha.state .. "," .. state.beta.state
        -- data: switching gamma off asks; alt switches off only, yes removes the NPCs
        m:select("gamma")
        result.off_gamma = tostring(m:press("toggle")) .. " " .. m.prompt.kind .. " " .. tostring(m.prompt.alt ~= nil)
        m:answer("yes")
        local c = table.concat(calls, ";")
        result.retire = tostring(c:find("disable:gamma:false", 1, true) ~= nil) .. tostring(c:find("orphans:gamma", 1, true) ~= nil) .. tostring(c:find("remove:gamma:2", 1, true) ~= nil) .. " " .. msg()
        state.gamma.enabled = true state.gamma.state = "loaded"
        m:refresh()
        m:press("toggle")
        m:answer("alt")
        result.alt = state.gamma.state .. " " .. msg()
        -- reload reports restart notes
        m:select("delta")
        result.reload = tostring(m:press("reload")) .. " " .. msg()
        -- the manager itself asks once before it switches itself off
        result.self_off = tostring(m:press("toggle")) .. " " .. tostring(m.prompt and m.prompt.kind)
        m:answer("no")
        -- robustness: bad calls and a host that fails
        result.bogus = tostring(m:press("bogus")) .. tostring(m:answer("yes")) .. tostring(m:select(nil)) .. tostring(m:select(5))
        fail_host = true
        local ok = pcall(function() m:refresh() return m:snapshot() end)
        result.host_down = tostring(ok) .. " " .. m:snapshot().count
        fail_host = false
        m:refresh()
        result.recovered = tostring(m:snapshot().count)
    )LUA");
    sol::table r = lua["result"];
    const auto get = [&](const char* k) { sol::object o = r[k]; return o.is<std::string>() ? o.as<std::string>() : std::string("<missing ") + k + ">"; };
    CHECK(get("beta_up") == "false:'beta' cannot be loaded before 'alpha' (requires)");
    CHECK(get("beta_up_press") == "false error:beta cannot move up: 'beta' cannot be loaded before 'alpha' (requires)");
    CHECK(get("saved_after_refusal") == "none");
    CHECK(get("first_up") == "nil:it is the first mod in the load order");
    CHECK(get("alpha_down").rfind("false error:", 0) == 0 && get("alpha_down").find("'beta' cannot be loaded before 'alpha'") != std::string::npos);
    CHECK(get("gamma_down") == "true warn:gamma moved down. The new load order applies at the next start." && get("order_after") == "alpha,beta,delta,gamma");
    CHECK(get("pending") == "true");
    CHECK(get("reset").rfind("true warn:Load order reset", 0) == 0 && get("reset").back() == '0');
    CHECK(get("off_alpha") == "prompt cascade_off" && get("prompt_text").find("alpha is needed by beta.") != std::string::npos);
    CHECK(get("after_no") == "loaded,loaded info:Cancelled." && get("after_yes") == "disabled,disabled");
    CHECK(get("disable_calls").find("disable:alpha:true") != std::string::npos);
    CHECK(get("on_beta") == "prompt cascade_on" && get("after_on") == "loaded,loaded");
    CHECK(get("off_gamma") == "prompt data_off true");
    CHECK(get("retire").rfind("truetruetrue info:Switched off gamma and removed 2 of its NPC(s)", 0) == 0);
    CHECK(get("alt") == "disabled info:Switched off gamma: gamma unloaded" || get("alt").rfind("disabled", 0) == 0);
    CHECK(get("reload").rfind("true warn:Reloaded delta: delta reloaded", 0) == 0);
    CHECK(get("self_off") == "prompt self_off");
    CHECK(get("bogus") == "falsefalsefalsefalse");
    CHECK(get("host_down") == "true 4" && get("recovered") == "4");
}
