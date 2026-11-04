-- The DATA of the Random Encounters mod. Edit this file, nothing else, to change what appears, where, how often and from which chapter on.
-- (Scalars like "switch it off" or "twice as often" live in config.toml next to it; main.lua is the engine that reads these tables.)
--
-- The model comes from the Gothic II "Random Encounter" script, carried over to Risen:
--   * every `interval` seconds each ZONE rolls its `chance`; when it hits, the zone tries to spawn ONE enemy from its POOL
--   * a spawn happens at a SPOT (a waypoint / freepoint of the world) that the player cannot see (line of sight + camera view) and that lies
--     between `min_distance` and `max_distance` from him; never when `max_near` of our monsters already stand around the spot
--   * a CATEGORY keeps the cooldown: after a successful spawn the zones of that category sleep (small things come often, big ones rarely);
--     a failed attempt only retries after `retry_after` seconds
--   * the POOL depends on the story CHAPTER: every entry has a weight and a chapter range
--   * a "surprise" once in a while: the enemy is placed at the nearest hidden spot, much closer than usual
--   * live respawn / cleanup: our monsters that were left far behind (and are unseen) are removed again, so the world does not fill up
--
-- Template names: docs/TEMPLATES.md (group "Monster/Kapitel 1..4"). Every entry of a pool also counts as "hostile" when the zone counts
-- the monsters near a spot.

