# Quick Loot

Press the **right mouse button** on an item, a dead or knocked-out human, a monster or animal carcass (the trophies are taken too), a washed-up body, a chest, a cupboard or a sarcophagus: you take everything at once, without walking over, kneeling, the loot window or any
animation. Hold it to take everything in reach.

Standalone mod for the Risen mod layer (`risen-spass`): copy this folder to `<Risen>\bin\spass\mods\quickloot`, nothing else is needed. Remove the folder to uninstall.

Only when no weapon is drawn and you are walking around; never for traps, lockpicking (a locked chest is only opened with its own key from your inventory), quest locks, doors and everything it does not know: then nothing happens and the normal use
key still does the game's full interaction. Taking an item or opening a chest in a watched place (a house) is theft: quick loot asks the game's NPCs exactly like the animated take does (people who see it
comment, watch or threaten; `witness_mode = "assess"`), or declines while somebody could see you (`"veto"`), or lets everybody ignore it (`"ignore"`). The key, a modifier key, the three kinds, the key
lock, the HUD lines, the pick-up sound of ground items (the material sound of the take animation), the hold repeat, the witness mode and radius and the reach are options: change them in the game
(ESC > Mods > Quick Loot > Options, they apply at once and are kept) or in `config.toml`.

Everything (what is declined and why, how it was verified, limits): [docs/QUICKLOOT.md](../../docs/QUICKLOOT.md).

Files: `mod.toml`, `main.lua` (key handling, guards, the three ways to take), `policy.lua` (the pure decisions: which interaction is plain, locks, which are theft, the witness mode), `config.toml` (defaults).
