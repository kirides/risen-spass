// Asset files and sounds (core/assets.h): kinds, the compiled sound container, sounds of dialogue lines through compile, export, import and
// validation; with --data the game's speech and sound archives.

// a short 16-bit mono PCM WAV (a tone of `ms` milliseconds)
static std::string test_wav(int ms) {
    const int rate = 8000, n = rate * ms / 1000;
    std::string pcm;
    for (int i = 0; i < n; ++i) {
        const auto s = static_cast<int16_t>(8000 * std::sin(2 * 3.14159265 * 440 * i / rate));
        pcm.push_back(static_cast<char>(s & 0xff));
        pcm.push_back(static_cast<char>((s >> 8) & 0xff));
    }
    std::string w;
    auto u32 = [&](uint32_t v) {
        for (int i = 0; i < 4; ++i) w.push_back(static_cast<char>((v >> (8 * i)) & 0xff));
    };
    auto u16 = [&](uint16_t v) {
        w.push_back(static_cast<char>(v & 0xff));
        w.push_back(static_cast<char>(v >> 8));
    };
    w += "RIFF";
    u32(static_cast<uint32_t>(36 + pcm.size()));
    w += "WAVEfmt ";
    u32(16);
    u16(1);  // PCM
    u16(1);  // mono
    u32(rate);
    u32(rate * 2);
    u16(2);
    u16(16);
    w += "data";
    u32(static_cast<uint32_t>(pcm.size()));
    return w + pcm;
}

TEST(loose_data_files_are_known) {
    CHECK(is_loose_data_file("compiled_combatspecies.bin") && is_loose_data_file("dir/Compiled_CombatSpecies.BIN") && is_loose_data_file("WeatherStates.g3ws"));
    CHECK(!is_loose_data_file("notes.txt") && !is_loose_data_file("X._xmsh"));
    Project p;
    p.assets.push_back({"a1", "assets/compiled_combatspecies.bin", "other"});
    p.assets.push_back({"a2", "assets/readme.txt", "other"});
    int unknown = 0;
    for (auto& x : validate(p, nullptr))
        if (x.area == std::string("Assets") && x.message.find("not a resource the engine loads") != std::string::npos) {
            ++unknown;
            CHECK(x.subject == "readme.txt");
        }
    CHECK_EQ(unknown, 1);
}

TEST(asset_kinds) {
    CHECK_EQ(asset_kind("a/b/X._xmsh"), std::string("mesh"));
    CHECK_EQ(asset_kind("X._XMAT"), std::string("material"));
    CHECK_EQ(asset_kind("X._ximg"), std::string("image"));
    CHECK_EQ(asset_kind("X._xsnd"), std::string("sound"));
    CHECK_EQ(asset_kind("greet.OGG"), std::string("sound"));
    CHECK_EQ(asset_kind("notes.txt"), std::string("other"));
    CHECK(is_raw_audio("x.mp3") && !is_raw_audio("x._xsnd"));
    CHECK(is_compiled_sound("x._xsnd") && !is_compiled_sound("x.wav"));
    CHECK_EQ(sound_language("INFO_ELDRIC_00000556_Druid_German_%_Eldric._xsnd"), std::string("German"));
    CHECK_EQ(sound_language("SVM_Enzo_Attack00_French._xsnd"), std::string("French"));
    CHECK_EQ(sound_language("sfx_door_open_01%_SW._xsnd"), std::string());
    CHECK_EQ(sound_export_path("greet.ogg"), std::string("sounds/greet.ogg"));
    CHECK_EQ(sound_export_path("INFO_X_English_%_Eldric._xsnd"), std::string("sounds/INFO_X_English_%_Eldric.mp3"));
    CHECK_EQ(sound_raw_name("INFO_X_%_Eldric._xsnd"), std::string("INFO_X_%_Eldric.wav"));
}

TEST(sound_container) {
    const std::string wav = test_wav(250);
    std::string why;
    auto raw = open_sound(wav, why);
    CHECK(raw.has_value());
    if (raw) {
        CHECK_EQ(raw->format, std::string("wav"));
        CHECK(!raw->compiled);
        CHECK(raw->duration_ms >= 249 && raw->duration_ms <= 251);
        CHECK_EQ(raw->sample_rate, 8000);
    }
    // the layer's container (src/common/sound_file.h) unpacks to the same file
    const std::string xsnd = spass::sound::buildXsnd(wav, 250, 0);
    auto c = open_sound(xsnd, why);
    CHECK(c.has_value() && c->compiled && c->audio == wav);
    // damaged / not audio
    std::string bad = xsnd;
    for (int i = 20; i < 24; ++i) bad[i] = '\xff';  // payload size far beyond the file
    CHECK(!open_sound(bad, why).has_value() && !why.empty());
    CHECK(!open_sound("hello, this is no audio at all", why).has_value());
}

