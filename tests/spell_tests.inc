// ---- spells: the Lua part (catalogue, the `spells` / `new_spells` sections, per-shot overrides); the natives and the item sections are mocked -----------
// Included by test_main.cpp inside its anonymous namespace.

void setupSpellMock(sol::state& lua) {
    lua.open_libraries(sol::lib::base, sol::lib::string, sol::lib::table, sol::lib::math);
    lua.script(R"LUA(
        log = {}
        templates = {}
        local function add(name, sets) templates[name] = { sets = sets, instances = {} } end
        local function cast(mana, consume, icon) return { gCCastInfo_PS = { ManaCost = tostring(mana), ConsumeItem = consume and "True" or "False" },
                                                       gCItem_PS = { GoldValue = "500", IconImage = icon or "ic" } } end
        local function proj(dmg, vel, radius) return { gCDamage_PS = { DamageAmount = tostring(dmg), DamageType = "gEDamageType_Magic" },
                                                        gCProjectile2_PS = { StartVelocity = tostring(vel), SweepTestRadius = tostring(radius) }, gCEffect_PS = { Effect = "eff" } } end
        add("It_Crystal_Fire", cast(8, false)); add("It_Crystal_Frost", cast(10, false)); add("It_Crystal_Magic", cast(5, false))
        for i, v in ipairs({ 15, 20, 25 }) do add("It_Projectile_Fireball_S" .. (i - 1), proj(100, v, 10 + 5 * (i - 1))) end
        for i, v in ipairs({ 15, 20, 25 }) do add("It_Projectile_Frost_S" .. (i - 1), proj(50, v, 10)) end
        add("It_Projectile_Missile_S0", proj(50, 60, 30))
        add("It_Ru_Skeleton", cast(40, false)); add("It_Scr_Skeleton", cast(20, true)); add("It_Orb_Skeleton", cast(0, true))
        add("It_Ru_Light", cast(10, false)); add("It_Scr_Light", cast(5, true)); add("It_Orb_Light", cast(0, true))
        add("It_Ru_Inferno", cast(80, false))   -- a rune without scroll and orb
        risen = { template = {}, item = {}, balance = {}, spell = {}, log = {} }
        function risen.template.exists(n) return templates[n] ~= nil end
        function risen.template.get(name)
            local t = templates[name]
            if not t then return nil, "unknown template '" .. name .. "'" end
            local h = {}
            function h:property_sets() local l = {} for k in pairs(t.sets) do l[#l + 1] = k end table.sort(l) return l end
            function h:list_properties(set) local l = {} for k in pairs(t.sets[set] or {}) do l[#l + 1] = { name = k, type = "x" } end return l end
            function h:get_property(set, prop) return t.sets[set] and t.sets[set][prop] end
            function h:set_property(set, prop, v) log[#log + 1] = name .. "." .. set .. "." .. prop .. "=" .. v t.sets[set][prop] = v return true end
            function h:instances() return t.instances end
            return setmetatable(h, { __tostring = function() return "Template(" .. name .. ")" end })
        end
        function risen.item.requirements(name) return templates[name] and templates[name].req or {} end
        function risen.item.skill_array() return {} end
    )LUA");
    spass::game::installTemplatePatchHelper(lua, lua["risen"]);
    spass::game::installBalanceHelpers(lua, lua["risen"]);
    // the item sections are covered by the balance and template-create tests: here they record what the spell sections hand them
    lua.script(R"LUA(
        risen.balance.sections.items = function(data)
            local jobs, problems = {}, {}
            for name, e in pairs(data) do
                if not templates[name] then problems[#problems + 1] = "items." .. name .. ": unknown template"
                else
                    jobs[#jobs + 1] = { label = name, run = function(o)
                        local t = templates[name]
                        if e.mana_cost ~= nil then t.sets.gCCastInfo_PS.ManaCost = tostring(e.mana_cost) end
                        if e.requires ~= nil then t.req = e.requires end
                        local props = e.properties and e.properties.gCCastInfo_PS
                        if props and props.ConsumeItem ~= nil then t.sets.gCCastInfo_PS.ConsumeItem = props.ConsumeItem and "True" or "False" end
                        log[#log + 1] = ("items %s mana=%s requires=%s consume=%s"):format(name, tostring(e.mana_cost), e.requires and next(e.requires) and "set" or tostring(e.requires and "empty"),
                            tostring(props and props.ConsumeItem))
                        return true, nil, 0 end }
                end
            end
            return jobs, problems
        end
        risen.balance.sections.new_items = function(data)
            local jobs, problems = {}, {}
            local names = {}
            for name in pairs(data) do names[#names + 1] = name end
            table.sort(names)
            for _, name in ipairs(names) do
                local e = data[name]
                if name:find("[^%w_]") then problems[#problems + 1] = "new_items." .. name .. ": bad template name"
                else
                    jobs[#jobs + 1] = { label = name, run = function(o)
                        local src = templates[e.base]
                        local sets = {}
                        for set, p in pairs(src.sets) do sets[set] = {} for k, v in pairs(p) do sets[set][k] = v end end
                        templates[name] = { sets = sets, instances = {}, req = e.requires }
                        if e.mana_cost ~= nil then sets.gCCastInfo_PS.ManaCost = tostring(e.mana_cost) end
                        local props = e.properties and e.properties.gCCastInfo_PS
                        if props and props.ConsumeItem ~= nil then sets.gCCastInfo_PS.ConsumeItem = props.ConsumeItem and "True" or "False" end
                        log[#log + 1] = ("new %s base=%s mana=%s consume=%s value=%s name=%s"):format(name, e.base, tostring(e.mana_cost), tostring(props and props.ConsumeItem),
                            tostring(e.value), type(e.name) == "table" and (e.name.English or "?") or tostring(e.name))
                        return true, nil, 0 end }
                end
            end
            return jobs, problems
        end
        entities = {}
    )LUA");
    spass::game::installSpellHelpers(lua, lua["risen"]);
}

TEST(spell_catalogue_lists_the_shipped_spells) {
    sol::state lua;
    setupSpellMock(lua);
    auto run = [&](const char* code) { return lua.script(std::string("return tostring((function() ") + code + " end)())").get<std::string>(); };
    CHECK(run("local n = risen.spell.names() return #n .. ',' .. n[1] .. ',' .. n[#n]") == "21,Beast,Telekinesis");
    CHECK(run("local i = risen.spell.info('Fireball') return i.kind .. ',' .. i.skill .. ',' .. i.items.crystal .. ',' .. #i.projectiles .. ',' .. i.projectiles[3]") ==
          "projectile,Fireball,It_Crystal_Fire,3,It_Projectile_Fireball_S2");
    CHECK(run("local i = risen.spell.info('Missile') return #i.projectiles .. ',' .. i.items.crystal") == "1,It_Crystal_Magic");
    // rune spells list only the forms that exist
    CHECK(run("local i = risen.spell.info('Skeleton') return i.kind .. ',' .. i.items.rune .. ',' .. i.items.scroll .. ',' .. i.items.orb") == "rune,It_Ru_Skeleton,It_Scr_Skeleton,It_Orb_Skeleton");
    CHECK(run("local i = risen.spell.info('Inferno') return tostring(i.items.rune) .. ',' .. tostring(i.items.scroll)") == "It_Ru_Inferno,nil");
    CHECK(run("local i, why = risen.spell.info('Nope') return tostring(i) .. '|' .. why") .find("nil|unknown spell 'Nope' (valid: Beast, Berserker") == 0);
    // get() reads the numbers from the templates
    CHECK(run("local g = risen.spell.get('Fireball') return g.items.crystal.mana_cost .. ',' .. tostring(g.items.crystal.consume) .. ',' .. g.projectiles[2].damage .. ',' .. g.projectiles[2].speed .. ',' .. "
              "g.projectiles[1].damage_type .. ',' .. g.projectiles[1].effect") == "8,false,100,20,Magic,eff");
    CHECK(run("local g = risen.spell.get('Light') return g.items.scroll.mana_cost .. ',' .. tostring(g.items.scroll.consume) .. ',' .. #g.projectiles") == "5,true,0");
}

TEST(spell_section_validates_everything_before_writing) {
    sol::state lua;
    setupSpellMock(lua);
    auto run = [&](const char* code) { return lua.script(std::string("return tostring((function() ") + code + " end)())").get<std::string>(); };
    auto refused = [&](const char* spec, const char* needle) {
        const std::string out = run((std::string("local rep, why = risen.balance.apply(") + spec + ") return tostring(rep) .. '|' .. tostring(why)").c_str());
        const bool ok = out.rfind("nil|", 0) == 0 && out.find(needle) != std::string::npos;
        if (!ok) {
            std::printf("    apply(%s) -> %s (wanted nil|...%s...)\n", spec, out.c_str(), needle);
        }
        return ok;
    };
    CHECK(refused("{ spells = { Fireballs = { mana_cost = 1 } } }", "spells.Fireballs: unknown spell (valid: Beast"));
    CHECK(refused("{ spells = { Fireball = { mana_cots = 1 } } }", "spells.Fireball: unknown setting 'mana_cots' (valid: consume, damage"));
    CHECK(refused("{ spells = { Fireball = 5 } }", "spells.Fireball: expected a table of settings"));
    CHECK(refused("{ spells = { Light = { damage = 5 } } }", "spells.Light: damage only applies to projectile spells"));
    CHECK(refused("{ spells = { Light = { projectile_properties = {} } } }", "projectile_properties only applies to projectile spells"));
    CHECK(refused("{ spells = { Fireball = { scroll_mana_cost = 5 } } }", "scroll_mana_cost only applies to rune spells"));
    CHECK(refused("{ spells = { Fireball = { orb_mana_cost = 5 } } }", "orb_mana_cost only applies to rune spells"));
    CHECK(refused("{ spells = { Fireball = { mana_cost = -1 } } }", "mana_cost must be a non-negative integer"));
    CHECK(refused("{ spells = { Fireball = { mana_cost = 1.5 } } }", "mana_cost must be a non-negative integer"));
    CHECK(refused("{ spells = { Fireball = { mana_cost = 4, mana_mult = 2 } } }", "mana_cost and mana_mult exclude each other"));
    CHECK(refused("{ spells = { Fireball = { damage = 5, damage_mult = 2 } } }", "damage and damage_mult exclude each other"));
    CHECK(refused("{ spells = { Fireball = { speed = 5, speed_mult = 2 } } }", "speed and speed_mult exclude each other"));
    CHECK(refused("{ spells = { Fireball = { damage = { 1, 2 } } } }", "damage: expected 3 values (one per projectile level), got 2"));
    CHECK(refused("{ spells = { Missile = { damage = { 1, 2 } } } }", "damage: expected 1 values"));
    CHECK(refused("{ spells = { Fireball = { damage = 'a lot' } } }", "damage must be a non-negative integer (or a list"));
    CHECK(refused("{ spells = { Fireball = { speed = 0 } } }", "speed must be a number > 0"));
    CHECK(refused("{ spells = { Fireball = { damage_type = 'Acid' } } }", "damage_type: unknown value 'Acid' (valid: None, Blunt, Edge, Fire, Ice, Magic, Physics, Point)"));
    CHECK(refused("{ spells = { Fireball = { effect = '' } } }", "effect must be an effect name"));
    CHECK(refused("{ spells = { Fireball = { consume = 'yes' } } }", "consume must be true or false"));
    CHECK(refused("{ spells = { Fireball = { projectile_properties = { gCNope_PS = { X = 1 } } } } }", "gCNope_PS"));
    CHECK(refused("{ spells = { Fireball = {} } }", "nothing to do"));
    // one bad entry stops the whole spec: the valid one is not written either
    CHECK(refused("{ spells = { Fireball = { mana_cost = 12 }, Light = { damage = 1 } } }", "spells.Light"));
    CHECK(lua.script("return #log").get<int>() == 0);
    CHECK(run("return risen.spell.get('Fireball').items.crystal.mana_cost") == "8");
    CHECK(run("return table.concat(risen.balance.section_names(), ',')") == "items,new_items,new_spells,spells");
}

TEST(spell_section_writes_items_and_projectiles) {
    sol::state lua;
    setupSpellMock(lua);
    auto run = [&](const char* code) { return lua.script(std::string("return tostring((function() ") + code + " end)())").get<std::string>(); };
    // absolute settings: crystal numbers go through the items section, projectile numbers through template.patch (all three levels)
    CHECK(run("local rep = assert(risen.balance.apply({ spells = { Fireball = { mana_cost = 12, damage = 150, requires = { Fireball = 2 }, consume = true, speed_mult = 1.5, damage_type = 'Fire' } } })) "
              "return rep.values .. ',' .. #rep.applied") == "1,1");
    CHECK(run("local g = risen.spell.get('Fireball') return g.items.crystal.mana_cost .. ',' .. tostring(g.items.crystal.consume) .. ',' .. g.projectiles[1].damage .. ',' .. g.projectiles[3].damage .. ',' .. "
              "g.projectiles[1].speed .. ',' .. g.projectiles[2].speed .. ',' .. g.projectiles[3].speed .. ',' .. g.projectiles[3].damage_type") == "12,true,150,150,22.5,30,37.5,Fire");
    CHECK(run("return table.concat(risen.item.requirements('It_Crystal_Fire'), ',') .. templates.It_Crystal_Fire.req.Fireball") == "2");
    // a list sets one value per level; other levels of other spells stay
    CHECK(run("assert(risen.balance.apply({ spells = { Frost = { damage = { 10, 20, 30 }, effect = 'eff_other', radius_mult = 2 } } })) local g = risen.spell.get('Frost') return g.projectiles[1].damage .. ',' .. "
              "g.projectiles[3].damage .. ',' .. g.projectiles[2].effect .. ',' .. g.projectiles[3].radius .. ',' .. risen.spell.get('Missile').projectiles[1].damage") == "10,30,eff_other,20,50");
    // relative settings are computed from the shipped values: applying twice gives the same numbers
    lua.script("risen.balance.apply({ spells = { Missile = { damage_mult = 2, mana_mult = 3 }, Skeleton = { mana_mult = 0.5 } } })");
    lua.script("risen.balance.apply({ spells = { Missile = { damage_mult = 2, mana_mult = 3 }, Skeleton = { mana_mult = 0.5 } } })");
    CHECK(run("local m, s = risen.spell.get('Missile'), risen.spell.get('Skeleton') return m.projectiles[1].damage .. ',' .. m.items.crystal.mana_cost .. ',' .. s.items.rune.mana_cost .. ',' .. "
              "s.items.scroll.mana_cost .. ',' .. s.items.orb.mana_cost") == "100,15,20,10,0");
    // rune spells: rune and scroll costs separately
    CHECK(run("assert(risen.balance.apply({ spells = { Light = { mana_cost = 7, scroll_mana_cost = 3, orb_mana_cost = 1 } } })) local g = risen.spell.get('Light') return g.items.rune.mana_cost .. ',' .. "
              "g.items.scroll.mana_cost .. ',' .. g.items.orb.mana_cost") == "7,3,1");
    // dry run validates and reports, writes nothing
    lua.script("log = {}");
    CHECK(run("local rep = assert(risen.balance.apply({ spells = { Frost = { mana_cost = 1 } } }, { dry_run = true })) return tostring(rep.dry_run) .. ',' .. #log .. ',' .. risen.spell.get('Frost').items.crystal.mana_cost") == "true,0,10");
    // risen.spell.modify is the one-spell form
    CHECK(run("assert(risen.spell.modify('Frost', { mana_cost = 11 })) return risen.spell.get('Frost').items.crystal.mana_cost") == "11");
}

TEST(spell_new_spells_are_built_from_the_pieces_of_a_shipped_spell) {
    sol::state lua;
    setupSpellMock(lua);
    auto run = [&](const char* code) { return lua.script(std::string("return tostring((function() ") + code + " end)())").get<std::string>(); };
    auto refused = [&](const char* spec, const char* needle) {
        const std::string out = run((std::string("local rep, why = risen.balance.apply(") + spec + ") return tostring(rep) .. '|' .. tostring(why)").c_str());
        const bool ok = out.rfind("nil|", 0) == 0 && out.find(needle) != std::string::npos;
        if (!ok) {
            std::printf("    apply(%s) -> %s (wanted nil|...%s...)\n", spec, out.c_str(), needle);
        }
        return ok;
    };
    CHECK(refused("{ new_spells = { X = { name = 'x' } } }", "'base' (the spell to build on: "));
    CHECK(refused("{ new_spells = { X = { base = 'Nope' } } }", "base: unknown spell 'Nope'"));
    CHECK(refused("{ new_spells = { X = { base = 'Fireball', bogus = 1 } } }", "new_spells.X: unknown setting 'bogus' (valid: base, consume"));
    CHECK(refused("{ new_spells = { X = { base = 'Fireball', forms = { 'orb' } } } }", "form 'orb' does not exist for projectile spells (valid: crystal, scroll)"));
    CHECK(refused("{ new_spells = { X = { base = 'Skeleton', forms = { 'crystal' } } } }", "form 'crystal' does not exist for rune spells (valid: rune, scroll, orb)"));
    CHECK(refused("{ new_spells = { X = { base = 'Light', damage = 3 } } }", "damage only applies to projectile spells"));
    CHECK(refused("{ new_spells = { X = { base = 'Fireball', damage_type = 'Acid' } } }", "damage_type: unknown value 'Acid'"));
    CHECK(refused("{ new_spells = { X = { base = 'Fireball', scroll = { bogus = 1 } } } }", "scroll: unknown setting 'bogus' (valid: consume, description"));
    CHECK(refused("{ new_spells = { X = { base = 'Inferno', forms = { 'scroll' } } } }", "the base spell has no scroll item"));
    CHECK(refused("{ new_spells = { ['Bad Name'] = { base = 'Fireball' } } }", "bad template name"));
    CHECK(refused("{ new_spells = { X = { base = 'Fireball', mana_cost = 4, mana_mult = 2 } } }", "mana_cost and mana_mult exclude each other"));
    CHECK(lua.script("return #log").get<int>() == 0);

    // a projectile spell: crystal and scroll; the scroll is the crystal used up (consume, no requirements, half the cost, labelled)
    CHECK(run("local rep = assert(risen.balance.apply({ new_spells = { Spass_Fire2 = { base = 'Fireball', name = 'Strong Fireball', mana_cost = 17, requires = { Fireball = 2 }, damage_mult = 2, "
              "forms = { 'crystal', 'scroll' } } } })) return rep.values .. ',' .. rep.applied[1]") == "1,new spell Spass_Fire2 from Fireball (Spass_Fire2, Spass_Fire2_Scroll)");
    CHECK(run("return table.concat(log, ';')") == "new Spass_Fire2 base=It_Crystal_Fire mana=17 consume=nil value=nil name=Strong Fireball;"
                                                   "new Spass_Fire2_Scroll base=It_Crystal_Fire mana=9 consume=true value=100 name=Strong Fireball (Scroll)");
    CHECK(run("return tostring(risen.spell.has_overrides('Spass_Fire2')) .. ',' .. tostring(risen.spell.has_overrides('Spass_Fire2_Scroll')) .. ',' .. tostring(risen.spell.has_overrides('It_Crystal_Fire'))") == "true,true,false");
    CHECK(run("local o = risen.spell.overrides() return tostring(o.Spass_Fire2.scroll) .. ',' .. tostring(o.Spass_Fire2_Scroll.scroll) .. ',' .. o.Spass_Fire2.settings.damage_mult") == "false,true,2");
    // a rune spell: rune, scroll and orb; per-form tables override, mana_mult scales every form from the shipped numbers
    lua.script("log = {}");
    CHECK(run("assert(risen.balance.apply({ new_spells = { Spass_Skel = { base = 'Skeleton', name = { English = 'Cheap Skeleton', German = 'Billiges Skelett' }, mana_cost = 15, requires = { Circle = 2 }, "
              "forms = { 'rune', 'scroll', 'orb' }, scroll = { mana_cost = 7, name = 'Skeleton scroll' } } } })) return table.concat(log, ';')") ==
          "new Spass_Skel base=It_Ru_Skeleton mana=15 consume=nil value=nil name=Cheap Skeleton;"
          "new Spass_Skel_Orb base=It_Orb_Skeleton mana=nil consume=nil value=nil name=Cheap Skeleton (Orb);"
          "new Spass_Skel_Scroll base=It_Scr_Skeleton mana=7 consume=nil value=nil name=Skeleton scroll");
    lua.script("log = {}");
    CHECK(run("assert(risen.balance.apply({ new_spells = { Spass_Light = { base = 'Light', mana_mult = 0.5, forms = { 'rune', 'scroll' } } } })) return table.concat(log, ';')") ==
          "new Spass_Light base=It_Ru_Light mana=5 consume=nil value=nil name=nil;new Spass_Light_Scroll base=It_Scr_Light mana=3 consume=nil value=nil name=nil");
    // a form table alone enables the form; the generic keys go to the first wanted form when the main one is not wanted
    lua.script("log = {}");
    CHECK(run("assert(risen.balance.apply({ new_spells = { Spass_Only_Scroll = { base = 'Skeleton', forms = { 'scroll' }, mana_cost = 3 } } })) return table.concat(log, ';')") ==
          "new Spass_Only_Scroll_Scroll base=It_Scr_Skeleton mana=3 consume=nil value=nil name=nil");
    // spell.create is the one-spell form and reports the same
    CHECK(run("local rep, why = risen.spell.create('Spass_Fire3', { base = 'Frost', damage = 70 }) return tostring(rep and rep.ok) .. ',' .. tostring(risen.spell.overrides().Spass_Fire3.settings.damage)") == "true,70");
}

TEST(spell_shot_overrides_are_written_into_the_projectile_of_a_shot) {
    sol::state lua;
    setupSpellMock(lua);
    auto run = [&](const char* code) { return lua.script(std::string("return tostring((function() ") + code + " end)())").get<std::string>(); };
    lua.script(R"LUA(
        local equipped = {}
        function risen.spell.equipped(who, slot) return equipped[who.name] end
        function risen.spell.shooter(e) return e.shooter end
        function set_equipped(who, item) equipped[who] = item end
        function projectile(template, shooter, damage, speed, radius)
            local e = { shooter = shooter, props = { gCDamage_PS = { DamageAmount = tostring(damage) }, gCProjectile2_PS = { StartVelocity = tostring(speed), SweepTestRadius = tostring(radius) }, gCEffect_PS = { Effect = "e" } } }
            function e:get_property(set, prop) return self.props[set] and self.props[set][prop] end
            function e:set_property(set, prop, v) self.props[set][prop] = v return true end
            return e
        end
        risen.spell.SLOT_MAGIC = 11
        player = { name = "PC_Hero" }
    )LUA");
    // relative numbers are relative to what the projectile carries (the template values incl. a `spells` edit), absolute ones replace them
    lua.script("assert(risen.balance.apply({ spells = { Fireball = { damage = 150 } }, new_spells = { Spass_Fire2 = { base = 'Fireball', damage_mult = 2, speed = 40, radius_mult = 3, effect = 'eff_new', forms = { 'crystal', 'scroll' } } } }))");
    lua.script("set_equipped('PC_Hero', 'Spass_Fire2')");
    CHECK(run("local e = projectile('It_Projectile_Fireball_S0', player, 150, 15, 10) local item, shooter, d = risen.spell.on_projectile(e, 'It_Projectile_Fireball_S0') "
              "return tostring(item) .. ',' .. tostring(shooter == player) .. ',' .. tostring(d.scroll) .. ',' .. e.props.gCDamage_PS.DamageAmount .. ',' .. e.props.gCProjectile2_PS.StartVelocity .. ',' .. "
              "e.props.gCProjectile2_PS.SweepTestRadius .. ',' .. e.props.gCEffect_PS.Effect") == "Spass_Fire2,true,false,300,40,30,eff_new");
    // the level of the projectile template is respected (S2 is the third), the scroll form is registered too
    lua.script("set_equipped('PC_Hero', 'Spass_Fire2_Scroll')");
    CHECK(run("local e = projectile('It_Projectile_Fireball_S2', player, 150, 25, 20) local item, _, d = risen.spell.on_projectile(e, 'It_Projectile_Fireball_S2') "
              "return item .. ',' .. tostring(d.scroll) .. ',' .. e.props.gCDamage_PS.DamageAmount .. ',' .. e.props.gCProjectile2_PS.SweepTestRadius") == "Spass_Fire2_Scroll,true,300,60");
    // not affected: another crystal, another projectile family, a projectile nobody shot, a template that is no projectile
    lua.script("set_equipped('PC_Hero', 'It_Crystal_Fire')");
    CHECK(run("local e = projectile('It_Projectile_Fireball_S0', player, 150, 15, 10) local item = risen.spell.on_projectile(e, 'It_Projectile_Fireball_S0') return tostring(item) .. ',' .. e.props.gCDamage_PS.DamageAmount") == "nil,150");
    lua.script("set_equipped('PC_Hero', 'Spass_Fire2')");
    CHECK(run("local e = projectile('It_Projectile_Frost_S0', player, 50, 15, 10) return tostring(risen.spell.on_projectile(e, 'It_Projectile_Frost_S0')) .. ',' .. e.props.gCDamage_PS.DamageAmount") == "nil,50");
    CHECK(run("local e = projectile('It_Projectile_Fireball_S0', nil, 150, 15, 10) return tostring(risen.spell.on_projectile(e, 'It_Projectile_Fireball_S0')) .. ',' .. e.props.gCDamage_PS.DamageAmount") == "nil,150");
    CHECK(run("local e = projectile('It_Arrow', player, 20, 50, 30) return tostring(risen.spell.on_projectile(e, 'It_Arrow'))") == "nil");
    // a second shot is computed from the fresh projectile again: nothing compounds
    CHECK(run("local e1, e2 = projectile('It_Projectile_Fireball_S0', player, 150, 15, 10), projectile('It_Projectile_Fireball_S0', player, 150, 15, 10) risen.spell.on_projectile(e1, 'It_Projectile_Fireball_S0') "
              "risen.spell.on_projectile(e2, 'It_Projectile_Fireball_S0') return e1.props.gCDamage_PS.DamageAmount .. ',' .. e2.props.gCDamage_PS.DamageAmount") == "300,300");
}
