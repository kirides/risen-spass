// ---- new templates: the pure helpers (name -> ID, name validation) and the Lua part of risen.template.create / the new_items, new_npcs and templates sections
// (the natives are mocked). Included by test_main.cpp inside its anonymous namespace.

TEST(template_id_md5_matches_the_reference_vectors) {
    using spass::templates::detail::md5;
    auto hex = [](const spass::templates::TemplateId& d) {
        static const char* digits = "0123456789abcdef";
        std::string s;
        for (auto b : d) {
            s.push_back(digits[b >> 4]);
            s.push_back(digits[b & 15]);
        }
        return s;
    };
    CHECK(hex(md5("")) == "d41d8cd98f00b204e9800998ecf8427e");
    CHECK(hex(md5("abc")) == "900150983cd24fb0d6963f7d28e17f72");
    CHECK(hex(md5("The quick brown fox jumps over the lazy dog")) == "9e107d9d372bb6826bd81d3542a419d6");
    // the padding boundary (55, 56 and 64 bytes)
    CHECK(hex(md5(std::string(55, 'a'))) == "ef1772b6dff9a122358552954ad0df65");
    CHECK(hex(md5(std::string(56, 'a'))) == "3b0c8ac703f828b04c6c197006d17218");
    CHECK(hex(md5(std::string(64, 'a'))) == "014842d480b571495a4a0363793f7367");
}

TEST(template_id_is_derived_from_the_name) {
    using namespace spass::templates;
    // deterministic, case sensitive, the root has its own ID
    CHECK(idForName("My_Sword") == idForName("My_Sword"));
    CHECK(idForName("My_Sword") != idForName("my_sword"));
    CHECK(idForName("My_Sword") != rootIdForName("My_Sword"));
    // the documented scheme: MD5 of "spass.template:<name>", printed like a GUID in memory order (Data1 little endian)
    CHECK(idForName("My_Sword") == detail::md5("spass.template:My_Sword"));
    CHECK(rootIdForName("My_Sword") == detail::md5("spass.template:My_Sword.tple"));
    const std::string text = guidText(idForName("My_Sword"));
    CHECK(text.size() == 36 && text[8] == '-' && text[13] == '-' && text[18] == '-' && text[23] == '-');
    const auto d = detail::md5("spass.template:My_Sword");
    char first[9];
    std::snprintf(first, sizeof first, "%02x%02x%02x%02x", d[3], d[2], d[1], d[0]);
    CHECK(text.substr(0, 8) == first);
    // no collisions among a few thousand plausible names (a collision would need a different name; the engine registry is checked at run time as well)
    std::set<std::string> seen;
    for (int i = 0; i < 5000; ++i) {
        seen.insert(guidText(idForName("Mod_Item_" + std::to_string(i))));
        seen.insert(guidText(idForName("Mod_Npc_" + std::to_string(i))));
    }
    CHECK(seen.size() == 10000);
}

TEST(template_names_are_validated) {
    using spass::templates::nameProblem;
    CHECK(nameProblem("Spass_Flame_Sword").empty());
    CHECK(nameProblem("_Hidden1").empty());
    CHECK(nameProblem(std::string(63, 'a')).empty());
    CHECK(!nameProblem("").empty());
    CHECK(!nameProblem(std::string(64, 'a')).empty());
    CHECK(!nameProblem("1Sword").empty());
    CHECK(!nameProblem("My Sword").empty());
    CHECK(!nameProblem("My-Sword").empty());
    CHECK(!nameProblem("Sch\xc3\xb6n").empty());
    CHECK(!nameProblem("a.tple").empty());
}