// a project with one dialogue whose lines carry sounds
static Project sound_project() {
    Project p;
    p.mod.id = "snd_test";
    p.mod.languages = {"English", "German"};
    p.assets.push_back({"a1", "greet.wav", "sound"});
    p.assets.push_back({"a2", "greet_de.wav", "sound"});
    p.assets.push_back({"a3", "Custom_Cue._xsnd", "sound"});
    DialogDef d;
    d.uid = "d1";
    d.name = "Hob";
    d.owner = "MK_Hob";
    GraphNode o = make_node(GraphKind::Dialog, "option", 0, 0);
    o.uid = "o1";
    o.data["info"] = "SND_Hello";
    o.data["text"] = json::object({{"English", "Hello"}});
    o.data["sound"] = json::object({{"default", "greet.wav"}});
    GraphNode l = make_node(GraphKind::Dialog, "line", 300, 0);
    l.uid = "l1";
    l.data["text"] = json::object({{"English", "Hi."}, {"German", "Hallo."}});
    l.data["sound"] = json::object({{"default", "greet.wav"}, {"German", "greet_de.wav"}});
    GraphNode s = make_node(GraphKind::Dialog, "command", 600, 0);
    s.uid = "s1";
    s.data["command"] = "SaySound";
    s.data["params"] = json::object({{"SoundName", "INFO_X_%_Eldric.wav"}});
    s.data["sound"] = json::object({{"default", "Custom_Cue._xsnd"}});
    d.graph.nodes = {o, l, s};
    add_link(GraphKind::Dialog, d.graph, "o1", "then", "l1", "in");
    add_link(GraphKind::Dialog, d.graph, "l1", "next", "s1", "in");
    p.dialogs.push_back(d);
    return p;
}

TEST(dialog_line_sounds) {
    const Project p = sound_project();
    std::vector<DialogIssue> issues;
    const json infos = compile_dialog(p, p.dialogs[0], issues);
    for (auto& i : issues)
        if (i.error) std::printf("  issue: %s\n", i.message.c_str());
    CHECK(std::none_of(issues.begin(), issues.end(), [](const DialogIssue& i) { return i.error; }));
    CHECK_EQ(infos.size(), size_t(1));
    const json& lines = infos[0]["lines"];
    CHECK_EQ(lines[0]["sound"].get<std::string>(), std::string("sounds/greet.wav"));
    CHECK_EQ(lines[1]["sound"]["default"].get<std::string>(), std::string("sounds/greet.wav"));
    CHECK_EQ(lines[1]["sound"]["de"].get<std::string>(), std::string("sounds/greet_de.wav"));
    CHECK_EQ(lines[2]["command"].get<std::string>(), std::string("SaySound"));
    CHECK_EQ(lines[2]["sound"].get<std::string>(), std::string("sounds/Custom_Cue.mp3"));
    CHECK_EQ(lines[2]["SoundName"].get<std::string>(), std::string("INFO_X_%_Eldric.wav"));
    CHECK_EQ(dialog_sound_refs(p), (std::vector<std::string>{"Custom_Cue._xsnd", "greet.wav", "greet_de.wav"}));
    // an audio file that is no asset of the project, a file that is no audio
    Project q = p;
    q.dialogs[0].graph.find("l1")->data["sound"] = json::object({{"default", "missing.ogg"}, {"German", "readme.txt"}});
    issues.clear();
    compile_dialog(q, q.dialogs[0], issues);
    CHECK_EQ(std::count_if(issues.begin(), issues.end(), [](const DialogIssue& i) { return i.error; }), 2);
    // validation: a sound for a language the mod does not have, two assets of one name
    q = p;
    q.dialogs[0].graph.find("l1")->data["sound"]["French"] = "greet.wav";
    q.assets.push_back({"a4", "other/greet.wav", "sound"});
    const auto probs = validate(q, nullptr);
    auto has = [&](const std::string& part) {
        return std::any_of(probs.begin(), probs.end(), [&](const Problem& pr) { return pr.message.find(part) != std::string::npos; });
    };
    CHECK(has("French"));
    CHECK(has("two asset files are called greet.wav"));
}

