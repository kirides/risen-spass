// ---- recipes: the Lua part (catalogue, knowledge, can_craft/craft, the `recipes` / `new_recipes` sections); the natives are mocked ------------------------
// Included by test_main.cpp inside its anonymous namespace.

void setupRecipeMock(sol::state& lua) {
    lua.open_libraries(sol::lib::base, sol::lib::string, sol::lib::table, sol::lib::math);
    lua.script(R"LUA(
        log = {}
        templates = {}
        inventory = {}          -- item -> count of the (single) character
        vars = {}
        created = {}
        local function add(name, sets, extra)
            templates[name] = { sets = sets, instances = {}, ing = {}, req = {} }
            for k, v in pairs(extra or {}) do templates[name][k] = v end
        end
        local function item(value) return { gCItem_PS = { GoldValue = tostring(value) } } end
        local function rec(craft, result, ing, req)
            return { gCItem_PS = { GoldValue = "100" }, gCRecipe_PS = { Craft = "gERecipeCategory_" .. craft, ResultItem = result, ResultAmount = "1" } }, { ing = ing, req = req }
        end
        add("It_1H_Sword", item(50)); add("It_IronOre", item(5)); add("It_Result", item(10)); add("It_Po_Health_01", item(75))
        local s, e = rec("Smith_Anvil", "It_Result", { { item = "It_IronOre", count = 1 } }, { { skill = "gESkill_Misc_Smith", amount = 1 } })
        add("It_Shipped_Hot", s, e)
        s, e = rec("Alchemy", "It_Po_Health_01", { { item = "It_IronOre", count = 3 } }, {})
        add("It_Recipe_Health_01", s, e)
        add("It_Written", item(1))   -- an item without gCRecipe_PS

        risen = { template = {}, item = {}, balance = {}, recipe = {}, log = {}, gamevar = {}, trace = nil }
        function risen.template.exists(n) return templates[n] ~= nil end
        function risen.template.created() return created end
        function risen.template.list(group)
            local l = {} for k in pairs(templates) do l[#l + 1] = k end table.sort(l) return l
        end
        function risen.template.get(name)
            local t = templates[name]
            if not t then return nil, "unknown template '" .. name .. "'" end
            local h = {}
            function h:property_sets() local l = {} for k in pairs(t.sets) do l[#l + 1] = k end table.sort(l) return l end
            function h:list_properties(set) local l = {} for k in pairs(t.sets[set] or {}) do l[#l + 1] = { name = k, type = "x" } end return l end
            function h:get_property(set, prop) return t.sets[set] and t.sets[set][prop] end
            function h:set_property(set, prop, v) log[#log + 1] = name .. "." .. set .. "." .. prop .. "=" .. tostring(v) t.sets[set][prop] = tostring(v) return true end
            function h:instances() return t.instances end
            function h:focus_name() return t.label or name end
            return setmetatable(h, { __tostring = function() return "Template(" .. name .. ")" end })
        end
        -- risen.template.create: a clone of the base with the item overrides noted (the real one is covered by the template-create tests)
        function risen.template.create(name, base, ov, opts)
            local b = templates[base]
            if not b then return nil, "unknown base" end
            local sets = {}
            for set, p in pairs(b.sets) do sets[set] = {} for k, v in pairs(p) do sets[set][k] = v end end
            local ing = {} for i, v in ipairs(b.ing) do ing[i] = v end
            templates[name] = { sets = sets, instances = {}, ing = ing, req = {}, label = type(ov.name) == "table" and ov.name.English or ov.name, desc = ov.description }
            created[name] = base
            log[#log + 1] = ("create %s base=%s name=%s value=%s"):format(name, base, tostring(templates[name].label), tostring(ov.value))
            return risen.template.get(name), true
        end
        function risen.template.is_planned(n) return planned and planned[n] == true end
        -- natives
        function risen.recipe.ingredients_raw(target)
            local t = templates[type(target) == "string" and target or target.name]
            if not t then return nil, "unknown template '" .. tostring(target) .. "'" end
            if not t.sets.gCRecipe_PS then return nil, "'" .. tostring(target) .. "' is not a recipe" end
            local out = {} for i, e in ipairs(t.ing) do out[i] = { item = e.item, count = e.count } end return out
        end
        function risen.recipe.set_ingredients_raw(target, list)
            local t = templates[type(target) == "string" and target or target.name]
            for _, e in ipairs(list) do if not templates[e.item] then return nil, "the engine did not accept the item '" .. e.item .. "'" end end
            t.ing = {} for i, e in ipairs(list) do t.ing[i] = { item = e.item, count = e.count } end
            log[#log + 1] = "ingredients " .. tostring(type(target) == "string" and target or target.name)
            return true
        end
        function risen.item.skill_array(target, kind)
            local t = templates[target]
            if kind == "recipe_required" then return t and t.req end
            return {}
        end
        function risen.item.set_skill_array(target, kind, entries)
            local t = templates[type(target) == "string" and target or target.name]
            if kind == "recipe_required" then t.req = entries log[#log + 1] = "skills " .. (type(target) == "string" and target or target.name) return true end
            return true
        end
        skills = { Smith = 0 }
        function risen.item.skill_total_raw(who, name) return skills[(name:gsub("^gESkill_%a+_", ""))] or 0, 0 end
        function risen.item.count(who, name) return inventory[name] or 0 end
        function risen.item.give(who, name, n) inventory[name] = (inventory[name] or 0) + n return 0 end
        function risen.item.take(who, name, n) local h = inventory[name] or 0 local k = math.min(h, n) inventory[name] = h - k return k end
        function risen.gamevar.get(n) return vars[n] end
        function risen.gamevar.set(n, v) vars[n] = v return true end
        function risen.recipe.can_craft_raw(who, name)
            local t = templates[name]
            for _, r in ipairs(t.req) do
                local id = r.skill:gsub("^gESkill_Misc_", "")
                if (skills[id] or 0) < r.amount then return 0 end
            end
            local n = 999
            for _, e in ipairs(t.ing) do n = math.min(n, math.floor((inventory[e.item] or 0) / e.count)) end
            if #t.ing == 0 then return 0 end
            return n
        end
        function risen.recipe.craft_raw(who, name, count)
            local t = templates[name]
            for _, e in ipairs(t.ing) do inventory[e.item] = (inventory[e.item] or 0) - e.count * count end
            local result = t.sets.gCRecipe_PS.ResultItem
            inventory[result] = (inventory[result] or 0) + count
            log[#log + 1] = ("craft %s x%d"):format(name, count)
            return 3
        end
        who = { name = "PC_Hero" }
    )LUA");
    spass::game::installTemplatePatchHelper(lua, lua["risen"]);
    spass::game::installBalanceHelpers(lua, lua["risen"]);
    spass::game::installRecipeHelpers(lua, lua["risen"]);
}

TEST(recipe_catalogue_describes_recipes) {
    sol::state lua;
    setupRecipeMock(lua);
    auto run = [&](const char* code) { return lua.script(std::string("return tostring((function() ") + code + " end)())").get<std::string>(); };
    CHECK(run("return table.concat(risen.recipe.stations(), ',')") == "alchemy,anvil,cooking,coolweapon,forge,frying,goldsmith,grindstone,scroll");
    CHECK(run("return table.concat(risen.recipe.names(), ',')") == "It_Recipe_Health_01,It_Shipped_Hot");
    CHECK(run("return table.concat(risen.recipe.names('anvil'), ',')") == "It_Shipped_Hot");
    CHECK(run("return table.concat(risen.recipe.names('Smith_Anvil'), ',') .. '|' .. table.concat(risen.recipe.names('gERecipeCategory_Alchemy'), ',')") == "It_Shipped_Hot|It_Recipe_Health_01");
    CHECK(run("local n, why = risen.recipe.names('bakery') return tostring(n) .. '|' .. why").find("nil|unknown station 'bakery' (valid: alchemy, anvil") == 0);
    CHECK(run("local i = risen.recipe.info('It_Shipped_Hot') return i.station .. ',' .. i.category .. ',' .. #i.ingredients .. ',' .. i.ingredients[1].item .. ',' .. i.result.item .. ',' .. i.result.count .. ',' .. i.skills.Smith .. ',' .. tostring(i.shipped)") ==
          "anvil,gERecipeCategory_Smith_Anvil,1,It_IronOre,It_Result,1,1,true");
    CHECK(run("local i, why = risen.recipe.info('It_Written') return tostring(i) .. '|' .. why") == "nil|'It_Written' is not a recipe (no gCRecipe_PS)");
    CHECK(run("local i, why = risen.recipe.info('It_Nope') return tostring(i) .. '|' .. why").find("nil|unknown template 'It_Nope'") == 0);
    CHECK(run("return risen.recipe.get('It_Recipe_Health_01').station") == "alchemy");
}

TEST(recipe_knowledge_is_carrying_the_recipe_item) {
    sol::state lua;
    setupRecipeMock(lua);
    auto run = [&](const char* code) { return lua.script(std::string("return tostring((function() ") + code + " end)())").get<std::string>(); };
    CHECK(run("return tostring(risen.recipe.known(who, 'It_Shipped_Hot'))") == "false");
    CHECK(run("return tostring(risen.recipe.learn(who, 'It_Shipped_Hot')) .. ',' .. tostring(risen.recipe.known(who, 'It_Shipped_Hot')) .. ',' .. inventory.It_Shipped_Hot") == "true,true,1");
    CHECK(run("return tostring(risen.recipe.learn(who, 'It_Shipped_Hot')) .. ',' .. inventory.It_Shipped_Hot") == "false,1");
    CHECK(run("return tostring(risen.recipe.forget(who, 'It_Shipped_Hot')) .. ',' .. tostring(risen.recipe.known(who, 'It_Shipped_Hot')) .. ',' .. tostring(risen.recipe.forget(who, 'It_Shipped_Hot'))") == "true,false,false");
    CHECK(run("local k, why = risen.recipe.known(who, 'It_Written') return tostring(k) .. '|' .. why") == "nil|'It_Written' is not a recipe (no gCRecipe_PS)");
    // learn_once gives the item once and remembers it in a game variable
    CHECK(run("return tostring(risen.recipe.learn_once(who, 'It_Shipped_Hot')) .. ',' .. inventory.It_Shipped_Hot .. ',' .. tostring(vars.SPASS_RECIPE_It_Shipped_Hot)") == "true,1,1");
    CHECK(run("risen.recipe.forget(who, 'It_Shipped_Hot') return tostring(risen.recipe.learn_once(who, 'It_Shipped_Hot')) .. ',' .. tostring(inventory.It_Shipped_Hot)") == "false,0");
}

TEST(recipe_craft_checks_with_the_engine_and_refuses_cleanly) {
    sol::state lua;
    setupRecipeMock(lua);
    auto run = [&](const char* code) { return lua.script(std::string("return tostring((function() ") + code + " end)())").get<std::string>(); };
    lua.script("risen.recipe.learn(who, 'It_Shipped_Hot')");
    // missing skill and ingredients: 0 with a reason, craft refused, nothing consumed
    CHECK(run("local n, why = risen.recipe.can_craft(who, 'It_Shipped_Hot') return n .. '|' .. why") == "0|skill Smith 1 (has 0); ingredients It_IronOre x1 (has 0)");
    lua.script("skills.Smith = 1");
    CHECK(run("local n, why = risen.recipe.can_craft(who, 'It_Shipped_Hot') return n .. '|' .. why") == "0|ingredients It_IronOre x1 (has 0)");
    CHECK(run("local ok, why = risen.recipe.craft(who, 'It_Shipped_Hot') return tostring(ok) .. '|' .. why").find("nil|cannot craft 1 time(s) (the engine allows 0): missing ingredients It_IronOre x1 (has 0)") == 0);
    CHECK(run("return #log") == "0");
    // two crafts possible with 2 ore; a third request is refused and consumes nothing
    lua.script("inventory.It_IronOre = 2");
    CHECK(run("return risen.recipe.can_craft(who, 'It_Shipped_Hot')") == "2");
    CHECK(run("local ok, res = risen.recipe.craft(who, 'It_Shipped_Hot', 3) return tostring(ok) .. '|' .. res").find("nil|cannot craft 3 time(s) (the engine allows 2)") == 0);
    CHECK(run("return inventory.It_IronOre .. ',' .. tostring(inventory.It_Result)") == "2,nil");
    CHECK(run("local ok, res = risen.recipe.craft(who, 'It_Shipped_Hot', 2) return tostring(ok) .. ',' .. res .. ',' .. inventory.It_IronOre .. ',' .. inventory.It_Result") == "true,It_Result,0,2");
    // the recipe item has to be carried unless opts.unknown
    lua.script("risen.recipe.forget(who, 'It_Shipped_Hot') inventory.It_IronOre = 1");
    CHECK(run("local ok, why = risen.recipe.craft(who, 'It_Shipped_Hot') return tostring(ok) .. '|' .. why").find("nil|'It_Shipped_Hot' is not known") == 0);
    CHECK(run("local ok = risen.recipe.craft(who, 'It_Shipped_Hot', 1, { unknown = true }) return tostring(ok) .. ',' .. inventory.It_IronOre") == "true,0");
    CHECK(run("local ok, why = risen.recipe.craft(who, 'It_Shipped_Hot', 0) return tostring(ok) .. '|' .. why") == "nil|count must be an integer from 1 to 99");
    CHECK(run("local ok, why = risen.recipe.craft(who, 'It_Written') return tostring(ok) .. '|' .. why") == "nil|'It_Written' is not a recipe (no gCRecipe_PS)");
}

TEST(recipe_section_edits_shipped_recipes_after_validating_everything) {
    sol::state lua;
    setupRecipeMock(lua);
    auto run = [&](const char* code) { return lua.script(std::string("return tostring((function() ") + code + " end)())").get<std::string>(); };
    auto refused = [&](const char* spec, const char* needle) {
        const std::string out = run((std::string("local rep, why = risen.balance.apply(") + spec + ") return tostring(rep) .. '|' .. tostring(why)").c_str());
        const bool ok = out.rfind("nil|", 0) == 0 && out.find(needle) != std::string::npos;
        if (!ok) {
            std::printf("    apply(%s) -> %s (wanted nil|...%s...)\n", spec, out.c_str(), needle);
        }
        return ok;
    };
    CHECK(refused("{ recipes = { It_Nope = { skills = {} } } }", "recipes.It_Nope: unknown template 'It_Nope'"));
    CHECK(refused("{ recipes = { It_Written = { skills = {} } } }", "recipes.It_Written: 'It_Written' is not a recipe"));
    CHECK(refused("{ recipes = { It_Shipped_Hot = { skils = {} } } }", "unknown setting 'skils' (valid: counts, ingredients, properties, result, skills, station)"));
    CHECK(refused("{ recipes = { It_Shipped_Hot = 5 } }", "expected a table of settings"));
    CHECK(refused("{ recipes = { It_Shipped_Hot = {} } }", "nothing to do"));
    CHECK(refused("{ recipes = { It_Shipped_Hot = { station = 'bakery' } } }", "station: unknown 'bakery' (valid: alchemy, anvil"));
    CHECK(refused("{ recipes = { It_Shipped_Hot = { ingredients = {} } } }", "a recipe needs at least one ingredient"));
    CHECK(refused("{ recipes = { It_Shipped_Hot = { ingredients = { It_Nope = 1 } } } }", "unknown item 'It_Nope'"));
    CHECK(refused("{ recipes = { It_Shipped_Hot = { ingredients = { It_IronOre = 0 } } } }", "the count of 'It_IronOre' must be an integer from 1 to 999, got 0"));
    CHECK(refused("{ recipes = { It_Shipped_Hot = { ingredients = { { item = 'It_IronOre', count = 1 }, { item = 'It_IronOre', count = 2 } } } } }", "'It_IronOre' is listed twice"));
    CHECK(refused("{ recipes = { It_Shipped_Hot = { ingredients = { It_IronOre = 1 }, counts = { It_IronOre = 2 } } } }", "ingredients and counts exclude each other"));
    CHECK(refused("{ recipes = { It_Shipped_Hot = { counts = { It_1H_Sword = 2 } } } }", "counts.It_1H_Sword: the recipe has no such ingredient (it has: It_IronOre)"));
    CHECK(refused("{ recipes = { It_Shipped_Hot = { counts = { It_IronOre = 'two' } } } }", "counts.It_IronOre must be an integer from 1 to 999"));
    CHECK(refused("{ recipes = { It_Shipped_Hot = { skills = { Smithing = 1 } } } }", "unknown skill 'Smithing'"));
    CHECK(refused("{ recipes = { It_Shipped_Hot = { result = { item = 'It_Result', count = 2 } } } }", "count must be 1 -- the engine's craft function creates exactly one result item"));
    CHECK(refused("{ recipes = { It_Shipped_Hot = { result = 'It_Nope' } } }", "unknown item 'It_Nope'"));
    CHECK(refused("{ recipes = { It_Shipped_Hot = { properties = { gCRecipe_PS = { Craft = 'x' } } } } }", "use station / ingredients / skills / result instead"));
    // one bad entry stops the whole spec
    CHECK(refused("{ recipes = { It_Shipped_Hot = { skills = { Smith = 3 } }, It_Recipe_Health_01 = { station = 'bakery' } } }", "recipes.It_Recipe_Health_01"));
    CHECK(run("return #log") == "0");
    CHECK(run("return table.concat(risen.balance.section_names(), ',')") == "items,new_recipes,recipes");
    // the edits
    CHECK(run("local rep = assert(risen.balance.apply({ recipes = { It_Shipped_Hot = { skills = { Smith = 2 }, counts = { It_IronOre = 4 }, result = 'It_Po_Health_01', station = 'forge' } } })) return rep.values .. ',' .. rep.applied[1]") ==
          "1,recipe It_Shipped_Hot (counts, result, skills, station)");
    CHECK(run("local i = risen.recipe.info('It_Shipped_Hot') return i.station .. ',' .. i.ingredients[1].count .. ',' .. i.result.item .. ',' .. i.skills.Smith") == "forge,4,It_Po_Health_01,2");
    // idempotent, and a list or map of ingredients replaces the whole list
    CHECK(run("assert(risen.balance.apply({ recipes = { It_Shipped_Hot = { counts = { It_IronOre = 4 } } } })) return risen.recipe.info('It_Shipped_Hot').ingredients[1].count") == "4");
    CHECK(run("assert(risen.recipe.modify('It_Recipe_Health_01', { ingredients = { 'It_1H_Sword', { item = 'It_IronOre', count = 2 } }, skills = {} })) "
              "local i = risen.recipe.info('It_Recipe_Health_01') local t = {} for _, e in ipairs(i.ingredients) do t[#t + 1] = e.item .. 'x' .. e.count end return table.concat(t, ',') .. '|' .. tostring(next(i.skills))") == "It_1H_Swordx1,It_IronOrex2|nil");
}

TEST(recipe_new_recipes_are_created_from_a_recipe_item) {
    sol::state lua;
    setupRecipeMock(lua);
    auto run = [&](const char* code) { return lua.script(std::string("return tostring((function() ") + code + " end)())").get<std::string>(); };
    auto refused = [&](const char* spec, const char* needle) {
        const std::string out = run((std::string("local rep, why = risen.balance.apply(") + spec + ") return tostring(rep) .. '|' .. tostring(why)").c_str());
        const bool ok = out.rfind("nil|", 0) == 0 && out.find(needle) != std::string::npos;
        if (!ok) {
            std::printf("    apply(%s) -> %s (wanted nil|...%s...)\n", spec, out.c_str(), needle);
        }
        return ok;
    };
    CHECK(refused("{ new_recipes = { R = { ingredients = { It_IronOre = 1 }, result = 'It_Result' } } }", "'station' is required (alchemy, anvil"));
    CHECK(refused("{ new_recipes = { R = { station = 'anvil', result = 'It_Result' } } }", "'ingredients' is required"));
    CHECK(refused("{ new_recipes = { R = { station = 'anvil', ingredients = { It_IronOre = 1 } } } }", "'result' is required (an item name)"));
    CHECK(refused("{ new_recipes = { R = { station = 'smithy', ingredients = { It_IronOre = 1 }, result = 'It_Result' } } }", "station: unknown 'smithy'"));
    CHECK(refused("{ new_recipes = { R = { station = 'anvil', ingredients = { It_Nope = 1 }, result = 'It_Result' } } }", "unknown item 'It_Nope'"));
    CHECK(refused("{ new_recipes = { R = { station = 'anvil', ingredients = { It_IronOre = 1 }, result = 'It_Nope' } } }", "result: unknown item 'It_Nope'"));
    CHECK(refused("{ new_recipes = { R = { station = 'anvil', ingredients = { It_IronOre = 1 }, result = { item = 'It_Result', count = 3 } } } }", "count must be 1"));
    CHECK(refused("{ new_recipes = { R = { station = 'anvil', ingredients = { It_IronOre = 1 }, result = 'It_Result', base = 'It_Written' } } }", "base: 'It_Written' has no gCRecipe_PS"));
    CHECK(refused("{ new_recipes = { R = { station = 'anvil', ingredients = { It_IronOre = 1 }, result = 'It_Result', base = 'It_Missing' } } }", "base: unknown template 'It_Missing'"));
    CHECK(refused("{ new_recipes = { R = { station = 'anvil', ingredients = { It_IronOre = 1 }, result = 'It_Result', bogus = 1 } } }", "unknown setting 'bogus' (valid: base, description"));
    CHECK(refused("{ new_recipes = { R = { station = 'anvil', ingredients = { It_IronOre = 1 }, result = 'It_Result', learn = 'yes' } } }", "learn must be true or false"));
    CHECK(refused("{ new_recipes = { It_Written = { station = 'anvil', ingredients = { It_IronOre = 1 }, result = 'It_Result' } } }", "a template called 'It_Written' already exists"));
    CHECK(refused("{ new_recipes = { R = { station = 'anvil', ingredients = { It_IronOre = 1 }, result = 'It_Result', skills = { Smithing = 1 } } } }", "unknown skill 'Smithing'"));
    CHECK(refused("{ new_recipes = { R = { station = 'anvil', ingredients = { It_IronOre = 1 }, result = 'It_Result', properties = { gCRecipe_PS = { ResultItem = 'x' } } } } }", "use station / ingredients"));
    CHECK(run("return #log") == "0");
    // a recipe whose result is an item the same spec creates (is_planned)
    lua.script("planned = { Spass_Sword = true }");
    lua.script("templates.Spass_Sword = nil");
    CHECK(refused("{ new_recipes = { R = { station = 'anvil', ingredients = { It_IronOre = 1 }, result = 'Spass_Other' } } }", "result: unknown item 'Spass_Other'"));
    lua.script("risen.template.create('Spass_Sword', 'It_1H_Sword', {})");
    // the full recipe: clone of the default base, the parts written after the clone
    CHECK(run("local rep = assert(risen.balance.apply({ new_recipes = { Spass_Plan = { station = 'Anvil', ingredients = { It_1H_Sword = 1, It_IronOre = 2 }, result = 'Spass_Sword', skills = { Smith = 1 }, "
              "name = 'Smithing plan' } } })) return rep.values .. ',' .. rep.applied[1]") == "1,new recipe Spass_Plan (anvil: 1x It_1H_Sword + 2x It_IronOre -> Spass_Sword)");
    CHECK(run("local i = risen.recipe.info('Spass_Plan') local t = {} for _, e in ipairs(i.ingredients) do t[#t + 1] = e.item .. 'x' .. e.count end return i.station .. ',' .. table.concat(t, '+') .. ',' .. i.result.item .. ',' .. "
              "i.skills.Smith .. ',' .. tostring(i.shipped) .. ',' .. i.label") == "anvil,It_1H_Swordx1+It_IronOrex2,Spass_Sword,1,false,Smithing plan");
    CHECK(run("return templates.Spass_Plan.desc") == "Anvil recipe: 1x It_1H_Sword, 2x It_IronOre -> Spass_Sword.");
    CHECK(run("return table.concat(risen.recipe.names('anvil'), ',')") == "It_Shipped_Hot,Spass_Plan");
    // applying again changes nothing (idempotent), and the whole thing crafts through the engine check
    CHECK(run("assert(risen.recipe.create('Spass_Plan', { station = 'anvil', ingredients = { It_1H_Sword = 1, It_IronOre = 2 }, result = 'Spass_Sword', skills = { Smith = 1 } })) return #risen.recipe.info('Spass_Plan').ingredients") == "2");
    lua.script("skills.Smith = 1 inventory.It_1H_Sword = 1 inventory.It_IronOre = 1");
    CHECK(run("local n, why = risen.recipe.can_craft(who, 'Spass_Plan') return n .. '|' .. why") == "0|ingredients It_IronOre x2 (has 1)");
    lua.script("inventory.It_IronOre = 3 risen.recipe.learn(who, 'Spass_Plan')");
    CHECK(run("local ok, res = risen.recipe.craft(who, 'Spass_Plan') return tostring(ok) .. ',' .. res .. ',' .. inventory.It_1H_Sword .. ',' .. inventory.It_IronOre .. ',' .. inventory.Spass_Sword") == "true,Spass_Sword,0,1,1");
    // the label defaults to the result's label
    lua.script("templates.Spass_Sword.label = 'Fine Sword'");
    CHECK(run("assert(risen.recipe.create('Spass_Plan2', { station = 'forge', ingredients = { 'It_IronOre' }, result = 'Spass_Sword' })) return risen.recipe.info('Spass_Plan2').label") == "Recipe: Fine Sword");
}
