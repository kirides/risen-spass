-- The item Spass_ProbeModel carries the mesh assets/Spass_Probe._xmsh, which risen-modkit made from probe.obj:
--   risen-modkit --convert-model mods/meshprobe/probe.obj --out mods/meshprobe/assets/Spass_Probe._xmsh --material ItWpn_SwordMisc_01_Diffuse_01._xmat
-- Look at it with the screenshot driver (docs/editor/USER_GUIDE.md, AST-4):
--   .\scripts\test-run.ps1 -Mods meshprobe,extmod_shots -WaitForResult -Seconds 200 -Scenario extmod_shots -Param scene=ground,items=Spass_ProbeModel+It_1H_Club,tag=probe
risen.balance.install({
    new_items = {
        Spass_ProbeModel = { base = "It_1H_Club", name = "Probe model", mesh = "Spass_Probe.xmsh" },
    },
})
