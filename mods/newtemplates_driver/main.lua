-- Plays the savegame scenarios of the newtemplates mod unattended (docs/RE.md section 25) as risen.test scenarios: scripts/smoke-test.ps1 -Scenario newtemplates.
-- One scenario, several runs (each a fresh game process; -Param phase=...):
--   save           new game -> the newtemplates verification -> a dialogue `Give` of a clone item -> items of clones into the player's and a world NPC's
--                  inventory -> quick save -> take them away -> quick load (same process: do the stacks come back?) -> new game (do the clones live on?)
--   load           fresh process, `-Start load`: the mod creates its templates on the first tick, before the savegame is read: items still there and resolved?
--   load_missing   fresh process, the mod creates nothing: what happens to the saved items; a quick save in that state
--   load_again     fresh process with the templates again: did the items survive a session without them (and the re-save)?
--   load_late      fresh process, the templates are created only after the savegame was read (session.start)
local T = risen.test
local log = risen.log
local phase = T.param("phase", "save")
local function L(...)
    local t = {}
    for i = 1, select("#", ...) do t[#t + 1] = tostring((select(i, ...))) end
    log.info("newtpldriver[" .. phase .. "] " .. table.concat(t, " "))
end

local function player_now() return assert(risen.entity.player(), "no player") end
local SWORD, BERRY = "Spass_Flame_Sword", "Spass_Healing_Berry"
local GIVER, GIFT = "SPASS_NewTpl_Giver", "SPASS_NewTpl_Gift"


local PLAN_SAVE = {
    "the spec is applied on the first tick",                   -- newtemplates (risen.balance.install on_applied)
    "every template of the spec was created",                  -- newtemplates verification
    "the clone has the property sets of its base",
    "the clone has the ID derived from its name",
    "the clone is a different template than its base",
    "item overrides read back",
    "the base template is untouched",
    "a second item clone has its own numbers",
    "the label comes from FO_<name>",
    "the description comes from ITEMDESC_<name>",
    "an NPC clone has its label too",
    "a spawn uses the new numbers",
    "the spawn is created from the clone",
    "the mesh override reaches the engine's resource list",
    "the icon override is read by the engine",
    "the spawn's label is the clone's",
    "an item of the clone can be given",
    "the inventory stack is the clone template",
    "the engine check follows the clone's requirement",
    "create again re-applies the overrides to the existing clone",
    "a lowered requirement passes the check",
    "an NPC clone spawns with its own stats",
    "the base NPC is untouched",
    "the clone keeps the species and carries its loot",
    "an NPC clone with another body loads that actor",
    "a human clone has its own stats",
    "invalid requests are refused with a reason",
    "a refused request creates nothing",
    "a bad spec is refused as a whole",
    "remove gives the name of a clone back",
    "create brings a removed clone back with the same ID",
    "remove is refused while entities of the clone exist",
    "remove refuses templates this layer did not create",
    "session.start applies it again (idempotent)",
    "a dialogue Give command hands out a clone item",
    "items of clones are in the inventories before the save",
    "same process: the clone items come back with the load",
    "same process: the world NPC's clone items come back",
    "same process: the stack still resolves to the clone",
    "new game: the clones are still there and unchanged",
    "new game: the player starts without them",
}
local PLAN_LOAD = {
    "the spec is applied on the first tick",
    "session.start applies it again (idempotent)",
    "fresh process: the player's items resolved to the clone",
    "fresh process: the world NPC's items resolved to the clone",
    "fresh process: the clone has the numbers of the spec",
    "fresh process: the clone has the same ID",
    "fresh process: the label of the item is shown",
}
local PLANS = {
    save = PLAN_SAVE,
    load = PLAN_LOAD,
    load_again = PLAN_LOAD,
    load_late = {
        "the spec is applied on the first tick",
        "fresh process: the player's items resolved to the clone",
        "fresh process: the world NPC's items resolved to the clone",
        "fresh process: the clone has the numbers of the spec",
        "fresh process: the clone has the same ID",
        "fresh process: the label of the item is shown",
    },
    load_missing = {
        "the template is missing in this session",
        "the dormant stack does not stop the game",
        "a save without the templates succeeds",
    },
}
T.plan(PLANS[phase] or PLAN_SAVE)

local sessions = {}   -- sessions[mode] = how many times session.start(mode) fired
local saved = false
risen.on("save.after", function(name, ok) saved = ok; L("save.after", name, ok) end)
risen.on("session.start", function(mode) sessions[mode] = (sessions[mode] or 0) + 1; L("session.start", mode, "#" .. sessions[mode]) end)
local function wait_session(mode, n, timeout)
    T.wait_until(function() return (sessions[mode] or 0) >= n end, timeout, "session.start(" .. mode .. ") #" .. n)
end

-- the world NPC the clone items are given to: the Peasant_1_01 with the smallest unique name (the same one in every run)
local function world_npc()
    local tpl = risen.template.get("Peasant_1_01")
    local best
    for _, e in ipairs(tpl and tpl:instances(100) or {}) do
        if not best or e:name() < best:name() then best = e end
    end
    return assert(best, "no Peasant_1_01 in the world")
end

local function define_gift()
    risen.dialog.create({
        name = GIFT, owner = GIVER, permanent = true,
        lines = {
            { speaker = "player", listener = "npc", text = "Do you have a sword for me?" },
            { command = "Give", Entity1 = "npc", Entity2 = "player", Item = SWORD, Amount = 1 },
            { command = "ShowSubtitle", text = "Take the Flame Tongue." },
            { command = "End" },   -- an open conversation blocks saving (World::IsSaveAllowed is false) and a save then crashed the game
        },
    })
end

-- ---- the fresh-process phases -------------------------------------------------------------------------------------------------------

local function verify_loaded()
    local player = player_now()
    local npc = world_npc()
    local t = risen.template.get(SWORD)
    local stack
    for i = 0, (player:stack_count() or 0) - 1 do
        local it = player:stack_item(i)
        if it and it:name() == SWORD then stack = it end
    end
    L("player swords", risen.item.count(player, SWORD), "berries", risen.item.count(player, BERRY), "npc", npc and npc:name(), npc and risen.item.count(npc, SWORD))
    T.eq("fresh process: the player's items resolved to the clone", { risen.item.count(player, SWORD), risen.item.count(player, BERRY),
        stack ~= nil and stack:item_value() or -1 }, { 3, 2, 900 })
    T.eq("fresh process: the world NPC's items resolved to the clone", npc and risen.item.count(npc, SWORD), 2)
    T.expect("fresh process: the clone has the numbers of the spec", t ~= nil and t:get_property("gCItem_PS", "GoldValue") == "900"
        and risen.item.requirements(SWORD).Strength == 25 and t:get_property("eCMesh_PS", "MeshFileName") == "It_Wpn_1H_Club.xmsh",
        t and t:get_property("gCItem_PS", "GoldValue") or "no template")
    T.eq("fresh process: the clone has the same ID", t and t:id(), risen.template.id_for(SWORD))
    T.eq("fresh process: the label of the item is shown", t and t:focus_name(), risen.loc.language() == "German" and "Flammenzunge" or "Flame Tongue")
end

if phase == "load" or phase == "load_again" or phase == "load_late" then
    T.run(function()
        wait_session("load", 1, 240)
        T.sleep(200)
        verify_loaded()
        T.finish(phase .. " done")
    end, "newtemplates")
    return
end

if phase == "load_missing" then
    T.run(function()
        wait_session("load", 1, 240)
        T.sleep(200)
        local player = player_now()
        local npc = world_npc()
        L("template exists:", risen.template.exists(SWORD), "player count", risen.item.count(player, SWORD), "stacks", player:stack_count(), "npc stacks", npc and npc:stack_count())
        T.expect("the template is missing in this session", not risen.template.exists(SWORD) and risen.item.count(player, SWORD) == 0,
            "exists " .. tostring(risen.template.exists(SWORD)))
        T.sleep(400)   -- the game keeps running with the dormant stacks (AI, inventory scripts)
        T.expect("the dormant stack does not stop the game", player_now() ~= nil and player:hp() ~= nil, "player hp " .. tostring(player:hp()))
        saved = false
        L("quick_save", risen.world.quick_save())
        T.wait_until(function() return saved end, 120, "save.after")
        T.expect("a save without the templates succeeds", saved, tostring(saved))
        T.finish("load_missing done")
    end, "newtemplates")
    return
end

-- ---- phase "save" -------------------------------------------------------------------------------------------------------------------

risen.on("load.before", define_gift)
risen.on("session.start", function() define_gift() end)

T.run(function()
    T.section("first session")
    wait_session("new", 1, 240)
    T.wait_until(function() return risen.gamevar.get("SPASS_NEWTPL_DONE") == 1 end, 240, "newtemplates verification")
    T.sleep(10)
    local player = player_now()
    L("verification done; swords in the inventory", risen.item.count(player, SWORD))

    T.section("a dialogue Give with a clone item")
    local x, y, z = player:position()
    risen.npc.spawn("Peasant_1_01", x + 150, y + 20, z + 150, { name = GIVER, peaceful = true })
    define_gift()
    T.sleep(200)
    local giver = risen.entity.get(GIVER)
    local info = risen.dialog.get(GIFT)
    local before = risen.item.count(player, SWORD)
    L("giver", giver, "info", info, "swords before", before)
    if giver and info then
        L("dialog.start ->", risen.dialog.start(player, giver))
        T.sleep(120)
        L("info:run ->", info:run())
        T.wait_until(function() return risen.item.count(player, SWORD) >= before + 1 end, 1500, "the Give command")
    end
    T.expect("a dialogue Give command hands out a clone item", risen.item.count(player, SWORD) == before + 1, ("%s %d -> %d"):format(SWORD, before, risen.item.count(player, SWORD)))

    -- the conversation must be over before anything is saved (a save while it is still closing crashed the game)
    T.wait_until(function() return not risen.dialog.is_running() end, 900, "the dialogue to close")
    T.sleep(120)
    giver = risen.entity.get(GIVER)
    if giver then giver:kill() end
    T.wait_until(function() return risen.world.is_save_allowed() end, 600, "saving to be allowed")

    T.section("items of clones in inventories, quick save")
    player = player_now()
    local npc = world_npc()
    risen.item.give(player, BERRY, 2)
    risen.item.give(npc, SWORD, 2)
    local ids_before = risen.template.get(SWORD):id()
    L("before the save: player swords", risen.item.count(player, SWORD), "berries", risen.item.count(player, BERRY), "npc", npc:name(), risen.item.count(npc, SWORD))
    T.eq("items of clones are in the inventories before the save", { risen.item.count(player, SWORD), risen.item.count(player, BERRY), risen.item.count(npc, SWORD) }, { 3, 2, 2 })
    T.sleep(30)
    L("quick_save", risen.world.quick_save())
    T.wait_until(function() return saved end, 120, "save.after")
    T.sleep(60)
    -- take everything away: a load must bring it back
    risen.item.take(player, SWORD, 3)
    risen.item.take(player, BERRY, 2)
    risen.item.take(npc, SWORD, 2)
    L("after taking: player swords", risen.item.count(player, SWORD), "npc", risen.item.count(npc, SWORD))
    T.sleep(30)

    T.section("quick load in the same process")
    L("quick_load", risen.world.quick_load())
    wait_session("load", 1, 240)
    T.sleep(250)
    player = player_now()
    npc = world_npc()
    local t = risen.template.get(SWORD)
    local stack
    for i = 0, (player:stack_count() or 0) - 1 do
        local it = player:stack_item(i)
        if it and it:name() == SWORD then stack = it end
    end
    L("after load: player swords", risen.item.count(player, SWORD), "berries", risen.item.count(player, BERRY), "npc", risen.item.count(npc, SWORD))
    T.eq("same process: the clone items come back with the load", { risen.item.count(player, SWORD), risen.item.count(player, BERRY) }, { 3, 2 })
    T.eq("same process: the world NPC's clone items come back", risen.item.count(npc, SWORD), 2)
    T.expect("same process: the stack still resolves to the clone", stack ~= nil and stack:item_value() == 900 and stack:focus_name() ~= "" and tostring(stack:template()) ~= "",
        stack and (tostring(stack:item_value()) .. " " .. stack:focus_name()) or "no stack")

    T.section("a new game in the same process")
    L("new_game", risen.world.new_game())
    wait_session("new", 2, 240)
    T.sleep(250)
    player = player_now()
    t = risen.template.get(SWORD)
    local numbers_ok = t ~= nil and t:id() == ids_before and t:get_property("gCItem_PS", "GoldValue") == "900" and risen.item.requirements(SWORD).Strength == 25
        and risen.template.get("Spass_Dire_Wolf") ~= nil
    L("new game: template", t, "id", t and t:id(), "player swords", risen.item.count(player, SWORD))
    T.expect("new game: the clones are still there and unchanged", numbers_ok, t and t:id() or "gone")
    T.eq("new game: the player starts without them", risen.item.count(player, SWORD), 0)
    T.finish("newtemplates save phase done")
end, "newtemplates")
