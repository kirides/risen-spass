-- Options of the generated risen_extended mod (written by risen-pack2mod / tools/extmod_gen.py from tools/extmod_templates/config.lua; edit the copy in the mod folder, a rerun keeps it).
return {
    -- ---- what a new game starts with ----
    -- "all": a new game starts with one of every new item (a showcase for looking at the models; the traders sell the chapter-1 stock without it);
    -- "plans": only the recipe plans; "none": nothing
    starter_kit = "none",
    -- do not hand out the half-finished smithing stages (_Hot / _Shaped / _Dull): they are made at the anvil from the plans
    skip_intermediate = true,
    -- the player knows the recipes that the mod hung on pearls, gold, gold ore and obsidian (a shipped item cannot become a recipe at run time)
    learn_recipes = true,

    -- ---- feature groups that switch at run time (change the value, start the game again; a savegame keeps what it already got) ----
    balance = true,     -- the changed numbers of shipped items (damage, values, requirements, mana costs, potions ...), the light spell range, monster actor swaps
    new_items = true,   -- the new items, their recipes, trader stock, chest contents, XP / refill edits of the dialogues' items (everything that needs a new item)
    dialogue = true,    -- XP rewards on dialogue infos (pickpocket lessons, quests) and the chapter-wise trader stock
    world = true,       -- items put into chests and equipment of placed NPCs
    teachers = true,    -- changed teaching limits of placed NPCs

    -- ---- feature groups that are files (the engine reads them once at start): switch them with scripts\extmod-features.ps1 ----
    --   combat      the melee system: -Combat changelog (default, p01: one-hand / two-hand / staff styles, 2H reach, no backwards hop) | shipped (p02, the classic table) | off (the game's own)
    --   wind        wind strength 0.3 (WeatherStates.g3ws)
    --   caves       darker caves and dungeons (18 lightmaps)
    --   vegetation  the vegetation textures
    --   skins       armor / head / helmet / monster skins, lizard and female actors
    -- Example:  .\scripts\extmod-features.ps1 -ModDir B:\DevDrive\risen-play\bin\spass\mods\risen_extended -Set combat=off,caves=off
}
