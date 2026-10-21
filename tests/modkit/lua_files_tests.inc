// Tests of the project's hand-written Lua files (core/lua_files.h, the Lua panel): reading, the export, validation, the import and the runtime's
// protected loader (modkit/runtime.lua M.extensions). Included by modkit_tests.cpp. The live check is the scenario modkit_ext.

TEST(lua_file_reading) {
    const LuaFileInfo ok = read_lua_file_code(R"(
        local runtime = require("modkit.runtime")     -- not run: requires and top-level calls are fine
        risen.script.define("MKX_A", function(ctx) return 1 end)
        risen.script.define('MKX_B', function(ctx) return 2 end)
        return { before_install = function(data) end, after_install = function(rt) end }
    )", "x.lua");
    CHECK(ok.ok && ok.error.empty());
    CHECK(ok.hooks.size() == 2 && ok.hooks[0] == "before_install" && ok.hooks[1] == "after_install");
    CHECK(ok.scripts.size() == 2 && ok.scripts[0] == "MKX_A" && ok.scripts[1] == "MKX_B");
    const LuaFileInfo bad = read_lua_file_code("return {", "broken.lua");
    CHECK(!bad.ok && bad.error.find("broken.lua") != std::string::npos);    // the syntax error names the file
    CHECK(read_lua_file_code("error('at run time only')", "r.lua").ok);      // compiled, not run
    const LuaFileInfo style = read_lua_file_code("local M = {}\nfunction M.after_install(rt) end\n-- before_install = x\n--[[ risen.script.define('Nope', f) ]]\nreturn M", "s.lua");
    CHECK(style.hooks.size() == 1 && style.hooks[0] == "after_install" && style.scripts.empty());   // comments do not count
    CHECK(lua_file_name_problem("tweaks").empty() && lua_file_name_problem("_a1").empty());
    CHECK(!lua_file_name_problem("").empty() && !lua_file_name_problem("1x").empty() && !lua_file_name_problem("a.b").empty() &&
          !lua_file_name_problem("a-b").empty() && !lua_file_name_problem("custom").empty());
    CHECK_EQ(lua_module_file("tweaks"), std::string("lua/tweaks.lua"));
    CHECK_EQ(lua_module_name("tweaks"), std::string("lua.tweaks"));
    // the starter file of "New Lua file" compiles and shows both hooks (commented out: none found)
    const LuaFileInfo starter = read_lua_file_code(lua_file_template("extension_1", "my_mod"), "extension_1.lua");
    CHECK(starter.ok && starter.hooks.empty());
}

TEST(lua_file_export_import_validation) {
    Project p = load_showcase();
    const fs::path src = temp_dir("luaf_src");
    write_text(src / "a.lua", "return { before_install = function(data) end }");
    write_text(src / "b.lua", "risen.log.info('b')");
    p.lua_files.push_back({"l1", "b_second", u8str(src / "b.lua")});
    p.lua_files.insert(p.lua_files.begin(), LuaFile{"l0", "a_first", u8str(src / "a.lua")});
    // the project file keeps them, in order
    const Project back = project_from_json(to_json(p));
    CHECK(back.lua_files.size() == 2 && back.lua_files[0].name == "a_first" && back.lua_files[1].file == u8str(src / "b.lua"));
    // main.lua: the data table, the Lua files before the content, custom.lua last, all through runtime.extensions
    const auto files = render_mod(p);
    const std::string main = files.at("main.lua");
    const auto at = [&](const char* s) { return main.find(s); };
    CHECK(at("runtime.extensions(require(\"data.lua_files\"))") < at("lua.call(\"before_install\", data)"));
    CHECK(at("lua.call(\"before_install\", data)") < at("runtime.install(data)"));
    CHECK(at("runtime.install(data)") < at("lua.call(\"after_install\", runtime)"));
    CHECK(at("lua.call(\"after_install\", runtime)") < at("runtime.extensions({ \"custom\" })"));
    CHECK(main.find("require(\"custom\")") == std::string::npos);   // never unprotected
    CHECK(files.at("data/lua_files.lua").find("{ \"lua.a_first\", \"lua.b_second\" }") != std::string::npos);
    // without Lua files: no list, custom.lua still protected
    const auto plain = render_mod(load_showcase());
    CHECK(!plain.count("data/lua_files.lua") && plain.at("main.lua").find("runtime.extensions({})") != std::string::npos);
    // the export copies them into lua/, a later export without one removes it, custom.lua is never touched
    const fs::path dir = temp_dir("luaf") / "modkit_showcase";
    ExportOptions o;
    o.project_dir = showcase_dir();
    CHECK(export_mod(p, u8str(dir), o).ok);
    CHECK(fs::exists(dir / "lua" / "a_first.lua") && fs::exists(dir / "lua" / "b_second.lua"));
    write_text(dir / "custom.lua", "-- mine");
    Project q = p;
    q.lua_files.pop_back();
    const ExportResult r2 = export_mod(q, u8str(dir), o);
    CHECK(r2.ok && !fs::exists(dir / "lua" / "b_second.lua") && read_text(dir / "custom.lua") == "-- mine");
    // the import from the data files finds them again, in order, where the export put them
    CHECK(export_mod(p, u8str(dir), o).ok);
    const ImportResult imp = import_mod(u8str(dir), true);
    CHECK(imp.ok && imp.project.lua_files.size() == 2 && imp.project.lua_files[0].name == "a_first" &&
          fs::path(u8(imp.project.lua_files[1].file)).filename() == "b_second.lua");
    // a file that does not compile stops the export; a missing one too
    write_text(src / "c.lua", "return {");
    Project bad = load_showcase();
    bad.lua_files.push_back({"l2", "c", u8str(src / "c.lua")});
    bad.lua_files.push_back({"l3", "gone", u8str(src / "nope.lua")});
    const ExportResult rb = export_mod(bad, u8str(temp_dir("luaf_bad") / "out"), o);
    CHECK(!rb.ok && rb.errors.size() == 2 && rb.errors[0].find("Lua file c: c.lua") != std::string::npos && rb.errors[1].find("missing") != std::string::npos);
    // validation: names and files
    Project v = load_showcase();
    v.lua_files.push_back({"v1", "custom", "x.lua"});
    v.lua_files.push_back({"v2", "dup", "y.lua"});
    v.lua_files.push_back({"v3", "dup", ""});
    const auto probs = validate(v, nullptr);
    auto has = [&](const std::string& part) { return std::any_of(probs.begin(), probs.end(), [&](const Problem& x) { return x.message.find(part) != std::string::npos; }); };
    CHECK(has("'custom' is the mod folder's own custom.lua"));
    CHECK(has("two Lua files have this name"));
    CHECK(has("choose the Lua file"));
}

