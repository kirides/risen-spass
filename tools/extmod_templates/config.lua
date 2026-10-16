-- Options of the generated risen_extended mod (this file is copied from tools/extmod_templates/config.lua by tools/extmod_gen.py; edit the copy in the mod folder).
return {
    -- "all": a new game starts with one of every new item (a showcase for looking at the models; the traders sell the chapter-1 stock without it);
    -- "plans": only the recipe plans; "none": nothing
    starter_kit = "none",
    -- do not hand out the half-finished smithing stages (_Hot / _Shaped / _Dull): they are made at the anvil from the plans
    skip_intermediate = true,
    -- the player knows the recipes that the mod hung on pearls, gold, gold ore and obsidian (a shipped item cannot become a recipe at run time)
    learn_recipes = true,
}
