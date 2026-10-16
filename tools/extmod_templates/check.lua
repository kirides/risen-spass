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
        },
        load = { "load: the templates resolve", "load: items are still in the inventory", "load: the numbers are applied again" },
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
        end
    end, "extmod")
end