// modkit/runtime.lua M.extensions in a plain LuaJIT state: a module that fails is logged with the mod's id and skipped, the others' hooks run
TEST(lua_file_runtime_loader) {
    lua_State* L = luaL_newstate();
    luaL_openlibs(L);
    const std::string setup = R"(
        logged = {}
        local mt = {}
        local function standin() return setmetatable({}, mt) end
        mt.__index = function() return standin() end
        mt.__call = function() return standin() end
        risen = standin()
        risen.log = { error = function(s) logged[#logged + 1] = s end, warn = function() end, info = function() end }
        mod = { id = "t_mod" }
        package.preload["lua.good"] = function() return { before_install = function(d) d.n = d.n * 2 end, after_install = function(rt) AFTER = rt end } end
        package.preload["lua.bad"] = function() error("boom") end
        package.preload["lua.badhook"] = function() return { before_install = function() error("hook boom") end } end
        package.preload["lua.plain"] = function() PLAIN = true end
    )";
    CHECK(luaL_dostring(L, setup.c_str()) == 0);
    const std::string rt = runtime_lua();
    CHECK(luaL_loadbuffer(L, rt.data(), rt.size(), "=runtime.lua") == 0 && lua_pcall(L, 0, 1, 0) == 0);
    lua_setglobal(L, "runtime");
    const char* run = R"(
        local data = { n = 21 }
        local ext = runtime.extensions({ "lua.bad", "lua.good", "lua.badhook", "lua.plain", "lua.missing" })
        ext.call("before_install", data)
        ext.call("after_install", "RT")
        return data.n
    )";
    const bool ran = luaL_dostring(L, run) == 0;
    CHECK(ran);
    CHECK(ran && lua_tointeger(L, -1) == 42);   // the good module's hook ran, the failing hook after it did not stop anything
    lua_getglobal(L, "AFTER");
    CHECK(lua_isstring(L, -1) && std::string(lua_tostring(L, -1)) == "RT");
    lua_getglobal(L, "PLAIN");
    CHECK(lua_toboolean(L, -1));                 // a module without hooks just runs
    CHECK(luaL_dostring(L, "return #logged, logged[1], logged[2], logged[3]") == 0);
    CHECK(lua_tointeger(L, -4) == 3);            // lua.bad, lua.missing (load) and lua.badhook.before_install
    const std::string all = std::string(lua_tostring(L, -3) ? lua_tostring(L, -3) : "") + "|" + (lua_tostring(L, -2) ? lua_tostring(L, -2) : "") + "|" +
                            (lua_tostring(L, -1) ? lua_tostring(L, -1) : "");
    CHECK(all.find("[t_mod] lua.bad: ") != std::string::npos && all.find("boom") != std::string::npos);
    CHECK(all.find("[t_mod] lua.missing: ") != std::string::npos);
    CHECK(all.find("[t_mod] lua.badhook.before_install: ") != std::string::npos);
    lua_close(L);
}
