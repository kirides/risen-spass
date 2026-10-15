// ---- quick loot building blocks (key names, risen.config) and the pure policy module of mods/quickloot ------------------------------------------------
// Included by test_main.cpp inside its anonymous namespace.

TEST(input_key_names) {
    using spass::input::vkFromName;
    CHECK(vkFromName("RButton") == 0x02 && vkFromName("rbutton") == 0x02 && vkFromName("LButton") == 0x01 && vkFromName("MButton") == 0x04);
    CHECK(vkFromName("V") == 'V' && vkFromName("v") == 'V' && vkFromName("7") == '7');
    CHECK(vkFromName("F5") == 0x74 && vkFromName("f1") == 0x70 && vkFromName("F24") == 0x87 && !vkFromName("F25") && !vkFromName("F0"));
    CHECK(vkFromName("Shift") == 0x10 && vkFromName("Ctrl") == 0x11 && vkFromName("Alt") == 0x12 && vkFromName("LShift") == 0xA0);
    CHECK(vkFromName("Space") == 0x20 && vkFromName("Num3") == 0x63 && !vkFromName("SecondaryAction") && !vkFromName("") && !vkFromName("Num10"));
    CHECK(spass::input::foldName("SecondaryAction") == "secondaryaction" && spass::input::trim("  V ") == "V");
}

TEST(config_file_overrides_defaults_by_type) {
    TempDir dir;
    dir.write("config.toml", "key = \"V\"\nitems = false\nrepeat_ms = 120\nratio = 2\nbogus = 1\nprint = \"yes\"\n[nested]\ndepth = 3\n");
    sol::state lua;
    lua.open_libraries(sol::lib::base);
    sol::table defaults = lua.create_table();
    defaults["key"] = "SecondaryAction";
    defaults["items"] = true;
    defaults["print"] = true;
    defaults["repeat_ms"] = 80;
    defaults["ratio"] = 0.5;
    sol::table nested = lua.create_table();
    nested["depth"] = 1;
    nested["name"] = "x";
    defaults["nested"] = nested;
    std::vector<std::string> problems;
    sol::table out = spass::game::loadModConfig(lua, dir.path / "config.toml", defaults, problems);
    CHECK(out.get<std::string>("key") == "V" && out.get<bool>("items") == false);
    CHECK(out.get<double>("repeat_ms") == 120 && out.get<double>("ratio") == 2);
    CHECK(out.get<bool>("print") == true);                                              // wrong type: the default stays
    CHECK(out.get<sol::table>("nested").get<double>("depth") == 3 && out.get<sol::table>("nested").get<std::string>("name") == "x");
    CHECK(problems.size() == 2);                                                        // `bogus` is unknown, `print` has the wrong type
    CHECK(defaults.get<bool>("items") == true);                                         // the defaults table is not touched
    // a missing file is no problem, a broken one falls back to the defaults and says so
    problems.clear();
    sol::table none = spass::game::loadModConfig(lua, dir.path / "missing.toml", defaults, problems);
    CHECK(problems.empty() && none.get<std::string>("key") == "SecondaryAction");
    dir.write("broken.toml", "key = [");
    sol::table broken = spass::game::loadModConfig(lua, dir.path / "broken.toml", defaults, problems);
    CHECK(problems.size() == 1 && broken.get<bool>("items") == true);
}

