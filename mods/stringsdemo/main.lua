-- The CSV files in strings/ were already loaded when this file runs. Look them up through the overlay:
local lang = risen.loc.language()
risen.on("mods.loaded", function()
    risen.log.info(("stringsdemo [%s]: FO_SPASS_Demo = %s | greeting = %s"):format(tostring(lang),
        tostring(risen.loc.get("FO_SPASS_Demo")), tostring(risen.loc.get("SPASS_Demo_Greeting"))))
end)
-- More files can be loaded explicitly (relative to the mod directory):
-- risen.loc.load_file("extra/quest_texts.csv", "English")