void setupCreateMock(sol::state& lua) {
    lua.open_libraries(sol::lib::base, sol::lib::string, sol::lib::table, sol::lib::math);
    lua.script(R"LUA(
        log, locs, created_map = {}, {}, {}
        local item_sets = { gCItem_PS = true, eCMesh_PS = true, gCInteraction_PS = true, gCDamage_PS = true }
        local npc_sets = { gCNPC_PS = true, gCSkills_PS = true, eCAnimation_PS = true, gCInteraction_PS = true }
        templates = {
            It_Sword = { sets = item_sets, focus = "Sword", desc = "A sword." },
            It_Club = { sets = item_sets, focus = "Club" },
            Wolf = { sets = npc_sets, focus = "Wolf" },
            Signpost = { sets = { gCInteraction_PS = true, eCMesh_PS = true } },
        }
        risen = { template = {}, loc = {}, balance = { sections = {} } }
        function risen.balance.register_section(name, handler) risen.balance.sections[name] = handler end
        function risen.template.exists(n) return templates[n] ~= nil end
        function risen.template.created() local c = {} for k, v in pairs(created_map) do c[k] = v end return c end
        function risen.template.name_problem(n)
            if n == "" or n:find("[^%w_]") or n:find("^%d") then return "bad name '" .. n .. "'" end
        end
        function risen.template.get(name)
            local t = templates[name]
            if not t then return nil, "unknown template '" .. name .. "'" end
            local h = {}
            function h:property_sets() local l = {} for k in pairs(t.sets) do l[#l + 1] = k end table.sort(l) return l end
            function h:focus_name() return t.focus or "" end
            function h:description() return t.desc or "" end
            return setmetatable(h, { __tostring = function() return "Template(" .. name .. ")" end })
        end
        function risen.template.clone(name, base)
            if templates[name] and not created_map[name] then return nil, "a template called '" .. name .. "' already exists" end
            if not templates[base] then return nil, "base template: unknown template '" .. base .. "'" end
            if not templates[name] then
                templates[name] = { sets = templates[base].sets, focus = templates[base].focus, desc = templates[base].desc }
                created_map[name] = base
                log[#log + 1] = "clone " .. name .. " " .. base
            end
            return risen.template.get(name)
        end
        function risen.template.patch(name, sets, opts)
            local n = 0
            for set, props in pairs(sets) do
                if not templates[name].sets[set] then return nil, "template '" .. name .. "' has no property set '" .. set .. "'" end
                for prop, v in pairs(props) do n = n + 1 if not (opts and opts.dry_run) then log[#log + 1] = "patch " .. name .. " " .. set .. "." .. prop .. "=" .. tostring(v) end end
            end
            return true, n
        end
        function risen.loc.exists(id) return locs[id] ~= nil end
        function risen.loc.language() return "English" end
        function risen.loc.set(id, text, lang) locs[id] = text locs[id .. "@" .. (lang or "*")] = text log[#log + 1] = "loc " .. id .. (lang and ("@" .. lang) or "") .. "=" .. text end
        -- the item and NPC sections as the real ones behave for what the tests need: a type check, an unknown-loot check, and a job that logs
        risen.balance.sections.items = function(data)
            local jobs, problems = {}, {}
            for name, e in pairs(data) do
                if not templates[name] then problems[#problems + 1] = "items." .. name .. ": unknown template"
                elseif e.value ~= nil and type(e.value) ~= "number" then problems[#problems + 1] = "items." .. name .. ": value must be an integer"
                elseif e.bogus ~= nil then problems[#problems + 1] = "items." .. name .. ": unknown setting 'bogus'"
                else jobs[#jobs + 1] = { label = name, run = function(o) log[#log + 1] = "items-run " .. name .. " value=" .. tostring(e.value) .. " mesh=" ..
                    tostring(e.properties and e.properties.eCMesh_PS and e.properties.eCMesh_PS.MeshFileName) .. " icon=" ..
                    tostring(e.properties and e.properties.gCItem_PS and e.properties.gCItem_PS.IconImage) .. " instances=" .. tostring(o.instances) return true, nil, 0 end } end
            end
            return jobs, problems
        end
        risen.balance.sections.npcs = function(data)
            local jobs, problems = {}, {}
            for name, e in pairs(data) do
                for item in pairs(e.loot or {}) do if not templates[item] then problems[#problems + 1] = "npcs." .. name .. ": loot: unknown item template '" .. item .. "'" end end
                if #problems == 0 then jobs[#jobs + 1] = { label = name, run = function() log[#log + 1] = "npcs-run " .. name .. " hp_max=" .. tostring(e.hp_max) .. " ani=" ..
                    tostring(e.properties and e.properties.eCAnimation_PS and e.properties.eCAnimation_PS.ResourceFilePath) return true, nil, 0 end } end
            end
            return jobs, problems
        end
    )LUA");
    spass::game::installTemplateCreateHelpers(lua, lua["risen"]);
}

TEST(template_create_translates_friendly_keys_and_applies_overrides) {
    sol::state lua;
    setupCreateMock(lua);
    auto run = [&](const char* code) { return lua.script(std::string("return tostring((function() ") + code + " end)())").get<std::string>(); };
    // an item: mesh, icon and material become property writes, the rest goes to the items section; the labels are set
    CHECK(run("local t, created = risen.template.create('My_Sword', 'It_Sword', { value = 900, mesh = 'It_Wpn_1H_Club.xmsh', icon = 'ic_it_1h_club', material = 2, "
              "name = 'Flame Tongue', description = 'Warm.', properties = { gCDamage_PS = { DamageAmount = 5 } } }) return tostring(t) .. ',' .. tostring(created)") == "Template(My_Sword),true");
    CHECK(run("return table.concat(log, ';')") == "clone My_Sword It_Sword;items-run My_Sword value=900 mesh=It_Wpn_1H_Club.xmsh icon=ic_it_1h_club instances=nil;"
                                                    "loc FO_My_Sword=Flame Tongue;loc ITEMDESC_My_Sword=Warm.");
    // creating again re-applies the overrides (created = false) and does not clone twice
    lua.script("log = {}");
    CHECK(run("local t, created = risen.template.create('My_Sword', 'It_Sword', { value = 100 }, { instances = true }) return tostring(created)") == "false");
    CHECK(run("return table.concat(log, ';')").find("clone") == std::string::npos);
    CHECK(run("return log[1]") == "items-run My_Sword value=100 mesh=nil icon=nil instances=true");
    // an NPC: the mesh is the actor file of eCAnimation_PS; a language table sets one label per language
    lua.script("log = {}");
    CHECK(run("local t = assert(risen.template.create('My_Wolf', 'Wolf', { hp_max = 500, mesh = 'Ani_Boar_Monster_Boar.xac', name = { English = 'Dire Wolf', German = 'Schreckenswolf' } })) return tostring(t)") ==
          "Template(My_Wolf)");
    CHECK(run("return table.concat(log, ';')") == "clone My_Wolf Wolf;npcs-run My_Wolf hp_max=500 ani=Ani_Boar_Monster_Boar.xac;loc FO_My_Wolf@English=Dire Wolf;loc FO_My_Wolf@German=Schreckenswolf" ||
          run("return table.concat(log, ';')") == "clone My_Wolf Wolf;npcs-run My_Wolf hp_max=500 ani=Ani_Boar_Monster_Boar.xac;loc FO_My_Wolf@German=Schreckenswolf;loc FO_My_Wolf@English=Dire Wolf");
    // a clone without `name` / `description` shows the label and description of its base, unless a mod already supplied them
    lua.script("log = {} locs.FO_Own_Club = 'My own club'");
    CHECK(run("assert(risen.template.create('Own_Club', 'It_Club', {})) assert(risen.template.create('Plain_Sword', 'It_Sword', {})) return locs.FO_Own_Club .. '|' .. locs.FO_Plain_Sword .. '|' .. "
              "locs.ITEMDESC_Plain_Sword .. '|' .. tostring(locs.ITEMDESC_Own_Club)") == "My own club|Sword|A sword.|nil");
    // other templates: only properties (and labels) are accepted
    CHECK(run("local t = assert(risen.template.create('My_Sign', 'Signpost', { mesh = 'x.xmsh', name = 'Sign' })) return tostring(t)") == "Template(My_Sign)");
    CHECK(run("local t, why = risen.template.create('My_Sign2', 'Signpost', { value = 3 }) return tostring(t) .. '|' .. why") ==
          "nil|unknown setting 'value' (a template that is neither item nor NPC accepts: name, description, mesh, icon, material, check_assets, properties)");
}

TEST(template_create_refuses_bad_requests_before_cloning) {
    sol::state lua;
    setupCreateMock(lua);
    auto run = [&](const char* code) { return lua.script(std::string("return tostring((function() ") + code + " end)())").get<std::string>(); };
    auto refused = [&](const char* args, const char* reason) {
        const std::string out = run((std::string("local t, why = risen.template.create(") + args + ") return tostring(t) .. '|' .. tostring(why)").c_str());
        const bool ok = out.rfind("nil|", 0) == 0 && out.find(reason) != std::string::npos;
        if (!ok) {
            std::printf("    create(%s) -> %s (wanted nil|...%s...)\n", args, out.c_str(), reason);
        }
        return ok;
    };
    CHECK(refused("'Bad Name', 'It_Sword', {}", "bad name"));
    CHECK(refused("'X', 'NoSuchBase', {}", "unknown template 'NoSuchBase'"));
    CHECK(refused("'It_Club', 'It_Sword', {}", "already exists"));                                  // a shipped name
    CHECK(refused("'X', 'It_Sword', { value = 'many' }", "value must be an integer"));              // validated by the items section
    CHECK(refused("'X', 'It_Sword', { bogus = 1 }", "unknown setting 'bogus'"));
    CHECK(refused("'X', 'It_Sword', { mesh = 5 }", "mesh: expected a mesh/actor file name"));
    CHECK(refused("'X', 'It_Sword', { icon = '' }", "icon: expected an icon image name"));
    CHECK(refused("'X', 'Wolf', { icon = 'ic_x' }", "only items have an icon"));
    CHECK(refused("'X', 'It_Sword', { material = 1.5 }", "material: expected a material switch number"));
    CHECK(refused("'X', 'It_Sword', { mesh = 'a.xmsh', properties = { eCMesh_PS = { MeshFileName = 'b.xmsh' } } }", "conflicts with mesh/icon/material"));
    CHECK(refused("'X', 'It_Sword', { name = 5 }", "name: expected a non-empty string"));
    CHECK(refused("'X', 'It_Sword', { name = { English = 5 } }", "name: expected a string or a table"));
    CHECK(refused("'X', 'Wolf', { description = 'text' }", "only items have a description"));
    CHECK(refused("'X', 'Signpost', { icon = 'ic_x' }", "has no property set 'gCItem_PS'"));
    CHECK(refused("'X', 'It_Sword', 'not a table'", "overrides must be a table"));
    CHECK(refused("'X', 'It_Sword', { base = 'It_Club' }", "overrides.base differs"));
    // nothing was cloned or labelled by any of them
    CHECK(run("return #log .. ',' .. tostring(next(created_map))") == "0,nil");
    // a clone cannot silently change its base
    CHECK(run("assert(risen.template.create('My_Sword', 'It_Sword', {})) local t, why = risen.template.create('My_Sword', 'It_Club', {}) return why") ==
          "template 'My_Sword' was already created from 'It_Sword' in this process (not 'It_Club')");
}

TEST(template_create_sections_validate_everything_and_plan_names) {
    sol::state lua;
    setupCreateMock(lua);
    auto run = [&](const char* code) { return lua.script(std::string("return tostring((function() ") + code + " end)())").get<std::string>(); };
    // the item section is validated against the base; the NPC section may loot an item the same spec creates
    CHECK(run("local jobs, problems = risen.balance.sections.new_items({ My_Sword = { base = 'It_Sword', value = 9 } }) return #jobs .. ',' .. #problems") == "1,0");
    CHECK(run("local jobs, problems = risen.balance.sections.new_npcs({ My_Wolf = { base = 'Wolf', loot = { My_Sword = 1 } } }) return #jobs .. ',' .. #problems .. ',' .. tostring(problems[1])") ==
          "1,0,nil");
    CHECK(run("local jobs, problems = risen.balance.sections.new_npcs({ Other_Wolf = { base = 'Wolf', loot = { Never_Planned = 1 } } }) return #jobs .. ',' .. #problems .. ',' .. problems[1]") ==
          "0,1,new_npcs.Other_Wolf: Other_Wolf: loot: unknown item template 'Never_Planned'");
    // `base` is required, kinds must match the section, and every problem is reported with the section path
    CHECK(run("local _, p = risen.balance.sections.new_items({ A = { value = 1 } }) return p[1]") == "new_items.A: 'base' (the template to clone) is required");
    CHECK(run("local _, p = risen.balance.sections.new_items({ A = { base = 'Wolf' } }) return p[1]") ==
          "new_items.A: base 'Wolf' is an NPC/monster template, expected an item template");
    CHECK(run("local _, p = risen.balance.sections.new_npcs({ A = { base = 'It_Sword' } }) return p[1]") ==
          "new_npcs.A: base 'It_Sword' is an item template, expected an NPC/monster template");
    CHECK(run("local j, p = risen.balance.sections.templates({ A = { base = 'It_Sword' }, B = { base = 'Wolf' } }) return #j .. ',' .. #p") == "2,0");
    CHECK(run("local _, p = risen.balance.sections.new_items('nope') return p[1]") == "new_items: expected a table { NewTemplateName = { base = \"...\", ... } }");
    // a clone of a clone from the same spec cannot be validated beforehand: the message says what to do
    CHECK(run("local _, p = risen.balance.sections.new_items({ Second = { base = 'My_Sword' } }) return p[1]").find("is created by the same spec") != std::string::npos);
    // running a job creates the template with the section's overrides and the apply options
    lua.script("log = {} templates.My_Sword = nil created_map.My_Sword = nil");
    CHECK(run("local jobs = risen.balance.sections.new_items({ My_Sword = { base = 'It_Sword', value = 9, name = 'Sword Nine' } }) "
              "local ok = jobs[1].run({ instances = true }) return tostring(ok) .. '|' .. table.concat(log, ';')") ==
          "true|clone My_Sword It_Sword;items-run My_Sword value=9 mesh=nil icon=nil instances=true;loc FO_My_Sword=Sword Nine;loc ITEMDESC_My_Sword=A sword.");
}

TEST(template_create_checks_mesh_and_icon_names_against_the_assets) {
    sol::state lua;
    setupCreateMock(lua);
    // a stand-in for risen.assets.check: only "Known.xmsh", "Known.xac" and the icon "IC_Known" exist
    lua.script(R"LUA(
        risen.assets = { check = function(kind, name)
            if kind == "icon" then if name == "IC_Known" then return true end return nil, "'" .. name .. "' is not a GUI bitmap" end
            if name == "Known.xmsh" and kind == "mesh" or name == "Known.xac" and kind == "actor" then return true end
            return nil, "no file for '" .. name .. "' (" .. kind .. ")"
        end }
    )LUA");
    auto run = [&](const char* code) { return lua.script(std::string("return tostring((function() ") + code + " end)())").get<std::string>(); };
    CHECK(run("local t = risen.template.create('A1', 'It_Sword', { mesh = 'Known.xmsh', icon = 'IC_Known' }) return tostring(t)") == "Template(A1)");
    CHECK(run("local t = risen.template.create('A2', 'Wolf', { mesh = 'Known.xac' }) return tostring(t)") == "Template(A2)");
    CHECK(run("local t, why = risen.template.create('A3', 'It_Sword', { mesh = 'Nope.xmsh' }) return tostring(t) .. '|' .. why") ==
          "nil|mesh: no file for 'Nope.xmsh' (mesh)");
    CHECK(run("local t, why = risen.template.create('A4', 'Wolf', { mesh = 'Known.xmsh' }) return tostring(t) .. '|' .. why") ==
          "nil|mesh: no file for 'Known.xmsh' (actor)");
    CHECK(run("local t, why = risen.template.create('A5', 'It_Sword', { icon = 'IC_Nope' }) return tostring(t) .. '|' .. why") ==
          "nil|icon: 'IC_Nope' is not a GUI bitmap");
    // opt out: the names are used as given
    CHECK(run("local t = risen.template.create('A6', 'It_Sword', { mesh = 'Nope.xmsh', icon = 'IC_Nope', check_assets = false }) return tostring(t)") == "Template(A6)");
    CHECK(run("local t, why = risen.template.create('A7', 'It_Sword', { check_assets = 'no' }) return tostring(t) .. '|' .. why") == "nil|check_assets: expected true or false");
    // refused requests cloned nothing
    CHECK(run("return tostring(created_map.A3) .. tostring(created_map.A4) .. tostring(created_map.A5) .. tostring(created_map.A7)") == "nilnilnilnil");
    // the declarative sections validate the same way
    CHECK(run("local _, p = risen.balance.sections.new_items({ B1 = { base = 'It_Sword', mesh = 'Nope.xmsh' } }) return p[1]") == "new_items.B1: mesh: no file for 'Nope.xmsh' (mesh)");
    CHECK(run("local j, p = risen.balance.sections.new_items({ B2 = { base = 'It_Sword', mesh = 'Nope.xmsh', check_assets = false } }) return #j .. ',' .. #p") == "1,0");
}
