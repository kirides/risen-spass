risen.on("mm.who", function(list) list[#list + 1] = "mm_data" end)
risen.save.set("runs", (risen.save.get("runs") or 0) + 1)
risen.balance.install({
    new_npcs = { Spass_MM_Guy = { base = "Peasant_1_01", name = "Manager Guy", level = 3, hp_max = 200, guild = "Cit" } },
})
risen.npc.persistent({ name = "MM_Guy", template = "Spass_MM_Guy", offset = { 220, 20, 0 }, peaceful = true, label = "Manager Guy" })
