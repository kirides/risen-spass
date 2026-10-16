---@diagnostic disable: need-check-nil, param-type-mismatch, cast-local-type, assign-type-mismatch, undefined-field
-- Live scenario `ex_ignore_theft` (only loaded in a test run, see main.lua; needs the mod `quickloot` for the quick path): a Peasant who looks at the player inside a watched area
-- (AreaZone_Default with the area name SL_Public) reacts to a theft without the variable (control) and not with it, for the game's animated take and for quick loot; clearing the variable
-- brings the reaction back.
local T = risen.test

return function(stats, VARIABLE)
    T.plan {
        "the area is watched and the witness looks at the player", "control: AssessTheft answers 1 without the variable", "variable set: AssessTheft answers 0 (no reaction)",
        "control: the animated take makes the witness react", "variable set: the animated take of an item, nobody reacts", "variable set: the quick take of an item, nobody reacts",
        "variable set: the animated opening of a chest, nobody reacts", "variable set: the quick opening of a chest, nobody reacts",
        "the handler was asked and answered 0 for each of them", "variable removed: the quick take makes the witness react again",
    }

    local p
    local last
    risen.on("quickloot.result", function(r) last = r end)
    local function nm(v) return v and v.name and v:name() or "-" end
    local function snap(n)
        return { mode = n:get_property("gCScriptRoutine_PS", "AIMode"), comment = n:get_property("gCNPC_PS", "LastPlayerComment"), reason = n:get_property("gCNPC_PS", "Reason"), state = n:state() }
    end
    local function reacted(s) return s.comment == "gEComment_Theft" and s.reason == "gEReason_Theft" end
    local function describe(s) return ("%s/%s/%s/%s"):format(tostring(s.state), tostring(s.mode), tostring(s.comment), tostring(s.reason)) end
    local function put(tpl, dx, dz)
        local x, y, z = p:position()
        local e = assert(risen.entity.spawn(tpl, x + dx, y + 20, z + dz), "spawn failed: " .. tpl)
        T.sleep(3)
        return e
    end
    local function witness()       -- 300 units south of the player; NPCs look along +z, so he looks at the player
        local w = put("Peasant_1_01", 0, -300)
        T.sleep(30)
        return w
    end
    local function chest(ang)
        local x, y, z = p:position()
        local a = math.rad(ang)
        local c = assert(risen.entity.spawn("Object_Interact_Animated_Chest_Poor", x + math.cos(a) * 200, y + 20, z + math.sin(a) * 200))
        T.sleep(3)
        c:set_property("gCLock_PS", "Status", "gELockStatus_Unlocked")
        risen.item.give(c, "It_Apple", 2)
        T.sleep(3)
        return c
    end
    local function ready()
        T.wait_until(function() return (p:state() or ""):sub(1, 9) == "PS_Normal" end, 60, "player free")
        T.sleep(10)
    end
    -- the game's own interaction (walks, kneels, the loot window for chests, takes everything)
    local function animated(target, loot_window)
        risen.interact.begin(p, target)
        T.sleep(5)
        if loot_window then
            if T.wait_until(function() local h = risen.hud.state(); return h and h.loot end, 30, "loot window") then
                T.sleep(10)
                risen.script.call("BtnEvent_OnClick_LootAll", p, target)
            end
        end
        ready()
        T.sleep(40)
    end
    local function quick(target)
        last = nil
        risen.emit("quickloot.try", target)
        T.sleep(60)
        return last or {}
    end

    T.run(function()
        T.wait_event("session.start", 180, function(m) return m == "new" end)
        p = assert(risen.entity.player())
        T.wait_until(function() return p:state() == "PS_Normal_Loop" end, 120, "intro over")
        T.sleep(80)
        p:set_hp_max(5000); p:set_hp(5000)
        risen.gamevar.remove(VARIABLE)

        T.section("the watched area")
        local x, y, z = p:position()
        local zone = assert(risen.entity.spawn("AreaZone_Default", x, y, z))
        T.sleep(5)
        zone:set_area_names({ "SL_Public" })        -- a box of +-500 units around the zone: the beach becomes a "house"
        local w = witness()
        local item = put("It_Apple", 0, 100)
        local sees = risen.world.can_see(w, p)
        T.expect("the area is watched and the witness looks at the player", sees == true and table.concat(zone:area_names() or {}, ",") == "SL_Public", tostring(sees))

        T.section("the engine's answer")
        T.eq("control: AssessTheft answers 1 without the variable", risen.script.call("AssessTheft", w, item), 1)
        risen.gamevar.set(VARIABLE, 1)
        T.eq("variable set: AssessTheft answers 0 (no reaction)", risen.script.call("AssessTheft", w, item), 0)
        T.sleep(5)
        w:kill(); T.sleep(10)

        T.section("control: the animated take without the variable")
        risen.gamevar.remove(VARIABLE)
        w = witness()
        local apple = put("It_Apple", 0, 100)
        animated(apple, false)
        local s = snap(w)
        T.expect("control: the animated take makes the witness react", reacted(s), describe(s))
        w:kill(); T.sleep(10)

        T.section("the variable is set: nobody reacts")
        risen.gamevar.set(VARIABLE, 1)
        local ignored0, asked0 = stats.ignored, stats.asked
        w = witness()
        apple = put("It_Apple", 0, 100)
        local n = risen.item.count(p, "It_Apple")
        animated(apple, false)
        s = snap(w)
        T.expect("variable set: the animated take of an item, nobody reacts", risen.item.count(p, "It_Apple") == n + 1 and not reacted(s), describe(s))
        w:kill(); T.sleep(10)

        w = witness()
        apple = put("It_Apple", 0, 100)
        n = risen.item.count(p, "It_Apple")
        local r = quick(apple)
        s = snap(w)
        T.expect("variable set: the quick take of an item, nobody reacts", r.handled and r.assessed == true and r.reacted == false and risen.item.count(p, "It_Apple") == n + 1 and not reacted(s),
            tostring(r.reason) .. " reacted=" .. tostring(r.reacted) .. " " .. describe(s))
        w:kill(); T.sleep(10)

        w = witness()
        local c1 = chest(57.3)
        n = risen.item.count(p, "It_Apple")
        animated(c1, true)
        s = snap(w)
        T.expect("variable set: the animated opening of a chest, nobody reacts", risen.item.count(p, "It_Apple") == n + 2 and not reacted(s), describe(s))
        w:kill(); T.sleep(10)

        w = witness()
        local c2 = chest(120)
        n = risen.item.count(p, "It_Apple")
        r = quick(c2)
        s = snap(w)
        T.expect("variable set: the quick opening of a chest, nobody reacts", r.handled and r.reacted == false and risen.item.count(p, "It_Apple") == n + 2 and not reacted(s),
            tostring(r.reason) .. " reacted=" .. tostring(r.reacted) .. " " .. describe(s))
        T.expect("the handler was asked and answered 0 for each of them", stats.ignored - ignored0 >= 4 and stats.ignored - ignored0 == stats.asked - asked0,
            ("asked %d, ignored %d"):format(stats.asked - asked0, stats.ignored - ignored0))
        w:kill(); T.sleep(10)

        T.section("the variable is removed: the game reacts again")
        risen.gamevar.remove(VARIABLE)
        w = witness()
        apple = put("It_Apple", 0, 100)
        r = quick(apple)
        s = snap(w)
        T.expect("variable removed: the quick take makes the witness react again", r.handled and r.reacted == true and reacted(s), tostring(r.reason) .. " " .. describe(s))
        w:kill()
    end, "ex_ignore_theft")
end