return {
    ---------------------------------------------------------------------------------------------------------------------------------
    -- general rules
    ---------------------------------------------------------------------------------------------------------------------------------
    settings = {
        world = "World",            -- only in this world (risen.world.name()); nil = everywhere
        interval = 2,               -- seconds (of play time) between two sweeps over the zones
        retry_after = 2,            -- seconds a category waits after an attempt that found no spot
        max_alive = 6,              -- never more than this many of OUR monsters alive at once
        only_states = "^PS_Normal", -- Lua pattern for the player's state: only while he simply walks around (not in a fight, a dialogue, a menu animation)
        surprise_chance = 0.2,      -- share of the successful rolls that become a "surprise" at the nearest hidden spot (0 = never)
        surprise_min_distance = 350, -- ... but not closer than this
        sight = 4000,               -- a spot farther away than this counts as unseen
        fov_margin = 0,             -- widen the camera's view cone by this many degrees when asking "can he see it" (more = stricter)
        -- live respawn / cleanup: remove our monsters again when they are this far away AND unseen (0 = keep them; they are saved by the engine)
        despawn_distance = 7000,
        despawn_check = 20,         -- seconds between two cleanups
    },

    ---------------------------------------------------------------------------------------------------------------------------------
    -- never: special monsters that a random encounter must NEVER create (quest targets, bosses, story unique ones). A pool entry that matches is
    -- removed when the mod loads (and logged). `names` are exact template names, `patterns` Lua patterns on the template name. The game marks such
    -- monsters by a suffix on the base species: LizardWarrior_Portal_03, LizardWarrior_EliteBoss_02, Rotworm_Don_01, LizardScout_Spy, ...
    ---------------------------------------------------------------------------------------------------------------------------------
    never = {
        names = { "Gyrger", "Titan", "Inferno", "Tideworm", "SummonedSkeleton", "AttackBrontok", "LizardPriest", "Ogre_Tame", "Wolf_Tame" },
        patterns = { "Boss", "Unikat", "Story", "Portal", "Gamestart", "_Don_%d", "_Spy$", "_Dead", "Hunter", "Keeper", "KeyMaster", "Severin", "Patroscon", "Monastery",
                     "Crypt", "Titan", "Vulcan", "OldMine", "Dungeon", "Feather", "NoviceRanch", "_Bruce_", "_Paul_", "Chief", "Undead" },
    },

    ---------------------------------------------------------------------------------------------------------------------------------
    -- categories: the cooldown shared by their zones (seconds of play time; config.toml `cooldown_scale` multiplies them)
    ---------------------------------------------------------------------------------------------------------------------------------
    categories = {
        small = { cooldown = 240 },     -- 4 minutes   (Gothic: TIMEOUT_SmallMonster)
        human = { cooldown = 600 },     -- 10 minutes  (Gothic: TIMEOUT_Bandit)
        big   = { cooldown = 1800 },    -- 30 minutes  (Gothic: TIMEOUT_BigMonster)
    },

    ---------------------------------------------------------------------------------------------------------------------------------
    -- pools: what can appear. `weight` (default 1), `chapters = { from, to }` (default: every chapter), `opts` = options of risen.npc.spawn
    -- (level, hp_max, guild, items, ...), `count` = how many at once (default 1; a number or { min, max }; the group appears around the spot).
    ---------------------------------------------------------------------------------------------------------------------------------
    pools = {
        wildlife = {
            { template = "Wolf",       weight = 5, chapters = { 1, 4 } },
            { template = "Boar",       weight = 3, chapters = { 1, 4 } },
            { template = "Stingrat",   weight = 3, chapters = { 1, 2 } },
            { template = "Vulture",    weight = 2, chapters = { 1, 2 } },
            { template = "Wolf_Black", weight = 2, chapters = { 2, 4 }, count = { 1, 2 } },
            { template = "GiantBug",   weight = 3, chapters = { 2, 4 } },
            { template = "Rotworm",    weight = 2, chapters = { 2, 4 } },
            { template = "Ghul",       weight = 2, chapters = { 2, 3 } },
            { template = "Skeleton",   weight = 2, chapters = { 2, 3 } },
            { template = "LizardScout", weight = 3, chapters = { 2, 4 } },
        },
        lizards = {
            { template = "LizardScout",   weight = 3, chapters = { 2, 3 } },
            { template = "LizardWarrior", weight = 3, chapters = { 3, 4 } },
            { template = "LizardWarrior_Elite", weight = 2, chapters = { 4, 4 } },
        },
        big = {
            { template = "Thundertail", weight = 2, chapters = { 2, 4 } },
            { template = "AshBeast",    weight = 2, chapters = { 4, 4 } },
            { template = "GiantScorpion", weight = 2, chapters = { 4, 4 } },
            { template = "Ogre",        weight = 1, chapters = { 4, 4 } },
        },
    },

    ---------------------------------------------------------------------------------------------------------------------------------
    -- zones: where and how often. Fields (all but `pool` optional):
    --   category     the cooldown group (above), default "small"
    --   pool         a key of `pools`
    --   chance       probability (0..1) per sweep that the zone tries to spawn; config.toml `chance_scale` multiplies it
    --   min_distance / max_distance   how far from the player the spot must be (ground distance, game units; 100 = 1 m)
    --   max_near / near_radius        at most this many of our monsters within this radius of the spot
    --   chapters     { from, to }: the zone is only active in these chapters
    --   area         { x =, z =, radius = }: only while the PLAYER is inside this circle (use it to bind a zone to a region of the island)
    --   habitat      true: HABITAT MODE (the default of the shipped zones). The monster is picked from the pool first, then the spot: only places where the GAME
    --                itself puts that species (habitats.lua, generated from the world files), hidden from the player, not already crowded. A wolf pack comes back
    --                where wolves live, a ghul where the ghuls are; a species that has no home near the player is skipped (the next of the pool is tried).
    --   regions      habitat mode only: { "Swamp_Outdoor", ... } the game's own outdoor regions (Game_Start, Harbour_Outdoor, Wild_Outdoor, Swamp_Outdoor, Pirate_Coast,
    --                Monastery_Outdoor, Druid_Outdoor, Gyrger_Island, OutMain): the zone only uses habitats there
    --   spots        explicit spots { {x, y, z}, ... }; without it the world's own points are used:
    --   points       filter for risen.world.points: { kind = "guard" } / { template = "Anchor_Patrol" } / { name_prefix = "..." }
    ---------------------------------------------------------------------------------------------------------------------------------
    zones = {
        { name = "roaming wildlife", habitat = true, category = "small", pool = "wildlife", chance = 0.20, min_distance = 700, max_distance = 2800, max_near = 2, near_radius = 1500 },
        { name = "lizard patrol",    habitat = true, category = "human", pool = "lizards",  chance = 0.10, min_distance = 900, max_distance = 3000, max_near = 2, near_radius = 1500, chapters = { 2, 4 } },
        { name = "rare big beast",   habitat = true, category = "big",   pool = "big",      chance = 0.05, min_distance = 1200, max_distance = 3500, max_near = 1, near_radius = 2500, chapters = { 2, 4 } },

        -- Bind a zone to a region: only while the player stands within 3000 units of the point, and only on these hand-made spots.
        -- (Replace the numbers with a place of your own; `risen.entity.player():position()` tells you where you are.)
        -- { name = "my crossing", category = "small", pool = "wildlife", chance = 0.3, area = { x = 1000, z = -2000, radius = 3000 },
        --   spots = { { 900, 0, -1500 }, { 1300, 0, -2600 } }, min_distance = 500, max_distance = 3000 },
    },
}
