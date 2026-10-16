-- Fixture of the scenario `modmanager` (docs/DEVELOPMENT.md "Testing and tracing"): answers the driver's question "who is running".
risen.on("mm.who", function(list) list[#list + 1] = "mm_base" end)
