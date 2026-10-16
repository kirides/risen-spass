---@diagnostic disable: need-check-nil, param-type-mismatch, cast-local-type, assign-type-mismatch
-- Live scenario `showcase` (only loaded in a test run, see main.lua). Two game processes (-Param phase=...):
--   play  new game: the numbers of every kind, the new items (one with loose files), the recipe, the dialogue conditions, the quest from the first word to the reward,
--         the apprentice who appears, the watchman's AI state, the voice; then a quick save, a quick load in the same process and a look at what is still there
--   load  a fresh process quick-loads that savegame: nothing is made twice, everything resolves again from the savegame
local T = risen.test

return function(story, balance, assets_state, reports)
    local phase = T.param("phase", "play")
    local BROGAR, TILDA, WATCHMAN, QUEST = story.BROGAR, story.TILDA, story.WATCHMAN, story.QUEST
    local SWORD, CLUB, RECIPE, SPELL = balance.SWORD, balance.CLUB, balance.RECIPE, balance.SPELL
    local VAR = "SPASS_RECIPE_" .. RECIPE
    local A = risen.assets

    local function tpl(name) return assert(risen.template.get(name), name) end
    local function player_now() return assert(risen.entity.player(), "no player") end
    local function count(name) return risen.item.count(player_now(), name) end
    local function await(pred, seconds)
        for _ = 1, math.floor(seconds * 8) do
            if pred() then return true end
            T.sleep(10)
        end
        return pred() and true or false
    end
    local function instances(template)
        local t = risen.template.get(template)
        local list = t and t:instances(100)
        return list and #list or -1
    end
    local function counts() return table.concat({ instances("Spass_BX_Brogar"), instances("Spass_BX_Tilda"), instances("Spass_BX_Watchman") }, ",") end
    local function quest_status() return (risen.quest.get(QUEST) and risen.quest.get(QUEST):get_property("Status")) or "none" end
    local function mods_text(list)
        local parts = {}
        for _, m in ipairs(list or {}) do parts[#parts + 1] = ("%s%+d"):format(m.skill, m.amount) end
        table.sort(parts)
        return table.concat(parts, ",")
    end
    local function lua_state(e) local c = e and risen.ai.current(e) return c and c.state end
    local function settled()
        T.wait_until(function() return risen.entity.get(BROGAR) ~= nil and risen.entity.get(WATCHMAN) ~= nil end, 90, "the persistent NPCs exist")
        T.sleep(120)
    end

    -- Opens the conversation, notes which options the engine offers, then runs one of them (an open conversation blocks saving; every option ends it).
    local function peek_and_run(npc, run_name)
        risen.dialog.start(player_now(), npc)
        T.sleep(90)
        local offered = {}
        for _, info in ipairs(risen.dialog.available()) do offered[info:name()] = true end
        assert(risen.dialog.get(run_name), run_name):run()
        T.wait_event("info.finished", 60, function(name) return name == run_name end)
        T.sleep(60)
        return offered
    end
    local function names(set)
        local l = {}
        for k in pairs(set) do if k:find("^BX_") then l[#l + 1] = k end end
        table.sort(l)
        return table.concat(l, ",")
    end
    local function save_game()
        T.wait_until(function() return risen.world.is_save_allowed() end, 30, "saving allowed")
        local saved = false
        risen.on("save.after", function(_, ok) saved = ok end)
        risen.world.quick_save()
        T.wait_until(function() return saved end, 60, "savegame written")
        T.sleep(200)   -- the engine keeps writing after save.after: the run must not end right after a save
        return saved
    end
    local function ready_to_smith(p)
        assert(risen.npc.set_stats(p, { skills = { Smith = 1, Strength = 20 } }))
        if count("It_SmithHammer") < 1 then risen.item.give(p, "It_SmithHammer", 1) end
        risen.item.give(p, "It_IronOre", 2)
        risen.item.give(p, "It_1H_Sword", 1)
    end

    local PLAN = {
        play = {
            "balance installed without failures", "shipped sword and potions re-balanced", "shipped monsters re-balanced", "spells: Fireball edited, Strong Fireball made",
            "new sword and its requirement", "the club's files exist and the engine loads them", "the recipe is known, the plan was given once",
            "Brogar: his template, his label, the voiced line is registered", "options before the quest: the engine offers only what holds", "the work option starts the quest",
            "quest running without ore: the hand-in is not offered", "two ore are not enough", "three ore: the hand-in is offered", "hand-in: ore taken, club given, quest done",
            "Tilda waited, now stands there", "Tilda has her routine and keeps her post by day", "Rolf runs the Lua state", "Rolf warns the player once",
            "crafting at the recipe works", "the sword option appears with the sword", "saved",
            "in-process load: nobody was made twice, Tilda is the same entity", "in-process load: rules and quest remembered", "in-process load: Rolf is back in his state", "in-process load: Brogar still talks",
        },
        load = {
            "load: the numbers are applied again", "load: the new items resolve", "load: the club's files are mounted", "load: inventory and recipe from the savegame",
            "load: one of each NPC, Tilda restored not made again", "load: the quest is done and its options are gone", "load: Tilda's routine and Rolf's state are back", "load: the voiced reply still plays",
        },
    }
    local lean = phase == "load" and T.param("lean", "") ~= ""   -- scripts/load-stress.ps1: only prove that the fresh-process load survives
    T.plan(lean and { "lean load survived" } or PLAN[phase] or {})

    T.run(function()
        T.wait_event("session.start", 180, function(m) return m == (phase == "play" and "new" or "load") end)
        settled()
        if lean then
            -- loops=N: load the savegame again N times in this process, the next one after a random 0.1 - 3 s (a load meets worlds in every state of settling)
            local loops = tonumber(T.param("loops", "0")) or 0
            for i = 1, loops do
                T.sleep(math.random(8, 240))
                if not risen.world.quick_load() then T.expect("loop load " .. i .. " started", false, "quick_load refused") break end
                T.wait_event("session.start", 120, function(m) return m == "load" end)
                if i % 10 == 0 then risen.log.info(("[loadloop] %d of %d loads done"):format(i, loops)) end
            end
            T.sleep(480)
            T.expect("lean load survived", risen.entity.player() ~= nil, "six seconds after the world settled")
            return
        end
        local p = player_now()
        local rule = assert(risen.spawn.get(TILDA))
        local before = counts() .. "/" .. rule:state()

        if phase == "play" then
            -- ---- 1. the numbers ---------------------------------------------------------------------------------------------------------------------------------------
            T.section("numbers")
            local bad = {}
            for _, r in ipairs(reports) do if not (r.report.ok and #r.report.failed == 0) then bad[#bad + 1] = r.when end end
            T.expect("balance installed without failures", #reports >= 2 and #bad == 0, ("%d applications (%s), failed: %s"):format(#reports, assets_state.ok and "assets ok" or tostring(assets_state.why), table.concat(bad, ",")))
            local sword, req = tpl("It_1H_Sword"), risen.item.requirements("It_1H_Sword") or {}
            T.eq("shipped sword and potions re-balanced", {
                sword:get_property("gCItem_PS", "GoldValue"), sword:get_property("gCDamage_PS", "DamageAmount"), req.Strength,
                mods_text(risen.item.modifiers("It_Po_Health_01")), tonumber(tpl("It_Po_Health_01"):get_property("gCItem_PS", "GoldValue")),
            }, { "90", "20", 8, "HitPoints+40", 15 })
            local x, y, z = p:position()
            x, y, z = x or 0, y or 0, z or 0
            local wolf = assert(risen.entity.spawn("Wolf", x + 2000, y + 20, z))
            local boar = assert(risen.entity.spawn("Boar", x + 2100, y + 20, z))
            T.eq("shipped monsters re-balanced", { wolf:hp_max(), risen.item.count(wolf, "It_Apple"), boar:hp_max(), boar:level() }, { 200, 1, 360, 8 })
            wolf:kill(); boar:kill()
            local fb = assert(risen.spell.get("Fireball"))
            local strong, scroll = tpl(SPELL), tpl(SPELL .. "_Scroll")
            T.eq("spells: Fireball edited, Strong Fireball made", { fb.items.crystal.mana_cost, fb.projectiles[1].damage, tonumber(strong:get_property("gCCastInfo_PS", "ManaCost")),
                tonumber(scroll:get_property("gCCastInfo_PS", "ManaCost")), strong:focus_name() }, { 10, 140, 20, 10, "Strong Fireball" })

            -- ---- 2. new items -------------------------------------------------------------------------------------------------------------------------------------------
            T.section("new items")
            local new_sword = tpl(SWORD)
            T.eq("new sword and its requirement", { new_sword:get_property("gCItem_PS", "GoldValue"), new_sword:get_property("gCDamage_PS", "DamageAmount"), new_sword:focus_name(),
                (risen.item.requirements(SWORD) or {}).Strength }, { "130", "24", "Slightly Improved Sword", 8 })
            local mesh = A.info("BX_Club.xmsh")
            local club = risen.entity.spawn(CLUB, x + 150, y + 20, z)
            local deps, has_mesh = club and club:dependencies() or {}, false
            for _, d in ipairs(deps) do if tostring(d):lower() == "bx_club._xmsh" then has_mesh = true end end
            T.sleep(120)   -- the resource system loads in the background
            local res = A.resource("BX_Club.xmsh")
            T.expect("the club's files exist and the engine loads them", assets_state.ok == true and mesh.found and mesh.loose and has_mesh and res and res.state == 3 and tostring(res.path):find("generated", 1, true) ~= nil,
                ("assets %s, loose %s, mesh dependency %s, resource %s %s"):format(tostring(assets_state.ok), tostring(mesh.loose), tostring(has_mesh), tostring(res and res.state), tostring(res and res.path)))
            if club then club:kill() end

            -- ---- 3. the recipe ------------------------------------------------------------------------------------------------------------------------------------------
            T.section("recipe")
            local info = assert(risen.recipe.info(RECIPE))
            T.eq("the recipe is known, the plan was given once", { info.station, info.result, risen.recipe.known(p, RECIPE), count(RECIPE), risen.gamevar.get(VAR) },
                { "anvil", { item = SWORD, count = 1 }, true, 1, 1 })

            -- ---- 4. Brogar and the dialogue conditions ---------------------------------------------------------------------------------------------------------------------
            T.section("Brogar")
            local brogar = assert(risen.entity.get(BROGAR))
            local tp = brogar:template()
            local voiced = false
            -- the voiced line is registered when the dialogue is created (the engine asks for it when the line is spoken)
            for _, k in ipairs(risen.voice.keys()) do if k:find("bx_brogar_who", 1, true) then voiced = true end end
            T.expect("Brogar: his template, his label, the voiced line is registered", tp and tp:name() == "Spass_BX_Brogar" and brogar:focus_name() == "Brogar the Smith" and voiced,
                ("%s / %s / voice %s"):format(tostring(tp and tp:name()), tostring(brogar:focus_name()), tostring(voiced)))
            local offered = peek_and_run(brogar, "BX_Brogar_Who")        -- also plays the voiced reply
            T.eq("options before the quest: the engine offers only what holds", names(offered), "BX_Brogar_Who,BX_Brogar_Work")

            offered = peek_and_run(brogar, "BX_Brogar_Work")
            T.expect("the work option starts the quest", quest_status():find("Running") ~= nil and risen.quest.is_running(QUEST), quest_status())
            offered = peek_and_run(brogar, "BX_Brogar_Who")
            T.eq("quest running without ore: the hand-in is not offered", names(offered), "BX_Brogar_Who")
            risen.item.give(p, "It_IronOre", 2)
            offered = peek_and_run(brogar, "BX_Brogar_Who")
            T.eq("two ore are not enough", names(offered), "BX_Brogar_Who")
            risen.item.give(p, "It_IronOre", 1)
            T.sleep(10)
            offered = peek_and_run(brogar, "BX_Brogar_Ore")
            T.eq("three ore: the hand-in is offered", names(offered), "BX_Brogar_Ore,BX_Brogar_Who")
            T.sleep(30)
            T.eq("hand-in: ore taken, club given, quest done", { count("It_IronOre"), count(CLUB), quest_status():find("Success") ~= nil }, { 0, 1, true })

            -- ---- 5. Tilda, the apprentice -------------------------------------------------------------------------------------------------------------------------------
            T.section("Tilda")
            T.wait_until(function() return rule:state() == "present" and risen.entity.get(TILDA) ~= nil end, 30, "Tilda appears")
            T.sleep(60)
            local tilda = assert(risen.entity.get(TILDA))
            T.eq("Tilda waited, now stands there", { before, counts(), story.stats.tilda_fired, rule:state(), (tilda:template() or { name = function() end }):name() },
                { "1,0,1/waiting", "1,1,1", { "condition" }, "present", "Spass_BX_Tilda" })
            local r = risen.npc.get_routine(tilda)
            risen.world.set_time(12, 0)
            risen.npc.apply_routine(tilda)
            local function guard_pos() return r and r.work and tilda:distance_to(r.work) or 1e9 end
            T.expect("Tilda has her routine and keeps her post by day", r and r.work and r.relax and r.sleep and r.created == 3
                and await(function() return guard_pos() < 90 and tilda:state() == "ZS_Interact_Interact" end, 90),
                ("routine %s, distance to the post %.0f, state %s"):format(tostring(r and r.work ~= nil), guard_pos(), tostring(tilda:state())))

            -- ---- 6. Rolf, the watchman (Lua AI) ----------------------------------------------------------------------------------------------------------------------------
            T.section("Rolf")
            local rolf = assert(risen.entity.get(WATCHMAN))
            T.expect("Rolf runs the Lua state", await(function() return lua_state(rolf) == "ZS_BX_Watch" end, 15), tostring(lua_state(rolf)))
            T.expect("Rolf warns the player once", await(function() return story.stats.warnings >= 1 end, 20) and story.stats.warnings == 1, tostring(story.stats.warnings))

            -- ---- 7. crafting, the dialogue condition on the sword ------------------------------------------------------------------------------------------------------------
            T.section("crafting")
            offered = peek_and_run(brogar, "BX_Brogar_Who")
            local without = names(offered)
            ready_to_smith(p)
            local ok, result = risen.recipe.craft(p, RECIPE)
            T.eq("crafting at the recipe works", { ok, result, count(SWORD), count("It_IronOre"), count("It_1H_Sword") }, { true, SWORD, 1, 0, 0 })
            offered = peek_and_run(brogar, "BX_Brogar_Who")
            T.eq("the sword option appears with the sword", { without, names(offered) }, { "BX_Brogar_Who", "BX_Brogar_Sword,BX_Brogar_Who" })

            -- ---- 8. savegame ---------------------------------------------------------------------------------------------------------------------------------------------------
            T.section("save and load")
            local brogar_id, tilda_id = brogar:id(), tilda:id()
            T.expect("saved", save_game())
            risen.world.quick_load()
            T.wait_event("session.start", 120, function(m) return m == "load" end)
            settled()
            T.sleep(60)
            local tilda2, brogar2 = risen.entity.get(TILDA), risen.entity.get(BROGAR)
            T.eq("in-process load: nobody was made twice, Tilda is the same entity", { counts(), tilda2 and tilda2:id() == tilda_id, brogar2 and brogar2:id() == brogar_id }, { "1,1,1", true, true })
            T.eq("in-process load: rules and quest remembered", { rule:state(), rule:fired(), story.stats.tilda_fired, quest_status():find("Success") ~= nil }, { "present", 1, { "condition" }, true })
            local rolf2 = risen.entity.get(WATCHMAN)
            T.expect("in-process load: Rolf is back in his state", await(function() rolf2 = risen.entity.get(WATCHMAN) return lua_state(rolf2) == "ZS_BX_Watch" end, 30), tostring(lua_state(rolf2)))
            offered = peek_and_run(risen.entity.get(BROGAR), "BX_Brogar_Who")
            T.eq("in-process load: Brogar still talks", names(offered), "BX_Brogar_Sword,BX_Brogar_Who")
            save_game()      -- the savegame the next process loads

        else
            -- ---- a fresh process loaded the savegame ------------------------------------------------------------------------------------------------------------------
            T.section("load")
            local sword, req = tpl("It_1H_Sword"), risen.item.requirements("It_1H_Sword") or {}
            local fb = assert(risen.spell.get("Fireball"))
            T.eq("load: the numbers are applied again", { sword:get_property("gCItem_PS", "GoldValue"), req.Strength, fb.items.crystal.mana_cost, tpl(SPELL):focus_name() },
                { "90", 8, 10, "Strong Fireball" })
            T.eq("load: the new items resolve", { tpl(SWORD):get_property("gCDamage_PS", "DamageAmount"), tpl(CLUB):focus_name(), risen.template.id_for(SWORD) == tpl(SWORD):id() },
                { "24", "Ember Club", true })
            local mesh = A.info("BX_Club.xmsh")
            T.expect("load: the club's files are mounted", mesh.found and mesh.loose, tostring(mesh.source))
            T.eq("load: inventory and recipe from the savegame", { count(SWORD), count(CLUB), count(RECIPE), risen.recipe.known(p, RECIPE), risen.gamevar.get(VAR) }, { 1, 1, 1, true, 1 })
            T.eq("load: one of each NPC, Tilda restored not made again", { counts(), rule:state(), rule:fired(), story.stats.tilda_fired }, { "1,1,1", "present", 1, {} })
            local offered = peek_and_run(assert(risen.entity.get(BROGAR)), "BX_Brogar_Who")
            T.eq("load: the quest is done and its options are gone", { quest_status():find("Success") ~= nil, names(offered) }, { true, "BX_Brogar_Sword,BX_Brogar_Who" })
            local tilda, rolf = assert(risen.entity.get(TILDA)), assert(risen.entity.get(WATCHMAN))
            local r = risen.npc.get_routine(tilda)
            T.expect("load: Tilda's routine and Rolf's state are back", r and r.work and r.relax and r.sleep and await(function() return lua_state(rolf) == "ZS_BX_Watch" end, 30),
                ("routine %s, state %s"):format(tostring(r and r.work ~= nil), tostring(lua_state(rolf))))
            local v
            for _, k in ipairs(risen.voice.keys()) do if k:find("bx_brogar_who", 1, true) then v = risen.voice.get(k) end end
            T.expect("load: the voiced reply still plays", v ~= nil and (v.requests or 0) >= 1, v and ("requests " .. tostring(v.requests)) or "no voice entry")
        end
    end, "showcase")
end
