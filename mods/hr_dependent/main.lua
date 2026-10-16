-- part of the scenario `hotreload`: a mod that requires hr_target (reloaded after it)
local tag = "dep1"
risen.on("hr.who", function(list) list[#list + 1] = { who = "dependent", tag = tag } end)
