// ---- NPC balancing (the Lua part of risen.npc.stats/set_stats and the npcs / npc_groups / item_groups sections; the natives are mocked) -----------
// Included by test_main.cpp inside its anonymous namespace.

void setupNpcMock(sol::state& lua) {
    lua.open_libraries(sol::lib::base, sol::lib::string, sol::lib::table, sol::lib::math);
    lua.script(R"LUA(
        log = {}
        -- gESkill limits like the engine's: attributes 0..200, level >= 1, the rest unlimited
        local function lim(id) if id >= 7 and id <= 9 then return 0, 200 elseif id == 2 then return 1, -1 elseif id >= 16 and id <= 20 then return 0, 10 end return 0, -1 end
        local function skills(hp, level, str, prot)
            local sk = {}
            for i = 0, 33 do sk[i] = 0 end
            sk[0], sk[5], sk[2], sk[7] = hp, hp, level, str
            for i = 10, 15 do sk[i] = prot end
            return sk
        end
        local function npc_props(guild, species)
            return { gCNPC_PS = { Guild = "gEGuild_" .. guild, Species = "gESpecies_" .. species, BraveryOverride = "gEBraveryOverride_None", AttitudeLock = "gEAttitude_None",
                                   DamageCalculationType = "gEDamageCalculationType_Normal", Group = "" },
                     gCDamage_PS = { DamageAmount = "0", DamageType = "gEDamageType_None" }, gCCombatSystem_PS = { CombatSpecies = "Wolf" } }
        end
        templates = {
            Wolf = { sk = skills(180, 6, 30, 24), props = npc_props("None", "Wolf"), inv = {}, instances = {} },
            Boar = { sk = skills(180, 6, 30, 24), props = npc_props("None", "Boar"), inv = {}, instances = {} },
            Chicken = { sk = skills(30, 1, 0, 0), props = npc_props("None", "Chicken"), inv = {}, instances = {} },
            Titan = { sk = skills(1200, 40, 200, 160), props = npc_props("None", "Ogre"), inv = {}, instances = {} },
            PC_Hero = { sk = skills(100, 1, 10, 0), props = npc_props("Cit", "Human"), inv = {}, instances = {} },
            It_Sword = { props = { gCItem_PS = { GoldValue = "50" }, gCDamage_PS = { DamageAmount = "10" } }, instances = {} },
            It_Apple = { props = { gCItem_PS = { GoldValue = "1" } }, instances = {} },
            It_Club = { props = { gCItem_PS = { GoldValue = "20" }, gCDamage_PS = { DamageAmount = "0" } }, instances = {} },
        }
        templates.Titan.props.gCDamage_PS.DamageAmount = "100"
        local groups = { NPC = { "Wolf", "PC_Hero", "Chicken" }, Monster = { "Boar", "Titan", "Skull_Head" }, ["Monster/Big"] = { "Titan" }, Items = { "It_Sword", "It_Club" },
                         ["Items/Weapons"] = { "It_Sword" } }
        risen = { npc = {}, template = {}, item = {}, log = {} }
        function risen.template.list(group) return groups[group] or {} end
        function risen.template.exists(name) return templates[name] ~= nil end
        function risen.template.census() local c = {} for n, t in pairs(templates) do if #t.instances > 0 then c[n] = #t.instances end end return c, 0 end
        local function fake_entity(name, hp, level)
            local t = templates[name]
            local e = { template_name = name, sk = {}, props = {}, stacks = 1 }
            for i = 0, 33 do e.sk[i] = t.sk[i] end
            e.sk[0], e.sk[2] = hp or e.sk[0], level or e.sk[2]
            for set, p in pairs(t.props) do e.props[set] = {} for k, v in pairs(p) do e.props[set][k] = v end end
            setmetatable(e, { __tostring = function() return "Entity(" .. name .. "@0x1)" end })
            function e:get_property(set, prop) return self.props[set] and self.props[set][prop] end
            function e:set_property(set, prop, value) log[#log + 1] = "entity." .. set .. "." .. prop .. "=" .. value self.props[set][prop] = value return true end
            function e:stack_count() return self.stacks end
            function e:template() return setmetatable({}, { __tostring = function() return "Template(" .. name .. ")" end }) end
            return e
        end
        function spawn(name, hp, level) local e = fake_entity(name, hp, level) local t = templates[name] t.instances[#t.instances + 1] = e return e end
        function risen.template.get(name)
            local t = templates[name]
            if not t then return nil, "unknown template '" .. name .. "'" end
            local h = {}
            function h:property_sets() local l = {} for k in pairs(t.props) do l[#l + 1] = k end if t.sk then l[#l + 1] = "gCSkills_PS" end return l end
            function h:list_properties(set) local l = {} for k in pairs(t.props[set] or {}) do l[#l + 1] = { name = k, type = "x" } end return l end
            function h:get_property(set, prop) return t.props[set] and t.props[set][prop] end
            function h:set_property(set, prop, v) log[#log + 1] = name .. "." .. set .. "." .. prop .. "=" .. v t.props[set][prop] = v return true end
            function h:instances() return t.instances end
            return setmetatable(h, { __tostring = function() return "Template(" .. name .. ")" end })
        end
        local function target(x)
            if type(x) == "string" then
                local t = templates[x]
                if not t then return nil, "unknown template '" .. x .. "'" end
                if not t.sk then return nil, "template '" .. x .. "' has no property set 'gCSkills_PS'" end
                return t.sk, true
            end
            return x.sk, false
        end
        function risen.npc.skill_raw(x, id)
            local sk, why = target(x)
            if not sk then return nil, why end
            local mn, mx = lim(id)
            return sk[id], 0, sk[id], mn, mx
        end
        function risen.npc.set_skill_base(x, id, v)
            local sk, is_template = target(x)
            if not sk then return nil, is_template end
            local mn, mx = lim(id)
            if mx > 0 and v > mx then v = mx end
            if v < mn then v = mn end
            sk[id] = v
            if not is_template and id == 5 and sk[0] > v then sk[0] = v end   -- the engine clamps current hit points to the maximum
            log[#log + 1] = (is_template and "T:" or "E:") .. id .. "=" .. v
            return v, v
        end
        function risen.npc.template_inventory(name) local l = {} for item, n in pairs(templates[name].inv) do l[#l + 1] = { item = item, amount = n } end return l end
        function risen.npc.template_give(name, item, n) local t = templates[name] t.inv[item] = (t.inv[item] or 0) + n log[#log + 1] = "give " .. item .. " " .. n return true end
        function risen.npc.template_take(name, item, n) local t = templates[name] t.inv[item] = (t.inv[item] or 0) - n log[#log + 1] = "take " .. item .. " " .. n return n end
        function risen.npc.combat_species(name) return { { name = "RightFistItem", value = "It_Fist_Monster" }, { name = "LeftFistItem", value = "(no template)" } } end
    )LUA");
    spass::game::installTemplatePatchHelper(lua, lua["risen"]);
    // risen.balance and a stubbed `items` section (the item side is covered by the balance tests)
    lua.script(R"LUA(
        risen.balance = {}
        risen.item = { skill_array = function() return {} end }
    )LUA");
    spass::game::installBalanceHelpers(lua, lua["risen"]);
    lua.script(R"LUA(
        item_entries = nil
        risen.balance.sections.items = function(entries)
            item_entries = entries
            local jobs = {}
            for name, e in pairs(entries) do jobs[#jobs + 1] = { label = name, run = function() log[#log + 1] = "item " .. name .. " value=" .. tostring(e.value) .. " damage=" .. tostring(e.damage) return true, nil, 1 end } end
            return jobs, {}
        end
    )LUA");
    spass::game::installNpcBalanceHelpers(lua, lua["risen"]);
}

TEST(npc_stats_and_set_stats_on_templates) {
    sol::state lua;
    setupNpcMock(lua);
    auto run = [&](const char* code) { return lua.script(std::string("return tostring((function() ") + code + " end)())").get<std::string>(); };
    // stats: totals, the reward formula (10 * level, at least 50; Chicken is "small": exactly 10 * level) and names
    CHECK(run("local s = risen.npc.stats('Wolf') return s.hp .. ',' .. s.hp_max .. ',' .. s.level .. ',' .. s.skills.Strength .. ',' .. s.xp_reward .. ',' .. s.guild .. ',' .. s.species") ==
          "180,180,6,30,60,None,Wolf");
    CHECK(run("local s = risen.npc.stats('Chicken') return s.level .. ',' .. s.xp_reward") == "1,10");
    CHECK(run("local s = risen.npc.stats(risen.template.get('Titan')) return s.xp_reward .. ',' .. s.damage.amount .. ',' .. s.combat.right_fist") == "400,100,It_Fist_Monster");
    CHECK(run("local s, why = risen.npc.stats('It_Sword') return tostring(s) .. '|' .. why") == "nil|template 'It_Sword' has no property set 'gCSkills_PS'");
    // absolute settings: hit points follow the maximum; xp sets the level; enum names are completed
    CHECK(run("local r = assert(risen.npc.set_stats('Wolf', { hp_max = 500, xp = 200, skills = { Strength = 90, ProtEdge = 60 }, guild = 'Pir', bravery = 'Brave', "
              "loot = { It_Apple = 3 } })) local s = risen.npc.stats('Wolf') return s.hp .. '/' .. s.hp_max .. ',' .. s.level .. ',' .. s.skills.Strength .. ',' .. s.skills.ProtEdge .. ',' .. "
              "s.guild .. ',' .. s.bravery .. ',' .. r.written") == "500/500,20,90,60,Pir,Brave,8");
    CHECK(run("return templates.Wolf.inv.It_Apple") == "3");
    // loot is a target amount, not an increment: applying again does nothing, lowering takes
    lua.script("log = {}");
    CHECK(run("risen.npc.set_stats('Wolf', { loot = { It_Apple = 3 } }) return #log") == "0");
    CHECK(run("risen.npc.set_stats('Wolf', { loot = { It_Apple = 1 } }) return log[1]") == "take It_Apple 2");
    // relative settings are computed from the shipped values: applying twice does not compound
    CHECK(run("assert(risen.npc.set_stats('Boar', { hp_mult = 2, level_add = 2, damage_mult = 1.5 })) assert(risen.npc.set_stats('Boar', { hp_mult = 2, level_add = 2, damage_mult = 1.5 })) "
              "local s = risen.npc.stats('Boar') return s.hp .. '/' .. s.hp_max .. ',' .. s.level .. ',' .. s.skills.Strength") == "360/360,8,45");
    // clamping of relative values and the notes; Strength is limited to 200
    CHECK(run("local r = assert(risen.npc.set_stats('Titan', { skills_mult = { Strength = 2 } })) return risen.npc.stats('Titan').skills.Strength .. ',' .. r.notes[1]") == "200,Strength clamped to 200");
    // the NPC's own damage scales with damage_mult (only when it has one)
    CHECK(run("assert(risen.npc.set_stats('Titan', { damage_mult = 0.5 })) return risen.template.get('Titan'):get_property('gCDamage_PS', 'DamageAmount')") == "50");
    // invalid specs: nothing written, every problem named
    lua.script("log = {}");
    CHECK(run("local r, why = risen.npc.set_stats('Wolf', { hp_max = 10, hp_mult = 2 }) return tostring(r) .. '|' .. why").find("hp_max and hp_mult exclude each other") != std::string::npos);
    CHECK(run("local r, why = risen.npc.set_stats('Wolf', { health = 10 }) return why").find("unknown setting 'health' (valid: ai_group, attitude, bravery") != std::string::npos);
    CHECK(run("local r, why = risen.npc.set_stats('Wolf', { skills = { Strenght = 1 } }) return why").find("unknown skill 'Strenght' (valid: HitPoints") != std::string::npos);
    CHECK(run("local r, why = risen.npc.set_stats('Wolf', { skills = { Strength = 999 } }) return why").find("Strength: 999 is outside the allowed range 0..200") != std::string::npos);
    CHECK(run("local r, why = risen.npc.set_stats('Wolf', { guild = 'Pirates' }) return why").find("guild: unknown value 'Pirates' (valid: None, Don") != std::string::npos);
    CHECK(run("local r, why = risen.npc.set_stats('Wolf', { hp = 500, hp_max = 100 }) return why").find("hp 500 is above hp_max 100") != std::string::npos);
    CHECK(run("local r, why = risen.npc.set_stats('Wolf', { damage_mult = 2, skills = { Strength = 5 } }) return why").find("damage_mult and an explicit Strength") != std::string::npos);
    CHECK(run("local r, why = risen.npc.set_stats('Wolf', { properties = { gCNPC_PS = { Nope = 1 } } }) return why").find("has no property 'Nope'") != std::string::npos);
    CHECK(run("local r, why = risen.npc.set_stats('It_Sword', { hp_max = 1 }) return why").find("'It_Sword' is not an NPC template") != std::string::npos);
    CHECK(run("local r, why = risen.npc.set_stats('Wolf', { loot = { It_Nope = 1 } }) return why").find("unknown item template 'It_Nope'") != std::string::npos);
    CHECK(run("return #log") == "0");
    // dry_run validates only
    CHECK(run("local r = assert(risen.npc.set_stats('Wolf', { hp_max = 1 }, { dry_run = true })) return tostring(r.dry_run) .. #log") == "true0");
}

TEST(npc_set_stats_propagates_to_instances_without_healing_the_damaged) {
    sol::state lua;
    setupNpcMock(lua);
    auto run = [&](const char* code) { return lua.script(std::string("return tostring((function() ") + code + " end)())").get<std::string>(); };
    lua.script("full = spawn('Wolf') damaged = spawn('Wolf', 7) dead = spawn('Wolf', 0)");
    CHECK(run("local r = assert(risen.npc.set_stats('Wolf', { hp_max = 500, level = 12, skills = { Strength = 80 } }, { instances = true })) "
              "return r.instances .. ';' .. full.sk[0] .. '/' .. full.sk[5] .. ';' .. damaged.sk[0] .. '/' .. damaged.sk[5] .. ';' .. dead.sk[0] .. '/' .. dead.sk[5] .. ';' .. full.sk[2] .. ',' .. damaged.sk[7]") ==
          "3;500/500;7/500;0/500;12,80");
    // an explicit current hit point value is written to the full ones only; lowering the maximum clamps (the engine does)
    CHECK(run("assert(risen.npc.set_stats('Wolf', { hp_max = 100 }, { instances = true })) return full.sk[0] .. '/' .. full.sk[5] .. ';' .. damaged.sk[0] .. '/' .. damaged.sk[5]") == "100/100;7/100");
    // a single entity: absolute, relative to the entity's own values; explicit hp is written even for a damaged one
    CHECK(run("local r = assert(risen.npc.set_stats(damaged, { hp_max = 200, hp = 150, skills_add = { Strength = 5 } })) return damaged.sk[0] .. '/' .. damaged.sk[5] .. ',' .. damaged.sk[7] .. ',' .. r.written") == "150/200,85,1");
    CHECK(run("local s = risen.npc.stats(damaged) return s.template .. ',' .. s.stacks") == "Wolf,1");
}

TEST(npc_balance_sections_groups_and_scale_npcs) {
    sol::state lua;
    setupNpcMock(lua);
    auto run = [&](const char* code) { return lua.script(std::string("return tostring((function() ") + code + " end)())").get<std::string>(); };
    lua.script("w1 = spawn('Wolf') w2 = spawn('Wolf', 9) t1 = spawn('Titan')");
    // npcs section through risen.balance.apply (instances default to true) and a bad entry rejects everything
    CHECK(run("local rep = assert(risen.balance.apply({ npcs = { Wolf = { hp_max = 300, xp = 100 } } })) return tostring(rep.ok) .. ',' .. #rep.applied .. ',' .. rep.instances .. ';' .. w1.sk[0] .. ',' .. w1.sk[2] .. ',' .. w2.sk[0]") ==
          "true,1,2;300,10,9");
    CHECK(run("local rep, why = risen.balance.apply({ npcs = { Wolf = { hp_max = 1 }, Nope = { hp_max = 1 } } }) return tostring(rep) .. '|' .. why") ==
          "nil|npcs.Nope: unknown template 'Nope' (NPC and monster names: docs/TEMPLATES.md, risen.template.list('NPC'))");
    CHECK(run("return risen.npc.stats('Wolf').hp_max") == "300");
    CHECK(run("local rep, why = risen.balance.apply({ npcs = { It_Sword = { hp_max = 1 } } }) return why") == "npcs.It_Sword: not an NPC template (no gCSkills_PS) (NPC and monster names: docs/TEMPLATES.md, risen.template.list('NPC'))");
    // groups: a Lua pattern; the default universe is NPC + Monster without the player's template and without templates that are no NPCs (heads)
    CHECK(run("local rep = assert(risen.balance.apply({ npc_groups = { { match = '^Wolf', hp_mult = 2 } } }, { instances = false })) return rep.applied[1] .. ';' .. risen.npc.stats('Wolf').hp_max") ==
          "npc_groups[1] ^Wolf (1 templates: hp_mult);360");
    CHECK(run("local rep = assert(risen.balance.apply({ npc_groups = { { level_add = 1 } } }, { instances = false })) return rep.applied[1] .. ';' .. risen.npc.stats('PC_Hero').level .. ',' .. risen.npc.stats('Boar').level") ==
          "npc_groups[1] all NPCs (4 templates: level_add);1,7");
    // a template group path and exact names (even outside the group)
    CHECK(run("local rep = assert(risen.balance.apply({ npc_groups = { { group = 'Monster/Big', names = { 'PC_Hero' }, level_add = 10 } } }, { instances = false })) "
              "return rep.applied[1] .. ';' .. risen.npc.stats('PC_Hero').level .. ',' .. risen.npc.stats('Titan').level") == "npc_groups[1] Monster/Big (2 templates: level_add);11,50");
    // errors: nothing matches, unknown keys, bad patterns, unknown group
    CHECK(run("local _, why = risen.balance.apply({ npc_groups = { { match = '^Zzz', hp_mult = 2 } } }) return why") == "npc_groups[1]: matches no NPC template (match '^Zzz')");
    CHECK(run("local _, why = risen.balance.apply({ npc_groups = { { match = 'Wolf', hp_mulT = 2 } } }) return why").find("unknown setting 'hp_mulT'") != std::string::npos);
    CHECK(run("local _, why = risen.balance.apply({ npc_groups = { { match = '(', hp_mult = 2 } } }) return why").find("bad pattern '('") != std::string::npos);
    CHECK(run("local _, why = risen.balance.apply({ npc_groups = { { group = 'Nope/Nope', hp_mult = 2 } } }) return why").find("no templates in group 'Nope/Nope'") != std::string::npos);
    CHECK(run("local _, why = risen.balance.apply({ npc_groups = { { match = 'Wolf', skills_mult = { Strength = -1 } } } }) return why").find("unsuitable value -1") != std::string::npos);
    // scale_npcs: filter = a template group, else a pattern; factors are relative to the shipped values (repeatable)
    CHECK(run("local rep = assert(risen.balance.scale_npcs{ filter = 'Monster', hp = 1.5, damage = 2, level = 1.5, xp = nil, skills = { ProtEdge = 0.5 }, instances = false }) "
              "local s = risen.npc.stats('Boar') return #rep.applied .. ';' .. s.hp_max .. ',' .. s.skills.Strength .. ',' .. s.level .. ',' .. s.skills.ProtEdge") == "1;270,60,9,12");
    CHECK(run("assert(risen.balance.scale_npcs{ filter = 'Monster', hp = 1.5, instances = false }) return risen.npc.stats('Boar').hp_max") == "270");
    CHECK(run("assert(risen.balance.scale_npcs{ filter = '^Wolf', hp = 3, instances = false }) return risen.npc.stats('Wolf').hp_max") == "540");
    // item_groups: relative to the shipped gold value / damage, through the items section
    CHECK(run("local rep = assert(risen.balance.apply({ item_groups = { { group = 'Items/Weapons', value_mult = 0.5, damage_mult = 2 } } })) return rep.applied[1] .. ';' .. item_entries.It_Sword.value .. ',' .. item_entries.It_Sword.damage .. ',' .. tostring(item_entries.It_Club)") ==
          "item_groups[1] Items/Weapons (1 items);25,20,nil");
    CHECK(run("local rep = assert(risen.balance.apply({ item_groups = { { match = 'Club', value_mult = 2, damage_mult = 3 } } })) return item_entries.It_Club.value .. ',' .. tostring(item_entries.It_Club.damage)") == "40,nil");
    CHECK(run("assert(risen.balance.apply({ item_groups = { { group = 'Items/Weapons', value_mult = 0.5 } } })) assert(risen.balance.apply({ item_groups = { { group = 'Items/Weapons', value_mult = 0.5 } } })) return item_entries.It_Sword.value") == "25");
    CHECK(run("local _, why = risen.balance.apply({ item_groups = { { group = 'Items/Weapons' } } }) return why") == "item_groups[1]: nothing to do (value_mult, damage_mult)");
    CHECK(run("return table.concat(risen.balance.section_names(), ',')") == "item_groups,items,npc_groups,npcs");
}
