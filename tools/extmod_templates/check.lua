-- Live check of the generated risen_extended mod (only loaded in a test run):
--   .\scripts\test-run.ps1 -Mods risen_extended -WaitForResult -Seconds 300 -Scenario extmod -Param check=extmod -Param phase=main -ShowChecks
--   phase main  new game: every entry applied, the archive numbers are in the engine, new items spawn / equip / are labelled, recipes craft (a smithing chain
--               and a goldsmith recipe), a monster with a new actor spawns, the loose assets resolve through the engine's name cache, quick save
--   phase load  a fresh process quick-loads that save: the crafted and given items of the new templates are still in the inventory
-- The numbers it compares are the ones in data/*.lua (taken from the archives by the generator) plus the explicit spot checks below (names typed here, numbers
-- from the analysis of the archives: docs/EXTENDED_MOD_ANALYSIS.md).
local T = risen.test

local ASSETS = require("data.assets")

local function num(text) return tonumber(text) end
local function player_now() return assert(risen.entity.player(), "no player") end
local function count(name) return risen.item.count(player_now(), name) end
local function sorted(t) local k = {} for n in pairs(t) do k[#k + 1] = n end table.sort(k) return k end

return function(M)
    local phase = T.param("phase", "main")
    local PLAN = {
        main = {
            "every entry applied", "first tick and session.start counts", "changed items carry the archive numbers", "spot checks: mana costs, crystals, potions, gold, light",
            "new items exist with their numbers", "new items are labelled", "a new weapon spawns and loads its mesh", "new items are equippable (requirements met)",
            "goldsmith recipe crafts", "smithing chain crafts", "monster actor swaps are applied", "a monster with a new actor spawns", "loose assets resolve to the mod",
            "replaced stock assets are shadowed", "second pass keeps the numbers",
            "dialogue edits applied", "an XP reward is on its info", "a trader refill is on its info", "chapter 1 trader stock is on the traders", "the combat table is the mod's (no backwards hop)", "a fight under the new combat table", "teacher limits are raised", "world edits applied", "a chest holds its new item",
            "hud2 texts are applied", "wind file and combat table are the mod's", "the repaired smithing chains",
            "placed items carry the mod's data", "Karakos carries the changed ogre axe", "renamed weapons are equippable", "Oscar sells the runesword from chapter 3",
        },
        load = { "load: the templates resolve", "load: items are still in the inventory", "load: the numbers are applied again", "load: stacks of the changed shipped items survive" },
    }
    T.plan(PLAN[phase] or {})

    local function check_numbers(label)
        local bad = {}
        for _, name in ipairs(sorted(M.items)) do
            local e = M.items[name]
            local t = risen.template.get(name)
            if not t then bad[#bad + 1] = name .. ": no template"
            else
                if e.value and num(t:get_property("gCItem_PS", "GoldValue")) ~= e.value then bad[#bad + 1] = ("%s value %s != %s"):format(name, tostring(t:get_property("gCItem_PS", "GoldValue")), e.value) end
                if e.damage and num(t:get_property("gCDamage_PS", "DamageAmount")) ~= e.damage then bad[#bad + 1] = ("%s damage %s != %s"):format(name, tostring(t:get_property("gCDamage_PS", "DamageAmount")), e.damage) end
                if e.mana_cost and num(t:get_property("gCCastInfo_PS", "ManaCost")) ~= e.mana_cost then bad[#bad + 1] = ("%s mana %s != %s"):format(name, tostring(t:get_property("gCCastInfo_PS", "ManaCost")), e.mana_cost) end
                if e.requires then
                    local want = {}
                    for _, r in ipairs(e.requires) do want[r.skill] = math.max(want[r.skill] or r.amount, r.amount) end
                    local got = risen.item.requirements(name) or {}
                    local d = {}
                    for k, v in pairs(want) do if got[k] ~= v then d[#d + 1] = k end end
                    for k in pairs(got) do if want[k] == nil then d[#d + 1] = k end end
                    if #d > 0 then bad[#bad + 1] = name .. " requires differ: " .. table.concat(d, ",") end
                end
                if e.modifies then
                    local got = risen.item.modifiers(name) or {}
                    if #got ~= #e.modifies then bad[#bad + 1] = ("%s modifies %d != %d"):format(name, #got, #e.modifies)
                    else
                        for i, m in ipairs(e.modifies) do
                            if got[i].skill ~= m.skill or got[i].amount ~= m.amount then bad[#bad + 1] = ("%s modifies[%d] %s%+d != %s%+d"):format(name, i, got[i].skill, got[i].amount, m.skill, m.amount) end
                        end
                    end
                end
            end
        end
        return bad
    end

    T.run(function()
        if phase == "main" then T.wait_event("session.start", 240, function(m) return m == "new" end)
        else T.wait_event("session.start", 240, function(m) return m == "load" end) end
        T.sleep(150)
        local p = player_now()

        if phase == "main" then
            T.section("application")
            T.eq("every entry applied", M.failed, {})
            local first, last
            for _, ps in ipairs(M.passes) do
                if ps.when == "first tick" then first = ps.counts end
                if ps.when:find("session.start", 1, true) then last = ps.counts end
            end
            local n_new, n_items = 0, 0
            for _ in pairs(M.new) do n_new = n_new + 1 end
            for _ in pairs(M.items) do n_items = n_items + 1 end
            T.eq("first tick and session.start counts", { first and first.new_items, first and first.items, last and last.new_items, last and last.items },
                { n_new, n_items, n_new, n_items })

            T.section("numbers")
            T.eq("changed items carry the archive numbers", check_numbers("main"), {})
            local function prop(name, set, p2) local t = risen.template.get(name) return t and t:get_property(set, p2) end
            local beast, berserk = M.items.It_Ru_Beast, M.items.It_Ru_Berserker
            local spot = {
                beast = { beast and beast.mana_cost, num(prop("It_Ru_Beast", "gCCastInfo_PS", "ManaCost")) },
                berserker = { berserk and berserk.mana_cost, num(prop("It_Ru_Berserker", "gCCastInfo_PS", "ManaCost")) },
                crystals = { num(prop("It_Crystal_Fire", "gCCastInfo_PS", "ManaCost")), num(prop("It_Crystal_Frost", "gCCastInfo_PS", "ManaCost")), num(prop("It_Crystal_Magic", "gCCastInfo_PS", "ManaCost")) },
                potions = { risen.item.modifiers("It_Po_STR_Max")[1].amount, risen.item.modifiers("It_Po_DEX_Max")[1].amount, risen.item.modifiers("It_Po_MP_Max")[1].amount, risen.item.modifiers("It_Po_HP_Max")[1].amount },
                gold = prop("It_Gold", "gCItem_PS", "Category"),
                light = num(prop("It_Cast_Light", "eCPointLight_PS", "Range")),
            }
            T.eq("spot checks: mana costs, crystals, potions, gold, light", spot, {
                beast = { 200, 200 }, berserker = { 100, 100 }, crystals = { 16, 24, 9 }, potions = { 3, 3, 3, 15 }, gold = "gEItemCategory_Misc", light = 1500,
            })

            T.section("new items")
            local bad = {}
            for _, name in ipairs(sorted(M.new)) do
                local e, t = M.new[name], risen.template.get(name)
                if not t then bad[#bad + 1] = name .. ": missing"
                else
                    if e.value and num(t:get_property("gCItem_PS", "GoldValue")) ~= e.value then bad[#bad + 1] = name .. " value" end
                    if e.damage and num(t:get_property("gCDamage_PS", "DamageAmount")) ~= e.damage then bad[#bad + 1] = name .. " damage" end
                end
            end
            T.eq("new items exist with their numbers", bad, {})
            local unlabelled = {}
            for _, name in ipairs(sorted(M.new)) do
                local t = risen.template.get(name)
                local label = t and t:focus_name() or ""
                if label == "" or risen.loc.exists("FO_" .. name) == false then unlabelled[#unlabelled + 1] = name end
            end
            T.eq("new items are labelled", unlabelled, {})

            -- a new weapon: spawn it, the engine loads its mesh (a loose file for most of them)
            local x, y, z = p:position()
            local meshes = {}
            local sample = {}
            for _, name in ipairs(sorted(M.new)) do
                if M.new[name].mesh and M.new[name].damage and #sample < 6 then sample[#sample + 1] = name end
            end
            local spawned, states = 0, {}
            for i, name in ipairs(sample) do
                local e = risen.entity.spawn(name, (x or 0) + 100 + 40 * i, (y or 0) + 10, z or 0)
                if e and e:is_item() then spawned = spawned + 1 end
            end
            T.sleep(40)
            for _, name in ipairs(sample) do
                local mesh = M.new[name].mesh:gsub("%.(x%w+)$", "._%1")
                local r = risen.assets.resource(mesh)
                states[#states + 1] = ("%s=%s"):format(mesh, r and r.state or "?")
                if not (r and r.state == 3) then meshes[#meshes + 1] = mesh end
            end
            T.eq("a new weapon spawns and loads its mesh", { spawned, meshes }, { #sample, {} })

            -- equip: meet the requirements, then the engine's own check
            assert(risen.npc.set_stats(p, { skills = { Strength = 200, Dexterity = 200, Smith = 3, Alchemy = 3, Circle = 3 } }))
            local not_usable, equipped = {}, 0
            for _, name in ipairs(sample) do
                risen.item.give(p, name, 1)
                if not risen.item.can_use(p, name) then not_usable[#not_usable + 1] = name end
            end
            for _, name in ipairs(sample) do
                if M.new[name].use_type == nil and risen.item.equip(p, name) then equipped = equipped + 1 end
            end
            T.sleep(20)
            T.eq("new items are equippable (requirements met)", { not_usable, equipped > 0 }, { {}, true })

            T.section("recipes")
            -- goldsmith: eight pearls -> a pearl necklace (the recipe that the mod hung on the pearl item)
            local rname
            for _, name in ipairs(sorted(M.rec.new)) do if M.rec.new[name].result == "It_Necklace_Pearl" then rname = name end end
            local made = 0
            if rname then
                risen.item.give(p, "It_Pearl", 8)
                local ok, why = risen.recipe.craft(p, rname, 1, { unknown = true })
                made = count("It_Necklace_Pearl")
                if not ok then risen.log.info("extmod: goldsmith craft: " .. tostring(why)) end
            end
            T.eq("goldsmith recipe crafts", { rname ~= nil, made, count("It_Pearl") }, { true, 1, 0 })

            -- a whole smithing chain: plan -> hot -> shaped -> dull -> weapon; the stage list is read from the recipe data
            local chain, pos = {}, nil
            for _, name in ipairs(sorted(M.rec.edit)) do
                if name:match("_Recipe$") and M.rec.edit[name].station == "forge" and (name:find("Ruby") or #chain == 0) then pos = name end
            end
            local stages, fin = {}, nil
            if pos then
                local cur = pos
                for _ = 1, 6 do
                    local r = M.rec.edit[cur]
                    if not r then break end
                    stages[#stages + 1] = cur
                    for _, ing in ipairs(r.ingredients) do if ing.item ~= cur and count(ing.item) < ing.count then risen.item.give(p, ing.item, ing.count) end end
                    if count(cur) < 1 then risen.item.give(p, cur, 1) end
                    local ok, why = risen.recipe.craft(p, cur, 1, { unknown = true })
                    if not ok then risen.log.info("extmod: chain stage " .. cur .. ": " .. tostring(why)) break end
                    cur = r.result
                    fin = cur
                end
            end
            T.expect("smithing chain crafts", pos ~= nil and fin ~= nil and count(fin) >= 1 and M.rec.edit[fin] == nil,
                ("start %s, stages %s, result %s x%s"):format(tostring(pos), table.concat(stages, ">"), tostring(fin), fin and count(fin) or "?"))

            T.section("monsters")
            local swapped, wrong = 0, {}
            for _, name in ipairs(sorted(M.patch)) do
                local t = risen.template.get(name)
                local want = M.patch[name].eCAnimation_PS and M.patch[name].eCAnimation_PS.ResourceFilePath
                if t and want then
                    swapped = swapped + 1
                    if t:get_property("eCAnimation_PS", "ResourceFilePath") ~= want then wrong[#wrong + 1] = name end
                end
            end
            T.eq("monster actor swaps are applied", { swapped > 0, wrong }, { true, {} })
            local mon = sorted(M.patch)[1]
            for _, name in ipairs(sorted(M.patch)) do if name == "LizardWarrior_Keeper" then mon = name end end
            local e = risen.entity.spawn(mon, (x or 0) - 300, (y or 0) + 10, z or 0)
            T.sleep(60)
            local deps = e and e:dependencies() or {}
            local want_actor = M.patch[mon].eCAnimation_PS.ResourceFilePath:lower()
            local found = false
            for _, d in ipairs(deps) do if tostring(d):lower():find(want_actor, 1, true) then found = true end end
            local res = risen.assets.resource((M.patch[mon].eCAnimation_PS.ResourceFilePath:gsub("%.xac$", "._xmac")))
            T.expect("a monster with a new actor spawns", e ~= nil and not e:is_dead() and found and res ~= nil and res.state >= 1, ("%s deps %s want %s"):format(mon, table.concat(deps, ","), want_actor))
            if e then e:kill() end

            T.section("assets")
            local not_mod, repl_bad = {}, {}
            for _, name in ipairs(ASSETS.new) do
                local phys = risen.vfs.lookup_physical(name)
                if not (phys and phys:lower():find("risen_extended", 1, true)) then not_mod[#not_mod + 1] = name .. " -> " .. tostring(phys) end
            end
            T.eq("loose assets resolve to the mod", { #ASSETS.new > 0, not_mod }, { true, {} })
            for _, name in ipairs(ASSETS.replaced) do
                local phys = risen.vfs.lookup_physical(name)
                if not (phys and phys:lower():find("risen_extended", 1, true)) then repl_bad[#repl_bad + 1] = name .. " -> " .. tostring(phys) end
            end
            T.eq("replaced stock assets are shadowed", { #ASSETS.replaced > 0, repl_bad }, { true, {} })

            T.section("second pass")
            T.eq("second pass keeps the numbers", check_numbers("again"), {})

            T.section("dialogue and world")
            local DLG, WORLD = M.dlg, M.wrld
            local d = M.dialog or {}
            T.eq("dialogue edits applied", { d.xp_added, d.xp_changed, d.trade_entries, d.trade_infos },
                { #DLG.xp.add, #DLG.xp.change, (function() local n = 0 for _, t in ipairs(DLG.trade) do n = n + #t.entries end return n end)(), #DLG.trade })
            -- read back: the last XP reward and the last trade command of the first info
            local xp = DLG.xp.add[#DLG.xp.add]
            local inf = risen.dialog.get(xp.info)
            local last = inf and inf:command_count() or 0
            T.eq("an XP reward is on its info", { inf ~= nil, last > 0 and inf:get_command_property(last, "Amount") }, { true, tostring(xp.amount) })
            local t1 = DLG.trade[1]
            local tinf = risen.dialog.get(t1.info)
            local tn = tinf and tinf:command_count() or 0
            local lastE = t1.entries[#t1.entries]
            T.eq("a trader refill is on its info", { tn > 0 and tinf:get_command_property(tn, "Self"), tn > 0 and tinf:get_command_property(tn, "Other"), tn > 0 and tinf:get_command_property(tn, "Param") },
                { lastE.trader, lastE.item, tostring(lastE.count) })
            -- chapter 1 stock was handed out at the start: Karlsen sells a staff from his trade stack (a stack of type 1; risen.item.count does not count it)
            local function has_stack(e, name)
                for i = 0, e:stack_count() - 1 do local s = e:stack_item(i) if s and s:name() == name then return true end end
                return false
            end
            local first = nil
            for _, t in ipairs(DLG.trade) do if t.chapter == 1 then first = t end end
            local ent = first and first.entries[#first.entries]
            local trader = ent and risen.entity.get(ent.trader)
            T.expect("chapter 1 trader stock is on the traders", trader ~= nil and has_stack(trader, ent.item) and (M.stock and M.stock.failed == 0),
                ent and ("%s has %s: %s, stock %s/%s"):format(ent.trader, ent.item, tostring(trader and has_stack(trader, ent.item)), tostring(M.stock and M.stock.refilled), tostring(M.stock and M.stock.failed)) or "no chapter 1 info")
            local wrong = {}
            for _, t in ipairs(WORLD.teachers) do
                local e = risen.entity.by_id(t.id)
                local list = e and risen.npc.teach_skills(e) or {}
                local got
                for _, s in ipairs(list) do if s.skill == risen.balance.skill_enum(t.skill) then got = s.max end end
                if got ~= t.max then wrong[#wrong + 1] = ("%s %s %s != %s"):format(t.name, t.skill, tostring(got), t.max) end
            end
            T.eq("teacher limits are raised", wrong, {})
            local w = M.world or {}
            T.eq("world edits applied", { w.containers, w.equipped ~= nil }, { #WORLD.containers, true })
            local box = WORLD.containers[1]
            local be = box and risen.entity.by_id(box.id)
            T.expect("a chest holds its new item", be ~= nil and risen.item.count(be, box.items[1].item) >= box.items[1].count, box and (box.name .. " " .. box.items[1].item))

            T.section("texts, wind, repaired chains")
            local wr = risen.loc.get("HUD2_Recipe_WriteScroll") or ""
            local rmap = risen.loc.get("HUD2_Button_WorlMap") or ""
            T.expect("hud2 texts are applied", wr:lower():find("transcription", 1, true) ~= nil and rmap:lower():find("world map", 1, true) ~= nil, ("WriteScroll %q, WorlMap %q"):format(wr:sub(1, 60), rmap))
            local wphys = risen.vfs.lookup_physical("WeatherStates.g3ws") or ""
            T.expect("wind file and combat table are the mod's", wphys:lower():find("risen_extended", 1, true) ~= nil, "WeatherStates.g3ws -> " .. wphys)
            local dw, so = M.rec.edit["It_2H_Dark_Wind_Hot"], M.rec.edit["It_Staff_Obsidian_Dull"]
            T.expect("the repaired smithing chains", dw ~= nil and dw.result == "It_2H_Dark_Wind_Shaped" and so ~= nil and so.result == "It_Staff_Slicer" and so.ingredients[1].item == "It_Staff_Obsidian_Dull",
                ("Dark Wind hot -> %s; obsidian staff dull -> %s from %s"):format(tostring(dw and dw.result), tostring(so and so.result), tostring(so and so.ingredients[1] and so.ingredients[1].item)))

            T.section("combat table")
            local phys = risen.vfs.lookup_physical("compiled_combatspecies.bin") or ""
            local ours = phys:lower():find("risen_extended", 1, true) ~= nil
            local jump
            for _, p in ipairs(risen.npc.combat_species("Human") or {}) do if p.name == "JumpBack" then jump = p.value end end
            T.expect("the combat table is the mod's (no backwards hop)", (not ours) or jump == "", ("file %s, Human.JumpBack = %q"):format(phys, tostring(jump)))

            T.section("a fight")
            -- two humans of enemy guilds with 2H / axe / 1H weapons of the mod fight each other under the (new) combat table: blows land, nothing crashes
            local hits = 0
            risen.on("entity.damaged", function(victim, attacker, lost) if lost and lost > 0 then hits = hits + 1 end end)
            local fx, fy, fz = p:position()
            local a = risen.npc.spawn("Inq_Novice_Fighter_01", (fx or 0) + 1500, fy or 0, fz or 0, { name = "EXT_Fighter_A", guild = "Inq", hp_max = 3000, level = 10, items = { { "It_Axe_Ogre", 1 } }, equip = true })
            local bb = risen.npc.spawn("Don_Henchman_S_01", (fx or 0) + 1600, fy or 0, fz or 0, { name = "EXT_Fighter_B", guild = "Pir", hp_max = 3000, level = 10, items = { { "It_1H_Epee", 1 } }, equip = true })
            T.wait_until(function() return hits >= 3 end, 40, "blows between the fighters")
            T.expect("a fight under the new combat table", a ~= nil and bb ~= nil and hits >= 3, ("%d blows landed"):format(hits))
            if a then a:kill() end
            if bb then bb:kill() end

            T.section("shipped templates the mod changes in place")
            -- The mod's files It_Axe_Ogreaxe / It_Axe_Pickaxe / It_2H_Uriziel / It_Axe_Trollfist carry the ENTITY names It_Axe_Ogre / It_Pickaxe / It_BS_RuneSword / It_2H_Berserk
            -- (the engine knows a template by that name, RE.md section 48): the shipped templates get new numbers, and what the world places (GUID) follows. Entity ids are
            -- from the game's own layers (Wild_Outdoor_Item, Pirate_Coast_Item, Dungeon_Titan_Items, BD_Waterfall_Items).
            local function has_stack(e, name)
                for i = 0, e:stack_count() - 1 do local s = e:stack_item(i) if s and s:name() == name then return true end end
                return false
            end
            local PLACED = {
                { "f0ab2fda-d678-492f-8235-30a82481f41c", "It_Pickaxe" }, { "b5ff1115-89ab-4151-8fad-bd95bb7d76b7", "It_Pickaxe" },
                { "a2196abd-8fb8-4f99-90e4-765e28ebc3f5", "It_BS_RuneSword" }, { "366a344c-8482-440f-aaed-d12ce618e413", "It_2H_Berserk" },
            }
            local badp, seen = {}, {}
            for _, pl in ipairs(PLACED) do
                local e, want = risen.entity.by_id(pl[1]), M.items[pl[2]]
                if not e then badp[#badp + 1] = pl[2] .. " " .. pl[1] .. ": not found"
                elseif not want then badp[#badp + 1] = pl[2] .. ": no data"
                else
                    -- the engine reports category and use type as numbers: a fresh spawn of the (changed) template is the reference
                    local ref = risen.entity.spawn(pl[2], (x or 0) + 100, (y or 0) + 10, z or 0, true)
                    local rc, ru = ref and ref:item_category(), ref and ref:item_use_type()
                    if ref then ref:kill() end
                    local got = { value = e:item_value(), category = e:item_category(), use = e:item_use_type(), damage = num(e:get_property("gCDamage_PS", "DamageAmount")), str = (e:requirements() or {}).Strength }
                    local wstr
                    for _, r in ipairs(want.requires or {}) do if r.skill == "Strength" then wstr = r.amount end end
                    seen[#seen + 1] = ("%s v%s cat %s/%s use %s/%s d%s s%s"):format(pl[2], tostring(got.value), tostring(got.category), tostring(rc), tostring(got.use), tostring(ru), tostring(got.damage), tostring(got.str))
                    if want.value and got.value ~= want.value then badp[#badp + 1] = pl[2] .. " value " .. tostring(got.value) end
                    if want.damage and got.damage ~= want.damage then badp[#badp + 1] = pl[2] .. " damage " .. tostring(got.damage) end
                    if want.category and got.category ~= rc then badp[#badp + 1] = pl[2] .. " category " .. tostring(got.category) end
                    if want.use_type and got.use ~= ru then badp[#badp + 1] = pl[2] .. " use type " .. tostring(got.use) end
                    if wstr and got.str ~= wstr then badp[#badp + 1] = pl[2] .. " strength " .. tostring(got.str) end
                end
            end
            T.eq("placed items carry the mod's data", badp, {})
            risen.log.info("extmod: placed items: " .. table.concat(seen, "; "))
            local karakos = risen.entity.by_id("3ea8344a-6c0e-4d0a-8de5-55dc4f182d33")
            local ogre = M.items.It_Axe_Ogre
            local kst
            if karakos then for i = 0, karakos:stack_count() - 1 do local s = karakos:stack_item(i) if s and s:name() == "It_Axe_Ogre" then kst = s end end end
            T.expect("Karakos carries the changed ogre axe", kst ~= nil and ogre ~= nil and kst:item_value() == ogre.value and num(kst:get_property("gCDamage_PS", "DamageAmount")) == ogre.damage,
                ("Karakos %s, stack value %s damage %s, want %s / %s"):format(tostring(karakos ~= nil), tostring(kst and kst:item_value()), tostring(kst and kst:get_property("gCDamage_PS", "DamageAmount")), tostring(ogre and ogre.value), tostring(ogre and ogre.damage)))
            -- the changed shipped weapons are weapons now: category Weapon, equippable once the requirements are met, also as stacks of the old name
            local unusable = {}
            for _, name in ipairs({ "It_Pickaxe", "It_Axe_Ogre", "It_BS_RuneSword", "It_2H_Berserk", "It_BS_RuneSword_01", "It_Axe_Trollfist", "It_Axe_Ogre_Old", "It_Pickaxe_Misc" }) do
                risen.item.give(p, name, 1)
                if not risen.item.can_use(p, name) then unusable[#unusable + 1] = name end
            end
            local eq_ok = risen.item.equip(p, "It_Pickaxe")
            T.sleep(20)
            T.eq("renamed weapons are equippable", { unusable, eq_ok }, { {}, true })
            -- Oscar's chapter 3 list names the runesword (the mod's It_BS_RuneSword_01); the chapter info runs this script, here it is called the way main.lua stocks chapter 1
            local oscar = risen.entity.get("Oscar")
            local sold = false
            for _, t in ipairs(DLG.trade) do
                if t.chapter == 3 then
                    for _, en in ipairs(t.entries) do
                        if en.trader == "Oscar" and en.item == "It_BS_RuneSword_01" and oscar then
                            local item = risen.entity.spawn(en.item, (x or 0) + 100, (y or 0) + 10, z or 0)
                            if item then pcall(function() risen.script.call("OnTradeInfo_RefillTradeItems", oscar, item, en.count) end) item:kill() end
                            sold = has_stack(oscar, "It_BS_RuneSword_01")
                        end
                    end
                end
            end
            T.expect("Oscar sells the runesword from chapter 3", sold, "Oscar " .. tostring(oscar ~= nil))

            -- keep a few things for the load phase
            risen.world.quick_save()
            T.wait_event("save.after", 60)
            T.sleep(200)
        else
            T.section("load")
            local missing = {}
            for _, name in ipairs({ "It_Necklace_Pearl" }) do
                if not risen.template.get(name) then missing[#missing + 1] = name end
            end
            local sample = {}
            for _, name in ipairs(sorted(M.new)) do
                if M.new[name].mesh and M.new[name].damage and #sample < 6 then sample[#sample + 1] = name end
            end
            for _, name in ipairs(sample) do if not risen.template.get(name) then missing[#missing + 1] = name end end
            T.eq("load: the templates resolve", missing, {})
            local lost = {}
            for _, name in ipairs(sample) do if count(name) < 1 then lost[#lost + 1] = name end end
            if count("It_Necklace_Pearl") < 1 then lost[#lost + 1] = "It_Necklace_Pearl" end
            T.eq("load: items are still in the inventory", lost, {})
            T.eq("load: the numbers are applied again", check_numbers("load"), {})
            local gone = {}
            for _, name in ipairs({ "It_Pickaxe", "It_Axe_Ogre", "It_BS_RuneSword", "It_2H_Berserk", "It_BS_RuneSword_01", "It_Axe_Trollfist" }) do
                if count(name) < 1 then gone[#gone + 1] = name end
            end
            T.eq("load: stacks of the changed shipped items survive", gone, {})
        end
    end, "extmod")
end
