// ---- dialogue conditions (M9): risen.dialog.create{ when = ... } compiles risen.cond conditions to engine conditions and a Lua script; the engine is mocked -----------------
// Included by test_main.cpp inside its anonymous namespace (after spawn_when_tests.inc, whose mock it extends).

void setupDialogCondMock(sol::state& lua) {
    setupSpawnWhenMock(lua);
    lua.script(R"LUA(
        -- skills, guilds and the entities' guild for risen.cond.skill / guild
        local skills = { Strength = "gESkill_Stat_STR", Sword = "gESkill_Combat_Sword", Sneak = "gESkill_Misc_Sneak" }
        risen.balance = { skill_enum = function(n) return skills[n] end, skills = function() return { "Strength", "Sword", "Sneak" } end }
        risen.npc.guilds = { None = 0, Cit = 4, Pir = 7 }
        skill_values = { Strength = 30 }
        function risen.item.skill_total(e, name) return skill_values[name] or 0 end
        guild_of = {}
        local plain_get = risen.entity.get
        function risen.entity.get(n)
            local h = plain_get(n)
            if h then function h:guild() return guild_of[n] or 4 end end
            return h
        end
        local plain_player = risen.entity.player
        function risen.entity.player() local p = plain_player() function p:guild() return guild_of.player or 0 end return p end
        api.balance = risen.balance

        -- the dialogue layer the chunk wraps: infos that remember their conditions, scripts that remember their function
        infos, scripts, created = {}, {}, {}
        local function new_info(name, owner)
            local i = { name = name, owner = owner, conds = {}, given = false }
            function i:add_condition(cls)
                if fail_class == cls then return nil, "the engine could not create it" end
                self.conds[#self.conds + 1] = { class = cls, props = {} }
                return #self.conds
            end
            function i:set_condition_property(idx, k, v) if rejects == k then return false end self.conds[idx].props[k] = v return true end
            function i:get_condition_property(idx, k) if misreads == k then return "?" end return self.conds[idx].props[k] end
            function i:set_condition_skill(idx, member, enum, amount) self.conds[idx].skill = member .. ":" .. enum .. ":" .. amount return true end
            function i:set_given(b) self.given = b end
            function i:clear_conditions() self.conds = {} end
            function i:condition_count() return #self.conds end
            return i
        end
        local shared = {
            create = function(spec)
                if infos[spec.name] then return infos[spec.name] end
                created[#created + 1] = spec
                infos[spec.name] = new_info(spec.name, spec.owner)
                return infos[spec.name]
            end,
            get = function(n) return infos[n] end,
        }
        api.dialog = shared
        api.script = { define = function(name, fn) scripts[name] = fn end }
        api.npc.guilds = risen.npc.guilds
        function api.npc.persistent_get(n) return persistent[n] end
        persistent = {}
        api.item = risen.item
        world.by_name.Hob = { id = 'hob', name = 'Hob', dead = false }
    )LUA");
    spass::game::installDialogCondModApi(lua, lua["api"], "test-mod");
}

TEST(cond_skill_and_guild_describe_validate_and_evaluate) {
    sol::state lua;
    setupDialogCondMock(lua);
    auto run = [&](const char* code) { return lua.script(std::string("return tostring((function() ") + code + " end)())").get<std::string>(); };
    auto fails = [&](const char* code, const char* needle) {
        const std::string r = run((std::string("local ok, err = pcall(function() ") + code + " end) return tostring(ok) .. '|' .. tostring(err)").c_str());
        return r.rfind("false|", 0) == 0 && r.find(needle) != std::string::npos;
    };
    CHECK(run("return tostring(api.cond.skill('Strength', 25))") == "Strength of player >= 25");
    CHECK(run("return tostring(api.cond.skill('Sword', 2, '<', 'Hob'))") == "Sword of Hob < 2");
    CHECK(run("return tostring(api.cond.guild('Pir'))") == "player guild is Pir");
    CHECK(run("return tostring(api.cond.guild({ 'Pir', 4 }, 'npc'))") == "npc guild is Pir/4");
    CHECK(fails("api.cond.skill('Nope', 1)", "unknown skill 'Nope'"));
    CHECK(fails("api.cond.skill('Strength', 1.5)", "integer"));
    CHECK(fails("api.cond.skill('Strength', 1, '=>')", "unknown operator"));
    CHECK(fails("api.cond.guild('Robbers')", "unknown guild 'Robbers'"));
    CHECK(fails("api.cond.guild({})", "empty"));
    // evaluation: skills are totals, guilds numbers; "npc" needs a dialogue context
    CHECK(run("local c = api.cond.skill('Strength', 30) local a = api.cond.test(c) skill_values.Strength = 29 return tostring(a) .. tostring(api.cond.test(c))") == "truefalse");
    CHECK(run("guild_of.player = 7 return tostring(api.cond.test(api.cond.guild('Pir'))) .. tostring(api.cond.test(api.cond.guild({ 'Cit', 'None' })))") == "truefalse");
    CHECK(run("world.by_name.Hob = { id = 'h', name = 'Hob', dead = false } guild_of.Hob = 4 return tostring(api.cond.test(api.cond.guild('Cit', 'Hob'))) .. tostring(api.cond.test(api.cond.guild('Pir', 'Hob')))") == "truefalse");
    CHECK(run("return tostring(api.cond.test(api.cond.guild('Cit', 'npc')))") == "nil");
    CHECK(run("return tostring(api.cond.test(api.cond.guild('Cit', 'npc'), { npc = 'Hob' })) .. tostring(api.cond.test(api.cond.guild('Pir', 'npc'), { npc = 'Hob' }))") == "truefalse");
    CHECK(run("return tostring(api.cond.test(api.cond.guild('Cit', 'Nobody')))") == "false");
    // "npc" as an item holder; a predicate receives (npc, player) in a dialogue and nothing outside
    CHECK(run("inv.It_Apple = 2 return tostring(api.cond.test(api.cond.item('It_Apple', 1, 'npc'))) .. tostring(api.cond.test(api.cond.item('It_Apple', 1, 'npc'), { npc = 'Hob' }))") == "nilfalse");
    CHECK(run("local seen = {} local f = function(a, b) seen[#seen + 1] = tostring(a and a:name()) .. '/' .. tostring(b ~= nil) return true end "
              "api.cond.test(f) api.cond.test(f, { npc = 'Hob' }) return table.concat(seen, ',')") == "nil/false,Hob/true");
    // an error inside a dialogue is a warning (and unknown), outside it stays an error
    CHECK(run("local E = function() error('boom') end api.cond.test(E, { npc = 'Hob', name = 'Opt', level = 'warn' }) return lines[#lines]:sub(1, 1) .. tostring(lines[#lines]:find(\"dialogue 'Opt': condition 'predicate' raised an error\") ~= nil)") == "Wtrue");
    CHECK(run("local E = function() error('bang') end api.cond.test(E) return lines[#lines]:sub(1, 1)") == "E");
}

TEST(dialog_compile_splits_a_condition_into_engine_conditions_and_a_lua_remainder) {
    sol::state lua;
    setupDialogCondMock(lua);
    auto run = [&](const char* code) { return lua.script(std::string("return tostring((function() ") + code + " end)())").get<std::string>(); };
    lua.script(R"LUA(
        C = api.cond
        function plan(when, owner)
            local p, why = api.dialog.compile(when, owner or "Hob")
            if not p then return "ERR " .. why end
            return table.concat(p.engine, " ; ") .. " | " .. tostring(p.lua)
        end
    )LUA");
    // each leaf with an engine twin
    CHECK(run("return plan(C.item('It_IronOre', 3))") == "ItemAmount{Item=It_IronOre, ItemAmount=3, Owner=PC_Hero} | nil");
    CHECK(run("return plan(C.item('It_Apple', 1, 'npc'))") == "ItemAmount{Item=It_Apple, ItemAmount=1, Owner=Hob} | nil");
    CHECK(run("return plan(C.event('Flag'))") == "PlayerKnows{PlayerKnows=Flag} | nil");
    CHECK(run("return plan(C.skill('Strength', 25, '>=', 'npc'))") == "SkillValue{CompareOperation=gECompareOperation_GreaterEqual, Entity=Hob, SkillValue=gESkill_Stat_STR 25} | nil");
    CHECK(run("return plan(C.skill('Strength', 25))") == "SkillValue{CompareOperation=gECompareOperation_GreaterEqual, Entity=player, SkillValue=gESkill_Stat_STR 25} | nil");
    CHECK(run("return plan(C.dead('Corpse'))") == "NPCStatus{SecondaryNPC=Corpse, SecondaryNPCStatus=gEInfoNPCStatus_Dead} | nil");
    CHECK(run("return plan(C.alive('Corpse'))") == "NPCStatus{SecondaryNPC=Corpse, SecondaryNPCStatus=gEInfoNPCStatus_Alive} | nil");
    CHECK(run("return plan(C.quest('Q_A', 'Running'))") == "QuestStatus{CondType=gEInfoCondType_Running, IsCurrentStatus=true, Quest=Q_A} | nil");
    CHECK(run("return plan(C.quest('Q_A', 'Won'))") == "QuestStatus{CondType=gEInfoCondType_Won, IsCurrentStatus=true, Quest=Q_A} | nil");
    // negations that have a twin
    CHECK(run("return plan(C.negate(C.item('It_IronOre')))") == "ItemAmount{Item=It_IronOre, ItemAmount=0, Owner=PC_Hero} | nil");
    CHECK(run("return plan(C.negate(C.event('Flag')))") == "PlayerNotKnows{PlayerNotKnows=Flag} | nil");
    CHECK(run("return plan(C.negate(C.skill('Strength', 25)))") == "SkillValue{CompareOperation=gECompareOperation_Less, Entity=player, SkillValue=gESkill_Stat_STR 25} | nil");
    CHECK(run("return plan(C.negate(C.skill('Strength', 25, '==')))") == "SkillValue{CompareOperation=gECompareOperation_NotEqual, Entity=player, SkillValue=gESkill_Stat_STR 25} | nil");
    // everything else is the Lua remainder
    CHECK(run("return plan(C.var('X', 2, '>='))") == " | var X >= 2");
    CHECK(run("return plan(C.guild('Pir', 'npc'))") == " | npc guild is Pir");
    CHECK(run("return plan(C.quest('Q_A', 'Open'))") == " | quest Q_A is Open");
    CHECK(run("return plan(C.quest('Q_A', { 'Failed', 'Lost' }))") == " | quest Q_A is Failed/Lost");
    CHECK(run("return plan(C.item('It_Apple', 1, 'Smith'))") == " | Smith holds >= 1 x It_Apple");
    CHECK(run("return plan(C.negate(C.item('It_IronOre', 3)))") == " | not player holds >= 3 x It_IronOre");
    CHECK(run("return plan(C.negate(C.quest('Q_A', 'Running')))") == " | not quest Q_A is Running");
    CHECK(run("return plan(C.negate(C.dead('X')))") == " | not X is dead");
    CHECK(run("return plan(function() return true end)") == " | predicate");
    CHECK(run("persistent.Hob = {} return plan(C.dead('Hob'))") == " | Hob is dead");   // a persistent NPC may be dead without a corpse: only Lua knows
    CHECK(run("persistent.Hob = {} return plan(C.alive('Hob'))") == "NPCStatus{SecondaryNPC=Hob, SecondaryNPCStatus=gEInfoNPCStatus_Alive} | nil");
    // combinations: all{} is the sequence itself, the rest becomes one script condition
    CHECK(run("return plan(C.all { C.item('It_IronOre', 3), C.quest('Q', 'Running') })") == "ItemAmount{Item=It_IronOre, ItemAmount=3, Owner=PC_Hero} ; QuestStatus{CondType=gEInfoCondType_Running, IsCurrentStatus=true, Quest=Q} | nil");
    CHECK(run("return plan(C.any { C.var('X'), C.guild('Pir') })") == " | any(var X is set, player guild is Pir)");
    CHECK(run("return plan(C.all { C.item('It_IronOre', 3), C.var('X'), C.guild('Pir'), C.event('F') })") == "ItemAmount{Item=It_IronOre, ItemAmount=3, Owner=PC_Hero} ; PlayerKnows{PlayerKnows=F} | all(var X is set, player guild is Pir)");
    CHECK(run("return plan(C.all { C.all { C.event('A'), C.event('B') }, C.negate(C.event('C')) })") == "PlayerKnows{PlayerKnows=A} ; PlayerKnows{PlayerKnows=B} ; PlayerNotKnows{PlayerNotKnows=C} | nil");
    CHECK(run("return plan(C.negate(C.all { C.event('A'), C.event('B') }))") == " | not all(game event A is set, game event B is set)");
    // invalid
    CHECK(run("return plan(5)") == "ERR when: expected a condition (risen.cond.*) or a function");
}

TEST(dialog_create_attaches_engine_conditions_and_a_script_in_that_order) {
    sol::state lua;
    setupDialogCondMock(lua);
    auto run = [&](const char* code) { return lua.script(std::string("return tostring((function() ") + code + " end)())").get<std::string>(); };
    lua.script(R"LUA(
        C = api.cond
        lines_spec = { { speaker = 'player', listener = 'npc', text = 'Hi' } }
        info = api.dialog.create({ name = 'Opt', owner = 'Hob', when = C.all { C.var('X', 2, '>='), C.item('It_IronOre', 3), C.guild('Pir', 'npc'), C.event('F') }, lines = lines_spec })
    )LUA");
    // engine conditions first (the engine stops at the first one that fails, so the script is asked last), the script at the end
    CHECK(run("local t = {} for i, c in ipairs(info.conds) do t[i] = c.class end return table.concat(t, ',')") == "ItemAmount,PlayerKnows,Script");
    CHECK(run("local c = info.conds[1].props return c.Owner .. ',' .. c.Item .. ',' .. c.ItemAmount") == "PC_Hero,It_IronOre,3");
    CHECK(run("local c = info.conds[3].props return c.ScriptName .. ',' .. c.Self .. ',' .. c.Other .. ',' .. c.Param") == "SPASS_Cond_test_mod_Opt,Hob,player,0");
    CHECK(run("return tostring(scripts.SPASS_Cond_test_mod_Opt ~= nil) .. ',' .. tostring(created[1].when) .. ',' .. tostring(created[1].engine) .. ',' .. created[1].name") == "true,nil,nil,Opt");
    // the script evaluates the remainder in Lua, in the dialogue context
    CHECK(run("local s = scripts.SPASS_Cond_test_mod_Opt local a = s({}) vars.X = 2 guild_of.Hob = 7 local b = s({}) return a .. ',' .. b") == "0,1");
    CHECK(run("guild_of.Hob = 4 return scripts.SPASS_Cond_test_mod_Opt({})") == "0");
    // describe tells what was made; an idempotent second call attaches nothing again
    CHECK(run("local d = api.dialog.describe('Opt') return #d.engine .. ',' .. d.lua") == "2,all(var X >= 2, npc guild is Pir)");
    CHECK(run("local again = api.dialog.create({ name = 'Opt', owner = 'Hob', when = C.event('Other'), lines = lines_spec }) return tostring(again == info) .. ',' .. #info.conds .. ',' .. #created") == "true,3,1");
    // a condition that is only engine conditions needs no script
    CHECK(run("local i = api.dialog.create({ name = 'Plain', owner = 'Hob', when = C.all { C.event('A'), C.negate(C.item('It_Apple')) }, lines = lines_spec }) "
              "local n = 0 for _ in pairs(scripts) do n = n + 1 end return #i.conds .. ',' .. n") == "2,1");
    // a predicate gets the NPC and the player; the condition text of a function is "predicate"
    CHECK(run("seen = nil api.dialog.create({ name = 'Fn', owner = 'Hob', when = function(npc, player) seen = npc:name() .. '/' .. tostring(player:is_player()) return true end, lines = lines_spec }) "
              "world.by_name.Hob = world.by_name.Hob or { id = 'h', name = 'Hob', dead = false } return scripts.SPASS_Cond_test_mod_Fn({}) .. ',' .. seen") == "1,Hob/true");
    // `engine`: raw conditions by class and property, after the vocabulary ones; combined with `when`
    CHECK(run("local i = api.dialog.create({ name = 'Raw', owner = 'Hob', when = C.event('A'), engine = { { class = 'OwnerNearEntity', Entity = 'PC_Hero' }, "
              "{ class = 'SkillValue', Entity = 'player', CompareOperation = 'Less', Skill = 'Sword', Amount = 2 } }, lines = lines_spec }) "
              "return i.conds[1].class .. ',' .. i.conds[2].class .. ',' .. i.conds[3].class .. ',' .. i.conds[2].props.Entity .. ',' .. i.conds[3].props.CompareOperation .. ',' .. i.conds[3].skill") ==
          "PlayerKnows,OwnerNearEntity,SkillValue,PC_Hero,gECompareOperation_Less,SkillValue:gESkill_Combat_Sword:2");
    // no `when`, no `engine`: the shared function, untouched
    CHECK(run("api.dialog.create({ name = 'Old', owner = 'Hob', lines = lines_spec }) return #infos.Old.conds") == "0");
}

TEST(dialog_create_refuses_bad_conditions_and_never_leaves_an_unconditional_option) {
    sol::state lua;
    setupDialogCondMock(lua);
    auto run = [&](const char* code) { return lua.script(std::string("return tostring((function() ") + code + " end)())").get<std::string>(); };
    lua.script(R"LUA(
        C = api.cond
        lines_spec = { { speaker = 'player', listener = 'npc', text = 'Hi' } }
        function bad(spec) spec.lines = lines_spec local i, why = api.dialog.create(spec) return tostring(i) .. '|' .. tostring(why) end
    )LUA");
    // validated before anything is created
    CHECK(run("return bad({ name = 'A', owner = 'Hob', when = 5 })").find("nil|dialog.create: when: expected a condition") == 0);
    CHECK(run("return bad({ name = 'A', owner = 'Hob', engine = 'x' })").find("'engine' must be a list") != std::string::npos);
    CHECK(run("return bad({ name = 'A', owner = 'Hob', engine = { { class = 'Nope' } } })").find("unknown class 'Nope'") != std::string::npos);
    CHECK(run("return bad({ name = 'A', owner = 'Hob', engine = { { class = 'ItemAmount', Bogus = 1 } } })").find("ItemAmount has no property 'Bogus'") != std::string::npos);
    CHECK(run("return bad({ name = 'A', owner = 'Hob', engine = { { class = 'ItemAmount', Item = {} } } })").find("must be a string, number or boolean") != std::string::npos);
    CHECK(run("return bad({ name = 'A', owner = 'Hob', engine = { { class = 'SkillValue', Skill = 'Nope', Amount = 1 } } })").find("Skill must be a skill name") != std::string::npos);
    CHECK(run("return bad({ name = 'A', owner = 'Hob', engine = { { class = 'SkillValue', Skill = 'Sword', Amount = 1.5 } } })").find("Amount must be an integer") != std::string::npos);
    CHECK(run("return tostring(infos.A) .. ',' .. #created") == "nil,0");
    // the engine fails while the conditions are attached: the option exists but is given (never offered), nothing half-attached stays
    CHECK(run("fail_class = 'PlayerKnows' local r = bad({ name = 'B', owner = 'Hob', when = C.all { C.item('It_IronOre', 3), C.event('F') } }) fail_class = nil "
              "return r:sub(1, 3) .. ',' .. tostring(infos.B.given) .. ',' .. #infos.B.conds") == "nil,true,0");
    CHECK(run("rejects = 'ItemAmount' local r = bad({ name = 'C', owner = 'Hob', when = C.item('It_IronOre', 3) }) rejects = nil return r:find(\"has no property 'ItemAmount'\") ~= nil and tostring(infos.C.given)") == "true");
    CHECK(run("misreads = 'Owner' local r = bad({ name = 'D', owner = 'Hob', when = C.item('It_IronOre', 3) }) misreads = nil return r:find(\"the engine did not take 'PC_Hero'\") ~= nil and tostring(infos.D.given)") == "true");
}

TEST(dialog_script_condition_is_false_with_one_warning_when_lua_cannot_decide) {
    sol::state lua;
    setupDialogCondMock(lua);
    auto run = [&](const char* code) { return lua.script(std::string("return tostring((function() ") + code + " end)())").get<std::string>(); };
    lua.script(R"LUA(
        C = api.cond
        calls = 0
        api.dialog.create({ name = 'Err', owner = 'Hob', when = function() calls = calls + 1 error('boom') end, lines = { { speaker = 'player', listener = 'npc', text = 'Hi' } } })
        api.dialog.create({ name = 'Nil', owner = 'Hob', when = C.all { C.event('F'), function() return nil end }, lines = { { speaker = 'player', listener = 'npc', text = 'Hi' } } })
        warnings = function() local n = 0 for _, l in ipairs(lines) do if l:sub(1, 1) == 'W' then n = n + 1 end end return n end
        errors = function() local n = 0 for _, l in ipairs(lines) do if l:sub(1, 1) == 'E' then n = n + 1 end end return n end
    )LUA");
    // an error: 0 (false) for the engine, a warning (never an error line) once however often the engine asks
    CHECK(run("local a, b, c = scripts.SPASS_Cond_test_mod_Err({}), scripts.SPASS_Cond_test_mod_Err({}), scripts.SPASS_Cond_test_mod_Err({}) return a .. b .. c .. ',' .. calls .. ',' .. warnings() .. ',' .. errors()") == "000,3,2,0");
    CHECK(run("local m = {} for _, l in ipairs(lines) do m[#m + 1] = l end return m[#m - 1]:find(\"dialogue 'Err': condition 'predicate' raised an error\") ~= nil and m[#m]:find(\"dialog 'Err': a Lua condition could not be evaluated\") ~= nil") == "true");
    // a predicate that answers nil cannot tell: false, one warning
    CHECK(run("vars.F = 0 local a, b = scripts.SPASS_Cond_test_mod_Nil({}), scripts.SPASS_Cond_test_mod_Nil({}) return a .. b .. ',' .. warnings()") == "00,3");
    // the answer is a real 1 / 0 otherwise
    CHECK(run("api.dialog.create({ name = 'Ok', owner = 'Hob', when = function() return flag_value end, lines = { { speaker = 'player', listener = 'npc', text = 'Hi' } } }) "
              "flag_value = true local a = scripts.SPASS_Cond_test_mod_Ok({}) flag_value = false return a .. ',' .. scripts.SPASS_Cond_test_mod_Ok({})") == "1,0");
}
