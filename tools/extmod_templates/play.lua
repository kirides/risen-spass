---@diagnostic disable: need-check-nil, param-type-mismatch, undefined-field
-- Play checks of the generated risen_extended mod (only loaded in a test run, -Param check=extplay): the real dialogue / engine paths that check.lua only reads back.
--   .\scripts\test-run.ps1 -Mods risen_extended -WaitForResult -Seconds 400 -Scenario extplay -Param check=extplay -ShowChecks
--   commands    GiveXP and RunScript(OnTradeInfo_RefillTradeItems) commands added the way main.lua does it run through a real conversation (shipped infos cannot be played: see below)
--   teaching    Sam (DEX limit 200 in the mod, 100 in the game): the +5 option is offered at DEX 100 and accepted, refused when it would pass the limit
--   combat      the style the engine picks for hand / skill combinations follows the loaded melee table (risen.npc.combat_style)
local T = risen.test

local function wait_s(s)
    local t0 = T.elapsed()
    T.wait_until(function() return T.elapsed() - t0 >= s end, s + 15, "wait " .. s .. " s")
end
local function has_stack(e, name)
    for i = 0, e:stack_count() - 1 do local s = e:stack_item(i) if s and s:name() == name then return true end end
    return false
end
local function names(list)
    local out = {}
    for _, inf in ipairs(list or {}) do out[#out + 1] = inf:name() end
    return out
end
local function offered(info)
    for _, inf in ipairs(risen.dialog.available() or {}) do if inf:name() == info then return true end end
    return false
end
local function say_goodbye()
    -- leave a running conversation (the "END" option is the last offered info)
    for _, inf in ipairs(risen.dialog.available() or {}) do
        if inf:get_command_property(1, "Text") == "END" then inf:run() return end
    end
end

return function(M)
    T.plan {
        "added GiveXP pays through a conversation", "added RunScript refill stocks the trader", "teaching: +5 offered at DEX 100 (limit 200)", "teaching: +5 accepted", "teaching: refused at the old limit 100", "teaching: refused near the new limit",
        "melee: one-hand weapon follows the Sword skill", "melee: two-hand weapon follows the Axe skill", "melee: staff follows the Staff skill", "melee: a two-hand weapon without the skill has no style",
    }
    local DEX, STR, LP, SWORD, AXE, STAFF = 8, 7, 4, 16, 17, 18

    T.run(function()
        T.wait_event("session.start", 240, function(m) return m == "new" end)
        wait_s(3)
        pcall(risen.gui.close, 26)
        wait_s(20)
        local p = assert(risen.entity.player())
        risen.npc.set_stats(p, { hp_max = 9000, hp = 9000 })
        risen.item.give(p, "It_Gold", 50000)

        -- ---- added commands run: the mod appends GiveXP (pickpocket lessons ...) and RunScript refills (chapter infos) to shipped infos. A shipped info cannot be played to its
        -- commands here: its voiced Say lines never end in this setup (the conversation stays on the first line), and the pickpocket lessons are only offered in pickpocket mode.
        -- So the same two commands are added, with the same code as main.lua, to an info of our own and run through a real conversation with Sam. ----
        T.section("added commands")
        local sam = assert(risen.entity.get("Sam"), "no Sam")
        do
            p:teleport(sam)
            wait_s(6)
            local info = risen.dialog.create({ name = "EXT_Play_Commands", owner = "Sam", permanent = true,
                lines = { { speaker = "player", listener = "npc", text = "Any news?" } } })
            assert(info, "cannot create the test info")
            local ix = assert(info:add_command("GiveXP"))
            assert(info:set_command_property(ix, "Amount", "100"))
            local ch2
            for _, t2 in ipairs(M.dlg.trade) do if t2.chapter == 2 then ch2 = t2 end end
            local ent = ch2.entries[#ch2.entries]
            local trader = assert(risen.entity.get(ent.trader))
            local before = has_stack(trader, ent.item)
            local is = assert(info:add_script_command())
            assert(info:set_script(is, "OnTradeInfo_RefillTradeItems"))
            for k, v in pairs({ Self = ent.trader, Other = ent.item, OtherType = "gEOtherType_TemplateEntity", Param = ent.count }) do assert(info:set_command_property(is, k, tostring(v))) end
            risen.dialog.rebuild()
            local xp0 = risen.npc.stats(p).experience
            assert(risen.dialog.start(p, sam), "dialogue did not start")
            wait_s(3)
            local shown = offered("EXT_Play_Commands")
            local ran = info:run()
            wait_s(15)
            local xp1 = risen.npc.stats(p).experience
            T.expect("added GiveXP pays through a conversation", shown and ran and xp1 - xp0 == 100, ("offered %s, run %s, xp %d -> %d"):format(tostring(shown), tostring(ran), xp0, xp1))
            T.expect("added RunScript refill stocks the trader", (not before) and has_stack(trader, ent.item), ("%s %s: before %s, after %s"):format(ent.trader, ent.item, tostring(before), tostring(has_stack(trader, ent.item))))
            say_goodbye()
            wait_s(3)
        end

        -- ---- teaching: Sam's DEX limit ----
        T.section("teaching")
        do
            sam:set_property("gCDialog_PS", "TeachEnabled", "True")
            local function topic()
                -- opens the "What can you teach me about hunting?" topic and reports which of +1 / +5 are offered
                assert(risen.dialog.start(p, sam), "dialogue did not start")
                wait_s(3)
                local parent = assert(risen.dialog.get("KALVERAMX2B_00254"))
                parent:run()
                wait_s(8)
                return offered("KALVERAMX2B_00253"), offered("KALVERAMX2B_00255")
            end
            p:teleport(sam)
            wait_s(6)
            risen.npc.set_skill_base(p, DEX, 100)
            risen.npc.set_skill_base(p, LP, 100)
            local plus1, plus5 = topic()
            T.expect("teaching: +5 offered at DEX 100 (limit 200)", plus1 and plus5, ("+1 %s +5 %s"):format(tostring(plus1), tostring(plus5)))
            local lp0 = select(1, risen.npc.skill_raw(p, LP))
            risen.dialog.get("KALVERAMX2B_00255"):run()
            wait_s(10)
            local dex1, lp1 = select(1, risen.npc.skill_raw(p, DEX)), select(1, risen.npc.skill_raw(p, LP))
            T.expect("teaching: +5 accepted", dex1 == 105 and lp1 == lp0 - 5, ("DEX %s LP %s -> %s"):format(tostring(dex1), tostring(lp0), tostring(lp1)))
            say_goodbye()
            wait_s(3)
            -- back at the game's own limit of 100: nothing may be offered for a player at 105
            assert(risen.npc.set_teach_limit(sam, "gESkill_Stat_DEX", 100))
            local a1, a5 = topic()
            T.expect("teaching: refused at the old limit 100", (not a1) and (not a5), ("+1 %s +5 %s"):format(tostring(a1), tostring(a5)))
            say_goodbye()
            wait_s(3)
            assert(risen.npc.set_teach_limit(sam, "gESkill_Stat_DEX", 200))
            risen.npc.set_skill_base(p, DEX, 197)
            local b1, b5 = topic()
            T.expect("teaching: refused near the new limit", b1 and not b5, ("at DEX 197: +1 %s +5 %s"):format(tostring(b1), tostring(b5)))
            say_goodbye()
            wait_s(3)
            risen.npc.set_skill_base(p, DEX, 20)
        end

        -- ---- the melee table: which style the engine would use ----
        T.section("combat")
        do
            local function style(hands, sword, axe, staff)
                risen.npc.set_skill_base(p, SWORD, sword); risen.npc.set_skill_base(p, AXE, axe); risen.npc.set_skill_base(p, STAFF, staff)
                local s, why = risen.npc.combat_style(p, hands)
                return s and s.name or ("nil:" .. tostring(why):sub(1, 40)), s and s.hit_range
            end
            local n1, r1 = style({ right = "1H" }, 5, 0, 0)
            local n1b = style({ right = "1H" }, 0, 5, 0)
            T.expect("melee: one-hand weapon follows the Sword skill", n1:find("S5", 1, true) ~= nil, ("Sword 5, 1H -> %s (range %s); Axe 5 only -> %s"):format(n1, tostring(r1), n1b))
            local n2, r2 = style({ right = "2H" }, 0, 5, 0)
            local n2s = style({ right = "2H" }, 5, 0, 0)
            T.expect("melee: two-hand weapon follows the Axe skill", n2:find("2H", 1, true) ~= nil or n2:find("Axe", 1, true) ~= nil, ("Axe 5, 2H -> %s (range %s); Sword 5 only -> %s"):format(n2, tostring(r2), n2s))
            local n3, r3 = style({ right = "Staff" }, 0, 0, 5)
            T.expect("melee: staff follows the Staff skill", n3:find("Staff", 1, true) ~= nil, ("Staff 5 -> %s (range %s)"):format(n3, tostring(r3)))
            T.expect("melee: a two-hand weapon without the skill has no style", n2s:sub(1, 4) == "nil:", "2H with Sword 5 and Axe 0 -> " .. n2s)
            risen.npc.set_skill_base(p, SWORD, 0); risen.npc.set_skill_base(p, AXE, 0); risen.npc.set_skill_base(p, STAFF, 0)
        end
        T.finish("extplay done")
    end, "extplay")
end
