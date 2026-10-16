-- Live check of example_dialog_when (only loaded in a test run).
local T = risen.test

return function(RURIK)
    T.plan { "new game: Rurik exists", "plan: the secret is one engine condition and one Lua condition", "plan: the hideout is one Lua condition", "start: only the chat is offered",
             "chat: trust grows", "chat: the game event is set", "after one chat: the hideout is offered (event OR guild)", "after one chat: the secret is not", "two chats: still no secret",
             "ore but no bandit: no secret", "bandit with ore and trust: the secret is offered", "the conversation lists it", "the secret was used: the ore is taken", "the secret was used: five apples", "the secret is gone",
             "the secret stays gone after a load", "the hideout is still offered after the load", "a bandit without ore, without the event: the hideout only through the guild" }

    local function names()
        local out = {}
        for _, o in ipairs(risen.dialog.available()) do
            local n = o:name()
            if n and n:find("^EXD_") then out[#out + 1] = n end
        end
        table.sort(out)
        return out
    end
    -- the options the engine offers right now (Info:available(): what it would put into the list), without opening a conversation
    local function offered()
        local out = {}
        for _, n in ipairs { "EXD_Rurik_Chat", "EXD_Rurik_Hideout", "EXD_Rurik_Secret" } do
            local info = risen.dialog.get(n)
            if info and info:available() then out[#out + 1] = n end
        end
        return out
    end
    local function converse(player, npc, info_name)
        assert(risen.dialog.start(player, npc))
        T.sleep(90)
        local list = names()
        if info_name then
            assert(risen.dialog.get(info_name), info_name):run()
            T.wait_event("info.finished", 60, function(name) return name == info_name end)
            T.sleep(240)         -- the commands (Give, End) run one after the other over a few seconds
        end
        return list
    end
    local function settled()
        T.wait_until(function() return risen.entity.get(RURIK) ~= nil and risen.entity.player() ~= nil end, 90, "Rurik exists")
        T.sleep(60)
    end

    T.run(function()
        T.wait_event("session.start", 180, function(m) return m == "new" end)
        settled()
        local player = assert(risen.entity.player())
        local rurik = assert(risen.entity.get(RURIK))
        T.expect("new game: Rurik exists", rurik:valid())
        player:set_guild(risen.npc.guilds.Cit)             -- a known starting point: not a bandit
        risen.item.take(player, "It_IronOre", 99)

        local secret = assert(risen.dialog.describe("EXD_Rurik_Secret"))
        T.eq("plan: the secret is one engine condition and one Lua condition",
            { #secret.engine, secret.engine[1]:match("^%w+"), secret.lua }, { 1, "ItemAmount", "all(var EXD_Trust >= 2, player guild is Pir)" })
        T.eq("plan: the hideout is one Lua condition", { #assert(risen.dialog.describe("EXD_Rurik_Hideout")).engine, assert(risen.dialog.describe("EXD_Rurik_Hideout")).lua }, { 0, "any(game event EXD_Heard is set, player guild is Pir)" })

        T.eq("start: only the chat is offered", converse(player, rurik, nil), { "EXD_Rurik_Chat" })
        -- (the conversation is still open: choose the chat, it ends the conversation)
        assert(risen.dialog.get("EXD_Rurik_Chat")):run()
        T.wait_event("info.finished", 60, function(name) return name == "EXD_Rurik_Chat" end)
        T.sleep(240)
        T.eq("chat: trust grows", risen.gamevar.get("EXD_Trust"), 1)
        T.expect("chat: the game event is set", risen.gamevar.exists("EXD_Heard"))
        T.eq("after one chat: the hideout is offered (event OR guild)", offered(), { "EXD_Rurik_Chat", "EXD_Rurik_Hideout" })
        T.expect("after one chat: the secret is not", not risen.dialog.get("EXD_Rurik_Secret"):available())

        converse(player, rurik, "EXD_Rurik_Chat")
        T.eq("two chats: still no secret", { risen.gamevar.get("EXD_Trust"), risen.dialog.get("EXD_Rurik_Secret"):available() }, { 2, false })

        risen.item.give(player, "It_IronOre", 3)
        T.eq("ore but no bandit: no secret", risen.dialog.get("EXD_Rurik_Secret"):available(), false)

        player:set_guild(risen.npc.guilds.Pir)
        T.eq("bandit with ore and trust: the secret is offered", risen.dialog.get("EXD_Rurik_Secret"):available(), true)
        local list = converse(player, rurik, "EXD_Rurik_Secret")
        local has = false
        for _, n in ipairs(list) do if n == "EXD_Rurik_Secret" then has = true end end
        T.expect("the conversation lists it", has, table.concat(list, ","))
        T.wait_until(function() return risen.item.count(player, "It_Apple") >= 5 end, 30, "the apples arrive")
        T.eq("the secret was used: the ore is taken", risen.item.count(player, "It_IronOre"), 0)
        T.eq("the secret was used: five apples", risen.item.count(player, "It_Apple") >= 5, true)
        T.eq("the secret is gone", risen.dialog.get("EXD_Rurik_Secret"):available(), false)

        -- save and load in the same process: the used option stays used, the conditions still work
        T.wait_until(function() return risen.world.is_save_allowed() end, 30, "saving allowed")
        local saved = false
        risen.on("save.after", function(_, ok) saved = ok end)
        risen.world.quick_save()
        T.wait_until(function() return saved end, 60, "savegame written")
        T.sleep(60)
        risen.world.quick_load()
        T.wait_event("session.start", 120, function(m) return m == "load" end)
        settled()
        T.eq("the secret stays gone after a load", risen.dialog.get("EXD_Rurik_Secret"):available(), false)
        T.eq("the hideout is still offered after the load", offered(), { "EXD_Rurik_Chat", "EXD_Rurik_Hideout" })

        risen.gamevar.remove("EXD_Heard")
        T.eq("a bandit without ore, without the event: the hideout only through the guild", { risen.entity.player():guild(), risen.dialog.get("EXD_Rurik_Hideout"):available() }, { risen.npc.guilds.Pir, true })
        risen.entity.player():set_guild(risen.npc.guilds.Cit)
    end, "example_dialog_when")
end