TEST(export_import_sounds) {
    const fs::path root = temp_dir("sounds");
    const std::string wav = test_wav(300), wav_de = test_wav(200);
    write_text(root / "greet.wav", wav);
    write_text(root / "greet_de.wav", wav_de);
    write_text(root / "Custom_Cue._xsnd", spass::sound::buildXsnd(wav, 300, 0));
    const Project p = sound_project();
    ExportOptions o;
    o.project_dir = u8str(root);
    const fs::path out = root / "mod";
    ExportResult r = export_mod(p, u8str(out), o);
    for (auto& e : r.errors) std::printf("  export: %s\n", e.c_str());
    CHECK(r.ok);
    CHECK_EQ(read_text(out / "sounds" / "greet.wav"), wav);
    CHECK_EQ(read_text(out / "sounds" / "greet_de.wav"), wav_de);
    CHECK_EQ(read_text(out / "sounds" / "Custom_Cue.mp3"), wav);  // unpacked (the layer reads the content, not the extension)
    CHECK(fs::exists(out / "assets" / "Custom_Cue._xsnd"));       // and mounted as it is (SaySound SoundName, shadowing)
    CHECK(!fs::exists(out / "assets" / "greet.wav"));
    CHECK(read_text(out / "mod.toml").find("assets      = \"assets\"") != std::string::npos);
    const std::string dialogs = read_text(out / "data" / "dialogs.lua");
    CHECK(dialogs.find("sounds/greet_de.wav") != std::string::npos);
    // the exported data runs and is what the layer takes
    const json back = lua_data_to_json(dialogs, "dialogs.lua");
    CHECK_EQ(back["infos"][0]["lines"][1]["sound"]["de"].get<std::string>(), std::string("sounds/greet_de.wav"));
    // a game sound without game data: refused with a reason
    Project q = p;
    q.dialogs[0].graph.find("o1")->data["sound"] = json::object({{"default", "INFO_ELDRIC_1_Druid_English_%_Eldric._xsnd"}});
    r = export_mod(q, u8str(out), o);
    CHECK(!r.ok);
    CHECK(!r.errors.empty() && r.errors[0].find("needs the game data") != std::string::npos);
    // a stale sound goes away
    Project s = p;
    s.dialogs[0].graph.find("l1")->data["sound"].erase("German");
    s.assets.erase(s.assets.begin() + 1);
    r = export_mod(s, u8str(out), o);
    CHECK(r.ok && !fs::exists(out / "sounds" / "greet_de.wav"));
    r = export_mod(p, u8str(out), o);
    CHECK(r.ok);
    // import from the data files: the lines keep their sounds (by file name), the files become assets again
    ImportResult im = import_mod(u8str(out), true);
    CHECK(im.ok);
    std::set<std::string> asset_names;
    for (auto& a : im.project.assets) asset_names.insert(u8str(u8(a.file).filename()) + ":" + a.kind);
    CHECK(asset_names.count("greet.wav:sound") && asset_names.count("greet_de.wav:sound") && asset_names.count("Custom_Cue._xsnd:sound"));
    bool line_ok = false, say_ok = false;
    for (auto& d : im.project.dialogs)
        for (auto& n : d.graph.nodes) {
            if (n.type == "line" && n.data.contains("sound"))
                line_ok = n.data["sound"].value("German", std::string()) == "greet_de.wav" && n.data["sound"].value("default", std::string()) == "greet.wav";
            if (n.type == "command" && n.data.contains("sound")) {
                say_ok = n.data["sound"].value("default", std::string()) == "Custom_Cue.mp3" && !n.data["params"].contains("sound");
            }
        }
    CHECK(line_ok);
    CHECK(say_ok);
}

TEST(gamedata_sounds) {
    auto g = game_data();
    if (!g) {
        std::printf("  (skipped: no --data)\n");
        return;
    }
    const auto& snd = g->assets("_xsnd");
    std::map<std::string, int> by_lang;
    for (auto& a : snd) by_lang[sound_language(a.name)]++;
    for (auto& [l, n] : by_lang) std::printf("  %-10s %d sounds\n", l.empty() ? "(none)" : l.c_str(), n);
    CHECK(by_lang["English"] > 5000 && by_lang["German"] > 5000);
    CHECK(by_lang[""] > 500);  // effects, music
    // a shipped speech file opens as MP3 (RE.md section 28) with the length its container says
    int opened = 0;
    for (auto& a : snd) {
        if (sound_language(a.name) != "English" || !p2m::starts_with(a.name, "INFO_")) continue;
        std::string why;
        const std::string bytes = g->read(a.name);
        auto s = open_sound(bytes, why);
        CHECK(s.has_value());
        if (!s) {
            std::printf("  %s: %s\n", a.name.c_str(), why.c_str());
            break;
        }
        CHECK_EQ(s->format, std::string("mp3"));
        CHECK(s->compiled);
        const auto stored = spass::sound::xsndDurationMs(bytes);
        CHECK(stored > 0 && (s->duration_ms + 150 >= stored && s->duration_ms <= stored + 150));
        if (++opened == 20) break;
    }
    CHECK_EQ(opened, 20);
    // an effect opens too
    for (auto& a : snd)
        if (sound_language(a.name).empty() && p2m::starts_with(p2m::lower(a.name), "sfx_")) {
            std::string why;
            CHECK(open_sound(g->read(a.name), why).has_value());
            break;
        }
}