TEST(quickloot_policy_decides_from_the_interaction_scripts) {
    sol::state lua;
    lua.open_libraries(sol::lib::base, sol::lib::string, sol::lib::table);
    std::ifstream f(fs::path(SPASS_SOURCE_DIR) / "mods" / "quickloot" / "policy.lua", std::ios::binary);
    CHECK(f.good());
    const std::string src((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    sol::protected_function_result r = lua.safe_script(src, sol::script_pass_on_error);
    CHECK(r.valid());
    sol::table policy = r;
    lua.set("policy", policy);
    lua.script(R"LUA(
        local function it(can, pre, interact, post) return { type = policy.PLAYER, can = can, pre = pre, interact = interact, post = post or "" } end
        result = {}
        local function kind(i) local k, why = policy.kind_of(i); return k or ("-" .. tostring(why ~= nil)) end
        result.item = kind(it("", "", "Interact_Player_TakeItem"))
        result.quest_item = kind(it("", "PreInteract_Magic_Item", "Interact_Player_TakeItem"))
        result.corpse = kind(it("CanInteract_Player_Loot", "PreInteract_Player_Loot", "Interact_Player_Loot"))
        result.wolf = kind(it("CanInteract_Player_Wolf", "PreInteract_Player_Loot", "Interact_Player_Loot"))
        result.chest = kind(it("CanInteract_Player_Chest", "PreInteract_Player_Chest", "Interact_Player_Chest"))
        result.cupboard = kind(it("CanInteract_Player_Cupboard", "PreInteract_Player_Cupboard", "Interact_Player_Cupboard"))
        result.sarcophagus = kind(it("CanInteract_Player_Sarcophagus", "PreInteract_Player_Sarcophagus", "Interact_Player_Sarcophagus"))
        result.titan = kind(it("CanInteract_Player_SarcophagusTitanlord", "PreInteract_Player_Sarcophagus", "Interact_Player_Sarcophagus"))
        result.trap = kind(it("CanInteract_Player_Sarcophagus_Trap", "", "Interact_Player_Sarcophagus_Trap"))
        result.post_script = kind(it("CanInteract_Player_Chest", "PreInteract_Player_Chest", "Interact_Player_Chest", "Something"))
        result.bed = kind(it("CanInteract_Player", "PreInteract_Player_Sleep", "Interact_Player_Sleep"))
        -- the engine's choice: the first player interaction whose can script accepts
        local info = { interactions = {
            { type = "gEInteractionType_Interact_NPC", can = "", pre = "", interact = "x", post = "" },
            it("CanInteract_Player_Talk", "", "Interact_Player_Talk"),
            it("CanInteract_Player_Loot", "PreInteract_Player_Loot", "Interact_Player_Loot") } }
        local asked = {}
        local chosen = policy.engine_choice(info, function(name) asked[#asked + 1] = name; return name == "CanInteract_Player_Loot" end)
        result.choice = chosen and chosen.interact or "nil"
        result.asked = table.concat(asked, ",")
        -- locks
        local has = function(t) return t == "It_Key" end
        result.lock_none = (policy.lock_plan(nil, has))
        result.lock_open = (policy.lock_plan({ Status = "gELockStatus_Unlocked" }, has))
        result.lock_key = (policy.lock_plan({ Status = "gELockStatus_Locked", Key = "It_Key", OnLockStatusChanged = "" }, has))
        result.lock_nokey = (policy.lock_plan({ Status = "gELockStatus_Locked", Key = "It_Other", OnLockStatusChanged = "" }, has))
        result.lock_notemplate = (policy.lock_plan({ Status = "gELockStatus_Locked", Key = "(no template)" }, has))
        result.lock_script = (policy.lock_plan({ Status = "gELockStatus_Locked", Key = "It_Key", OnLockStatusChanged = "OnQuestSuccess_X" }, has))
        result.witness = tostring(policy.is_witness("gESpecies_Human", false, false)) .. tostring(policy.is_witness("gESpecies_Human", true, false))
            .. tostring(policy.is_witness("gESpecies_Human", false, true)) .. tostring(policy.is_witness("gESpecies_Wolf", false, false))
    )LUA");
    sol::table res = lua["result"];
    const auto get = [&](const char* k) { return res.get<std::string>(k); };
    CHECK(get("item") == "item" && get("corpse") == "corpse" && get("chest") == "container" && get("cupboard") == "container" && get("sarcophagus") == "container");
    CHECK(get("quest_item") == "-true" && get("wolf") == "-true" && get("titan") == "-true" && get("trap") == "-true" && get("post_script") == "-true" && get("bed") == "-true");
    CHECK(get("choice") == "Interact_Player_Loot" && get("asked") == "CanInteract_Player_Talk,CanInteract_Player_Loot");
    CHECK(get("lock_none") == "none" && get("lock_open") == "open" && get("lock_key") == "key");
    CHECK(get("lock_nokey") == "needs lockpick" && get("lock_notemplate") == "needs lockpick" && get("lock_script") == "needs lockpick");
    CHECK(get("witness") == "truefalsefalsefalse");
}

TEST(trace_channels_calls_and_hud_exist) {
    using spass::trace::parseChannels;
    CHECK(parseChannels("calls,hud").mask != 0 && parseChannels("calls,hud").custom.empty());
}
