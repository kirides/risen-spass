-- Development aid: dumps the engine's property names and types (reflection) for entity property sets, quests and every dialogue
-- command, as "PROP <object> <name>:<type> ..." log lines. tools/gen_properties_doc.py turns them into docs/PROPERTIES.md.
--   scripts/test-run.ps1 -Mods propdump -Seconds 120
local log = risen.log
local frames = 0

local function dump(label, props)
    if not props then
        log.info(("PROP %s <unavailable>"):format(label))
        return
    end
    local parts = {}
    for _, p in ipairs(props) do parts[#parts + 1] = p.name .. ":" .. p.type end
    log.info(("PROP %s %s"):format(label, table.concat(parts, " ")))
end

risen.on("tick", function()
    frames = frames + 1
    if frames ~= 300 then return end
    local player = risen.entity.player()
    if not player then return end
    local x, y, z = player:position()

    -- entity property sets (NPC, an item, the player)
    local npc = risen.entity.spawn("Peasant_1_01", x + 100, y + 20, z + 100)
    local sword = risen.entity.spawn("It_1H_Sword", x + 150, y + 20, z + 100)
    for _, set in ipairs({ "gCNPC_PS", "gCInventory_PS", "gCSkills_PS", "gCScriptRoutine_PS", "gCCharacterMovement_PS", "gCInteraction_PS", "gCParty_PS",
                           "gCNavigation_PS", "gCStateGraph_PS", "gCDialog_PS", "gCEffect_PS", "gCLock_PS", "gCClock_PS", "gCArena_PS", "gCDoor_PS", "gCAIZone_PS",
                           "gCAnchor_PS", "gCGameScript_PS", "gCSectorPersistence_PS" }) do
        dump("entity-set " .. set .. " (npc)", npc and npc:list_properties(set))
    end
    dump("entity-set gCItem_PS (item)", sword and sword:list_properties("gCItem_PS"))
    dump("entity-set gCNPC_PS (player)", player:list_properties("gCNPC_PS"))
    if npc then npc:kill() end
    if sword then sword:kill() end

    -- quest
    local q = risen.quest.define({ name = "Q_SPASS_PropDump", title = "dump" })
    if q then dump("quest", q:list_properties()) end

    -- info and every command that can be inserted by name
    local info = risen.dialog.new_info("SPASS_PropDump_Info", "SPASS_PropDump")
    if info then
        dump("info", info:list_properties())
        local names = {}
        for name in pairs(risen.dialog.command_table()) do names[#names + 1] = name end
        table.sort(names)
        for _, name in ipairs(names) do
            local idx = info:add_command(name)
            if idx then dump("command " .. name, info:list_command_properties(idx)) end
        end
        local sidx = info:add_script_command()
        if sidx then dump("command RunScript", info:list_command_properties(sidx)) end
        local lidx = info:add_log_command()
        if lidx then dump("command AddLogText", info:list_command_properties(lidx)) end
    end
end)
