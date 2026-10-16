// Hardening (docs/DEVELOPMENT.md "Hardening"): a native with typed parameters must refuse wrong Lua values with a Lua error, never crash the process.
// The live counterpart is the scenario `hardening` (mods/hardening): every public function of the real API with a battery of wrong arguments.

// sol2 only checks the arguments of a native (and so turns `risen.npc.routine_points(5)` into an error instead of reading a null `const LuaEntity&`) when it
// does not assume that exceptions can travel through Lua's frames. With LuaJIT it assumes so by default: CMakeLists.txt defines SOL_EXCEPTIONS_SAFE_PROPAGATION=0.
#if SOL_IS_ON(SOL_PROPAGATE_EXCEPTIONS)
#error "SOL_EXCEPTIONS_SAFE_PROPAGATION=0 is missing: typed natives would not check their arguments"
#endif

TEST(hardening_typed_natives_refuse_wrong_types_with_a_lua_error) {
    sol::state lua;
    lua.open_libraries(sol::lib::base, sol::lib::string);
    lua.new_usertype<spass::game::LuaEntity>("Entity", sol::no_constructor);
    struct Other {
        int x = 0;
    };
    lua.new_usertype<Other>("Other", sol::no_constructor);
    lua["other"] = Other{};
    lua["entity"] = spass::game::LuaEntity{};
    lua.set_function("takes_entity", [](const spass::game::LuaEntity& e) { return static_cast<int>(e.name.size()); });
    lua.set_function("takes_two", [](const spass::game::LuaEntity& a, const spass::game::LuaEntity& b) { return a.name.size() + b.name.size(); });
    lua.set_function("takes_int", [](int x) { return x; });
    lua.set_function("takes_float", [](float x) { return x; });
    lua.set_function("takes_string", [](const std::string& s) { return s.size(); });
    lua.set_function("takes_bool", [](bool b) { return b; });
    lua.set_function("takes_table", [](sol::table t) { return t.size(); });
    lua.set_function("takes_function", [](sol::protected_function f) { return f.valid(); });
    lua.set_function("takes_optional_entity", [](sol::optional<spass::game::LuaEntity> e) { return e.has_value(); });

    // every row: native, arguments that are NOT acceptable; each must be refused with an error, and the Lua state must stay usable
    struct Row {
        const char* fn;
        std::vector<const char*> bad;
    };
    const std::vector<Row> rows = {
        {"takes_entity", {"", "nil", "5", "'x'", "true", "{}", "other", "function() end"}},
        {"takes_two", {"entity", "entity, 5", "entity, other", "other, entity", "entity, nil"}},
        {"takes_int", {"", "nil", "'x'", "{}", "true", "other", "1.5", "0/0", "1e300"}},
        {"takes_float", {"", "nil", "'x'", "{}", "other"}},
        {"takes_string", {"", "nil", "{}", "true", "other", "function() end"}},
        {"takes_table", {"", "nil", "5", "'x'", "other", "true"}},
    };
    int refused = 0;
    for (const Row& row : rows) {
        for (const char* args : row.bad) {
            const std::string code = std::string("return pcall(") + row.fn + (*args ? ", " : "") + args + ")";
            sol::protected_function_result r = lua.safe_script(code, sol::script_pass_on_error);
            CHECK(r.valid());
            if (!r.valid()) {
                continue;
            }
            const bool ok = r.get<bool>(0);
            CHECK(!ok);
            refused += ok ? 0 : 1;
        }
    }
    CHECK(refused > 30);
    // an optional parameter is not an error for a wrong type, it is "absent"; and a good call still works afterwards
    CHECK(lua.safe_script("return takes_optional_entity(5) == false and takes_optional_entity(entity) == true", sol::script_pass_on_error).get<bool>());
    CHECK(lua.safe_script("return takes_entity(entity) == 0 and takes_int(7) == 7 and takes_string('ab') == 2 and takes_table({1, 2}) == 2", sol::script_pass_on_error).get<bool>());
}

TEST(hardening_methods_forwarding_to_lua_survive_a_call_without_arguments) {
    // Entity:can_use() / skill_total() / ... forward their arguments to the Lua functions of risen.item through `sol::variadic_args`. Called with no argument at all
    // that list has a negative size, and iterating it ran for ever (the game hung in the fuzz scenario).
    sol::state lua;
    lua.open_libraries(sol::lib::base, sol::lib::string);
    auto entity = lua.new_usertype<spass::game::LuaEntity>("Entity", sol::no_constructor);
    spass::game::addBalanceMethods(entity);
    lua["e"] = spass::game::LuaEntity{};
    // no `spass.item` table exists in this state: the forwarders answer nil, reason
    CHECK(lua.safe_script("local a, b = pcall(Entity.can_use) return a == true and type(b) == 'nil'", sol::script_pass_on_error).get<bool>());
    CHECK(lua.safe_script("local ok = pcall(Entity.skill_total) return ok", sol::script_pass_on_error).get<bool>());
    CHECK(lua.safe_script("local ok = pcall(Entity.requirements) return ok", sol::script_pass_on_error).get<bool>());
    CHECK(lua.safe_script("local ok = pcall(Entity.set_requirements, nil) return ok", sol::script_pass_on_error).get<bool>());
    CHECK(lua.safe_script("local ok = pcall(Entity.set_modifiers, e, {}, {}) return ok", sol::script_pass_on_error).get<bool>());
}
