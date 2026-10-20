// The Strings table and the voice recording workflow (core/voice.h, core/audio_convert.h, core/process.h): rows of the project and their ids, the export
// (CSV / JSON / scripts), the matcher of recordings, the import into the project, the ffmpeg command line and its probing with a mocked process runner,
// and (when ffmpeg is on PATH) a real conversion.

// a project with a dialogue: an option, an NPC line and a SaySound command; a quest log node, an item and a free string row
static Project voice_project() {
    Project p;
    p.mod.id = "vox";
    p.mod.name = "Voice test";
    p.mod.languages = {"English", "German"};
    p.assets.push_back({"a1", "voice/vox_1111aaaa_English.ogg", "sound"});
    DialogDef d;
    d.uid = "d1";
    d.name = "Greeting";
    d.owner = "VOX_Hob";
    GraphNode o = make_node(GraphKind::Dialog, "option", 0, 0);
    o.uid = "1111aaaa000000000000000000000000";
    o.data["text"] = json::object({{"English", "Who are you?"}, {"German", "Wer bist du?"}});
    o.data["sound"] = json::object({{"English", "vox_1111aaaa_English.ogg"}});
    GraphNode l = make_node(GraphKind::Dialog, "line", 300, 0);
    l.uid = "2222bbbb000000000000000000000000";
    l.data["text"] = json::object({{"English", "I am Hob, the \"farmer\", from here."}, {"German", "Ich bin Hob."}});
    l.data["stage"] = json::object({{"English", "grumpy"}});
    GraphNode s = make_node(GraphKind::Dialog, "command", 600, 0);
    s.uid = "3333cccc000000000000000000000000";
    s.data["command"] = "SaySound";
    s.data["text"] = json::object({{"English", "(sigh)"}});
    GraphNode o2 = make_node(GraphKind::Dialog, "option", 0, 200);
    o2.uid = "4444dddd000000000000000000000000";
    o2.data["text"] = json::object({{"English", "Bye."}});
    d.graph.nodes = {o, l, s, o2};
    add_link(GraphKind::Dialog, d.graph, o.uid, "then", l.uid, "in");
    add_link(GraphKind::Dialog, d.graph, l.uid, "next", s.uid, "in");
    p.dialogs.push_back(d);
    QuestDef q;
    q.uid = "q1";
    q.name = "VOX_Quest";
    q.title = {{"English", "The farmer"}};
    GraphNode lg = make_node(GraphKind::Quest, "log", 0, 0);
    lg.uid = "5555eeee000000000000000000000000";
    lg.data["text"] = json::object({{"English", "Hob wants help."}});
    q.graph.nodes.push_back(lg);
    p.quests.push_back(q);
    ItemDef it;
    it.uid = "i1";
    it.name = "VOX_Hoe";
    it.base = "It_Hoe";
    it.label = {{"English", "Hob's hoe"}};
    p.items.push_back(it);
    p.strings.push_back({"s1", "HUD2_Btn_Test", {{"English", "Test"}}, ""});
    return p;
}

static const StringLine* line_by_id(const std::vector<StringLine>& v, const std::string& id) {
    for (auto& l : v)
        if (l.id == id) return &l;
    return nullptr;
}

TEST(voice_project_rows) {
    const Project p = voice_project();
    const auto rows = project_lines(p);
    const StringLine* opt = line_by_id(rows, "vox_1111aaaa");
    const StringLine* npc = line_by_id(rows, "vox_2222bbbb");
    const StringLine* cue = line_by_id(rows, "vox_3333cccc");
    CHECK(opt && npc && cue);
    if (!opt || !npc || !cue) return;
    CHECK(opt->spoken && opt->speaker == "PC_Hero" && opt->voice == "Player" && opt->listener == "VOX_Hob" && opt->order == 1);
    CHECK_EQ(opt->runtime_key, std::string("vox_1111aaaa_1"));  // the default info name is <mod>_<uid8>, line 1 is the option itself
    CHECK(npc->spoken && npc->speaker == "VOX_Hob" && npc->order == 2 && npc->parent == "vox_1111aaaa");
    CHECK_EQ(npc->runtime_key, std::string("vox_1111aaaa_2"));
    CHECK_EQ(npc->stage.at("English"), std::string("grumpy"));
    CHECK(cue->spoken && cue->field == "command" && cue->runtime_key == "vox_1111aaaa_3");
    // the option id is the info name the export gives it
    CHECK_EQ(info_name(p, p.dialogs[0].graph.nodes[0]), opt->id);
    // text rows of other objects
    CHECK(line_by_id(rows, "quest.VOX_Quest.title") && !line_by_id(rows, "quest.VOX_Quest.title")->spoken);
    CHECK(line_by_id(rows, "vox_5555eeee") && line_by_id(rows, "vox_5555eeee")->kind == "quest");
    const StringLine* hoe = line_by_id(rows, "item.VOX_Hoe.label");
    CHECK(hoe && hoe->string_id == "FO_VOX_Hoe");
    CHECK(line_by_id(rows, "HUD2_Btn_Test") && line_by_id(rows, "HUD2_Btn_Test")->kind == "string");
    // status
    CHECK(audio_status(p, *opt, "English") == AudioStatus::Recorded);
    CHECK(audio_status(p, *opt, "German") == AudioStatus::Missing);
    CHECK(audio_status(p, *npc, "English") == AudioStatus::Missing);
    CHECK(audio_status(p, *hoe, "English") == AudioStatus::None);
    // the names: the file the studio delivers and the sound the layer makes of it (src/common/sound_file.h voiceBaseName)
    CHECK_EQ(expected_audio_file(*npc, "German"), std::string("vox_2222bbbb_German.ogg"));
    CHECK_EQ(runtime_sound(p, *npc, "German"), spass::sound::voiceBaseName("vox", "vox_1111aaaa_2", "German") + ".wav");
    CHECK_EQ(runtime_sound(p, *npc, "German"), std::string("INFO_SPASS_vox_vox_1111aaaa_2_German_%_SPASS.wav"));
}

TEST(voice_ids_are_stable) {
    Project p = voice_project();
    const auto before = project_lines(p);
    // a save / load round trip, a renamed dialogue and moved nodes keep the ids of the dialogue lines
    Project q = project_from_json(json::parse(dump_project(p)));
    q.dialogs[0].name = "Renamed";
    q.dialogs[0].graph.nodes[1].x += 500;
    const auto after = project_lines(q);
    for (const char* id : {"vox_1111aaaa", "vox_2222bbbb", "vox_3333cccc", "vox_4444dddd"}) {
        CHECK(line_by_id(before, id) != nullptr);
        CHECK(line_by_id(after, id) != nullptr);
    }
    // the runtime keys follow what the exported dialogue gets: line n of the option's info
    std::vector<DialogIssue> issues;
    const json infos = compile_dialog(q, q.dialogs[0], issues);
    CHECK_EQ(infos[0]["name"].get<std::string>(), std::string("vox_1111aaaa"));
    CHECK(infos[0]["lines"].size() >= 3);
    CHECK_EQ(infos[0]["lines"][1]["text"]["default"].get<std::string>(), std::string("I am Hob, the \"farmer\", from here."));
}

TEST(voice_csv_export) {
    const Project p = voice_project();
    const auto rows = project_lines(p);
    std::vector<const StringLine*> ptrs;
    for (auto& r : rows) ptrs.push_back(&r);
    VoiceExportOptions o;
    const auto vr = voice_rows(p, ptrs, o);
    CHECK_EQ(vr.size(), size_t(8));  // 4 spoken lines x 2 languages
    o.missing_only = true;
    CHECK_EQ(voice_rows(p, ptrs, o).size(), size_t(7));  // the option is recorded in English
    o.missing_only = false;
    o.spoken_only = false;
    CHECK(voice_rows(p, ptrs, o).size() > vr.size());
    o.languages = {"German"};
    o.spoken_only = true;
    CHECK_EQ(voice_rows(p, ptrs, o).size(), size_t(4));
    const std::string csv = voice_csv(vr);
    CHECK(csv.rfind("\xEF\xBB\xBF" "line_id,language,status,", 0) == 0);
    CHECK(csv.find("\r\n") != std::string::npos);
    // quoting round trip: commas and quotes in a text
    const auto parsed = parse_csv(csv);
    CHECK_EQ(parsed.size(), vr.size() + 1);
    CHECK_EQ(parsed[0].size(), voice_csv_columns().size());
    bool found = false;
    for (auto& r : parsed)
        if (r[0] == "vox_2222bbbb" && r[1] == "English") {
            found = true;
            CHECK_EQ(r[6], std::string("I am Hob, the \"farmer\", from here."));
            CHECK_EQ(r[7], std::string("grumpy"));
            CHECK_EQ(r[2], std::string("missing"));
            CHECK_EQ(r[15], std::string("vox_2222bbbb_English.ogg"));
            CHECK_EQ(r[16], std::string("INFO_SPASS_vox_vox_1111aaaa_2_English_%_SPASS.wav"));
        }
    CHECK(found);
    const json j = voice_json(p, vr);
    CHECK_EQ(j["lines"].size(), vr.size());
    CHECK_EQ(j["lines"][0]["line_id"].get<std::string>(), std::string("vox_1111aaaa"));
    const auto scripts = voice_scripts(p, vr);
    CHECK(scripts.count("VOX_Hob_English.txt") && scripts.count("PC_Hero_German.txt"));
    CHECK(scripts.at("VOX_Hob_English.txt").find("(grumpy)") != std::string::npos);
    // written to a folder
    const fs::path dir = temp_dir("voice_export");
    const auto res = write_voice_export(p, ptrs, VoiceExportOptions{}, u8str(dir));
    CHECK(res.ok && fs::exists(dir / "voice_lines.csv") && fs::exists(dir / "voice_lines.json") && fs::exists(dir / "scripts" / "VOX_Hob_German.txt"));
}

TEST(voice_matcher) {
    const Project p = voice_project();
    const auto rows = project_lines(p);
    const std::vector<std::string> langs = p.mod.languages;
    const auto rep = match_recordings(p, rows,
                                      {"in/vox_2222bbbb_English.wav", "in/VOX_2222BBBB_de.flac", "in/vox_4444dddd.mp3", "in/INFO_SPASS_vox_vox_1111aaaa_3_German_%_SPASS.ogg",
                                       "in/unknown_English.ogg", "in/notes.txt", "in/vox_1111aaaa_German.ogg", "in/vox_1111aaaa_German.wav"},
                                      langs, "English");
    auto matched = [&](const std::string& file) -> const VoiceMatch* {
        for (auto& m : rep.matched)
            if (m.file == file) return &m;
        return nullptr;
    };
    const VoiceMatch* a = matched("in/vox_2222bbbb_English.wav");
    CHECK(a && a->line_id == "vox_2222bbbb" && a->language == "English" && a->node_uid == "2222bbbb000000000000000000000000");
    const VoiceMatch* b = matched("in/VOX_2222BBBB_de.flac");  // case and a language code
    CHECK(b && b->language == "German");
    const VoiceMatch* c = matched("in/vox_4444dddd.mp3");  // no language: the default one
    CHECK(c && c->language == "English" && c->how == "name (default language)");
    const VoiceMatch* d = matched("in/INFO_SPASS_vox_vox_1111aaaa_3_German_%_SPASS.ogg");  // the layer's own name
    CHECK(d && d->line_id == "vox_3333cccc" && d->language == "German" && d->how == "runtime name");
    CHECK_EQ(rep.unmatched.size(), size_t(2));  // unknown_English.ogg and notes.txt
    CHECK_EQ(rep.duplicates.size(), size_t(1));  // two files for vox_1111aaaa / German
    CHECK(!matched("in/vox_1111aaaa_German.ogg") && !matched("in/vox_1111aaaa_German.wav"));
    CHECK_EQ(rep.matched.size(), size_t(4));

    // a filled CSV names the files freely (bare name or relative path), and wins over the names
    std::string err;
    const auto csv = csv_assignments("\xEF\xBB\xBFline_id,language,text,file\r\nvox_2222bbbb,German,\"Ich, bin\",take_07.wav\r\nvox_4444dddd,English,x,Sub/bye final.WAV\r\n"
                                     "vox_3333cccc,English,x,take_07.wav\r\nmissing_line,English,x,ghost.wav\r\n",
                                     err);
    CHECK(err.empty() && csv.size() == 4);
    const auto rep2 = match_recordings(p, rows, {"C:/rec/take_07.wav", "C:/rec/Sub/bye final.wav", "C:/rec/ghost.wav", "C:/rec/vox_2222bbbb_English.ogg"}, langs, "English", &csv);
    CHECK_EQ(rep2.ambiguous.size(), size_t(1));  // take_07.wav is named for two lines
    CHECK(rep2.ambiguous.size() == 1 && rep2.ambiguous[0].second.size() == 2);
    bool bye = false, by_name = false;
    for (auto& m : rep2.matched) {
        if (m.line_id == "vox_4444dddd" && m.how == "csv") bye = true;
        if (m.line_id == "vox_2222bbbb" && m.language == "English" && m.how == "name") by_name = true;
    }
    CHECK(bye && by_name);
    CHECK(rep2.unmatched.size() == 1 && rep2.unmatched[0].second.find("missing_line") != std::string::npos);
    std::string bad;
    csv_assignments("a,b\r\n1,2\r\n", bad);
    CHECK(!bad.empty());
}

TEST(voice_import_apply) {
    Project p = voice_project();
    const fs::path dir = temp_dir("voice_import");
    write_text(dir / "rec" / "vox_2222bbbb_German.wav", test_wav(300));
    write_text(dir / "rec" / "vox_4444dddd_English.flac", "fLaC not really");  // needs a conversion
    const auto rows = project_lines(p);
    VoiceImportOptions o;
    // without ffmpeg: the WAV is copied, the FLAC is listed with how to get ffmpeg
    VoicePreview pv = preview_voice_import(p, rows, u8str(dir / "rec"), "", "English", o, nullptr);
    CHECK(pv.error.empty() && pv.files == 2 && pv.steps.size() == 2);
    const VoiceImportStep* wav = nullptr;
    const VoiceImportStep* flac = nullptr;
    for (auto& s : pv.steps) (s.match.line_id == "vox_2222bbbb" ? wav : flac) = &s;
    CHECK(wav && !wav->skip && !wav->convert && wav->dest_rel == "voice/vox_2222bbbb_German.wav");
    CHECK(flac && flac->skip && flac->why.find("ffmpeg") != std::string::npos);
    const auto done = run_voice_import(pv.steps, u8str(dir), o, nullptr, run_process, {}, nullptr);
    CHECK_EQ(done.size(), size_t(1));
    CHECK(fs::exists(dir / "voice" / "vox_2222bbbb_German.wav"));
    const std::string before = dump_project(p);
    CHECK_EQ(apply_voice_import(p, done), 1);
    const GraphNode* n = p.dialogs[0].graph.find("2222bbbb000000000000000000000000");
    CHECK(n && n->data["sound"]["German"] == "vox_2222bbbb_German.wav");
    CHECK(find_asset(p, "vox_2222bbbb_German.wav") && find_asset(p, "vox_2222bbbb_German.wav")->file == "voice/vox_2222bbbb_German.wav");
    CHECK(audio_status(p, *line_by_id(project_lines(p), "vox_2222bbbb"), "German") == AudioStatus::Recorded);
    CHECK_EQ(apply_voice_import(p, done), 0);  // again: nothing changes
    // the exported line plays it
    std::vector<DialogIssue> issues;
    const json infos = compile_dialog(p, p.dialogs[0], issues);
    CHECK_EQ(infos[0]["lines"][1]["sound"]["de"].get<std::string>(), std::string("sounds/vox_2222bbbb_German.wav"));
    // undo is a snapshot of the model
    History h;
    Project q = project_from_json(json::parse(before));
    h.reset(q);
    apply_voice_import(q, done);
    h.touch();
    CHECK(h.settle(q));
    CHECK(h.undo(q));
    CHECK(dump_project(q) == before);
}

TEST(voice_edit_rows) {
    Project p = voice_project();
    auto rows = project_lines(p);
    CHECK(set_line_text(p, *line_by_id(rows, "vox_2222bbbb"), "German", "Ich bin's."));
    CHECK(set_line_stage(p, *line_by_id(rows, "vox_2222bbbb"), "German", "leise"));
    CHECK(set_line_text(p, *line_by_id(rows, "item.VOX_Hoe.label"), "German", "Hobs Hacke"));
    CHECK(set_line_text(p, *line_by_id(rows, "quest.VOX_Quest.title"), "German", "Der Bauer"));
    rows = project_lines(p);
    CHECK_EQ(line_by_id(rows, "vox_2222bbbb")->text.at("German"), std::string("Ich bin's."));
    CHECK_EQ(line_by_id(rows, "vox_2222bbbb")->stage.at("German"), std::string("leise"));
    CHECK_EQ(p.items[0].label.at("German"), std::string("Hobs Hacke"));
    CHECK_EQ(p.quests[0].title.at("German"), std::string("Der Bauer"));
    // a game row becomes a free string row once; the game row is then marked overridden
    StringLine g;
    g.game = true;
    g.id = g.string_id = "INFO_FILLERX2_00002231";
    g.kind = "infos";
    g.text = {{"English", "That was good."}, {"German", "Das war gut."}, {"French", "C'était"}};
    CHECK(!set_line_text(p, g, "English", "x"));
    CHECK(!override_game_line(p, g, p.mod.languages).empty());
    CHECK(override_game_line(p, g, p.mod.languages).empty());
    CHECK(p.strings.back().text.size() == 2 && p.strings.back().text.at("German") == "Das war gut.");
    std::vector<StringLine> game = {g};
    mark_overridden(game, project_lines(p));
    CHECK(game[0].overridden);
}

// ---- ffmpeg ------------------------------------------------------------------------------------------------------------------------------------------

TEST(ffmpeg_command_line) {
    FfmpegInfo ff;
    ff.ok = true;
    ff.path = "C:/Program Files/ffmpeg/bin/ffmpeg.exe";
    ff.libvorbis = true;
    ConvertOptions o;
    auto a = ffmpeg_convert_args(ff, "in put.flac", "out/x.ogg", o);
    auto has = [&](const std::vector<std::string>& v, const std::vector<std::string>& seq) { return std::search(v.begin(), v.end(), seq.begin(), seq.end()) != v.end(); };
    CHECK(a.front() == ff.path && a.back() == "out/x.ogg");
    CHECK(has(a, {"-i", "in put.flac"}) && has(a, {"-ac", "1"}) && has(a, {"-ar", "44100"}) && has(a, {"-c:a", "libvorbis"}) && has(a, {"-nostdin"}));
    CHECK(!has(a, {"-af"}));
    o.normalize = true;
    o.mono = false;
    o.sample_rate = 0;
    a = ffmpeg_convert_args(ff, "a.m4a", "b.ogg", o);
    CHECK(has(a, {"-af", "loudnorm=I=-16.0:TP=-1.5:LRA=11"}) && !has(a, {"-ac", "1"}) && !has(a, {"-ar"}));
    ff.libvorbis = false;
    a = ffmpeg_convert_args(ff, "a.m4a", "b.wav", o);
    CHECK(has(a, {"-c:a", "pcm_s16le"}) && converted_ext(ff) == "wav");
    // the Windows quoting reads back unchanged
    CHECK_EQ(quote_windows_arg("plain"), std::string("plain"));
    CHECK_EQ(quote_windows_arg("C:/Program Files/x.exe"), std::string("\"C:/Program Files/x.exe\""));
    CHECK_EQ(quote_windows_arg("say \"hi\""), std::string("\"say \\\"hi\\\"\""));
    CHECK_EQ(quote_windows_arg("dir\\ with\\"), std::string("\"dir\\ with\\\\\""));
    CHECK_EQ(quote_windows_arg(""), std::string("\"\""));
    // PATH lookup
    auto exists = [](const std::string& f) { return f == "D:\\tools\\ffmpeg.exe" || f == "/usr/bin/ffmpeg"; };
    CHECK_EQ(find_in_path("ffmpeg", "C:\\Windows;\"D:\\tools\";E:\\x", exists, true), std::string("D:\\tools\\ffmpeg.exe"));
    CHECK_EQ(find_in_path("ffmpeg", "/usr/local/bin:/usr/bin", exists, false), std::string("/usr/bin/ffmpeg"));
    CHECK(find_in_path("ffmpeg", "/nowhere", exists, false).empty());
}

TEST(ffmpeg_probe_mocked) {
    std::vector<std::vector<std::string>> calls;
    ProcessRunner good = [&](const std::vector<std::string>& argv, const std::atomic<bool>*) {
        calls.push_back(argv);
        ProcessResult r;
        r.started = true;
        r.exit_code = 0;
        if (argv.back() == "-version") r.output = "ffmpeg version 6.1.1-3ubuntu5 Copyright (c) 2000-2023 the FFmpeg developers\nbuilt with gcc 13\n";
        else r.output = "Encoders:\n A....D libvorbis            libvorbis (codec vorbis)\n A....D libmp3lame\n";
        return r;
    };
    FfmpegInfo f = probe_ffmpeg("/usr/bin/ffmpeg", good);
    CHECK(f.ok && f.version == "6.1.1-3ubuntu5" && f.libvorbis && calls.size() == 2 && calls[0][0] == "/usr/bin/ffmpeg");
    ProcessRunner missing = [](const std::vector<std::string>&, const std::atomic<bool>*) {
        ProcessResult r;
        r.error = "cannot start ffmpeg (error 2)";
        return r;
    };
    f = probe_ffmpeg("ffmpeg", missing);
    CHECK(!f.ok && f.error.find("cannot start") != std::string::npos);
    CHECK(!probe_ffmpeg("", good).ok);
    ProcessRunner other = [](const std::vector<std::string>&, const std::atomic<bool>*) {
        ProcessResult r;
        r.started = true;
        r.exit_code = 0;
        r.output = "this is some other program\n";
        return r;
    };
    CHECK(!probe_ffmpeg("notffmpeg", other).ok);
    // a failing conversion reports ffmpeg's last lines; a cancelled one says so
    FfmpegInfo ok;
    ok.ok = true;
    ok.path = "ffmpeg";
    ok.libvorbis = true;
    const fs::path dir = temp_dir("ffmpeg_mock");
    ProcessRunner fails = [](const std::vector<std::string>&, const std::atomic<bool>*) {
        ProcessResult r;
        r.started = true;
        r.exit_code = 1;
        r.output = "line 1\nin.xyz: Invalid data found when processing input\n";
        return r;
    };
    const std::string e = convert_audio(ok, "in.xyz", u8str(dir / "o.ogg"), {}, fails, nullptr);
    CHECK(e.find("Invalid data") != std::string::npos && !fs::exists(dir / "o.ogg"));
    ProcessRunner cancelled = [](const std::vector<std::string>&, const std::atomic<bool>*) {
        ProcessResult r;
        r.started = true;
        r.cancelled = true;
        return r;
    };
    CHECK_EQ(convert_audio(ok, "in.xyz", u8str(dir / "o.ogg"), {}, cancelled, nullptr), std::string("cancelled"));
    // a "conversion" that writes something unplayable is refused
    ProcessRunner junk = [](const std::vector<std::string>& argv, const std::atomic<bool>*) {
        write_text(u8(argv.back()), "not audio");
        ProcessResult r;
        r.started = true;
        r.exit_code = 0;
        return r;
    };
    CHECK(convert_audio(ok, "in.xyz", u8str(dir / "o.ogg"), {}, junk, nullptr).find("does not play") != std::string::npos);
    // and one that writes a WAV is accepted and renamed into place
    ProcessRunner wav = [](const std::vector<std::string>& argv, const std::atomic<bool>*) {
        write_text(u8(argv.back()), test_wav(200));
        ProcessResult r;
        r.started = true;
        r.exit_code = 0;
        return r;
    };
    CHECK(convert_audio(ok, "in.xyz", u8str(dir / "o.ogg"), {}, wav, nullptr).empty() && fs::exists(dir / "o.ogg") && !fs::exists(dir / "o.ogg.part"));
    // plays_as_is: what the layer takes
    std::string why;
    CHECK(plays_as_is(test_wav(100), why));
    CHECK(!plays_as_is("fLaC\x00\x00\x00\x22", why));
}

// a real ffmpeg (skipped when there is none on PATH): a FLAC made from a WAV is converted to mono 44.1 kHz Ogg Vorbis that the layer's probe accepts
TEST(ffmpeg_real_conversion) {
    const std::string exe = locate_ffmpeg("");
    if (exe.empty()) {
        std::printf("  (skipped: no ffmpeg on PATH)\n");
        return;
    }
    const FfmpegInfo ff = probe_ffmpeg(exe, run_process);
    CHECK(ff.ok);
    if (!ff.ok) return;
    std::printf("  ffmpeg %s at %s, libvorbis %s\n", ff.version.c_str(), ff.path.c_str(), ff.libvorbis ? "yes" : "no");
    const fs::path dir = temp_dir("ffmpeg real");  // a space in the path
    write_text(dir / "tone.wav", test_wav(500));
    // make a FLAC (a format the game cannot play) with ffmpeg itself
    const ProcessResult mk = run_process({ff.path, "-hide_banner", "-nostdin", "-y", "-loglevel", "error", "-i", u8str(dir / "tone.wav"), u8str(dir / "tone.flac")});
    CHECK(mk.started && mk.exit_code == 0);
    std::string why;
    CHECK(!plays_as_is(read_text(dir / "tone.flac"), why));
    ConvertOptions o;
    o.normalize = true;
    const std::string err = convert_audio(ff, u8str(dir / "tone.flac"), u8str(dir / "out" / "tone.ogg"), o, run_process);
    if (!err.empty()) std::printf("  %s\n", err.c_str());
    CHECK(err.empty());
    const auto snd = open_sound(read_text(dir / "out" / "tone.ogg"), why);
    CHECK(snd.has_value());
    if (snd) {
        CHECK_EQ(snd->format, std::string(ff.libvorbis ? "ogg" : "wav"));
        CHECK(snd->channels == 1 && snd->sample_rate == 44100);
        CHECK(snd->duration_ms >= 450 && snd->duration_ms <= 560);
    }
    // the whole import with conversion
    Project p = voice_project();
    write_text(dir / "rec" / "vox_4444dddd_English.flac", read_text(dir / "tone.flac"));
    VoiceImportOptions io;
    VoicePreview pv = preview_voice_import(p, project_lines(p), u8str(dir / "rec"), "", "English", io, &ff);
    CHECK(pv.steps.size() == 1 && pv.steps[0].convert);
    std::atomic<bool> cancel{false};
    size_t progress_calls = 0;
    const auto done = run_voice_import(pv.steps, u8str(dir), io, &ff, run_process, [&](size_t, size_t, const std::string&) { ++progress_calls; }, &cancel);
    CHECK(done.size() == 1 && progress_calls >= 2 && fs::exists(dir / "voice" / ("vox_4444dddd_English." + converted_ext(ff))));
    // cancel kills a running ffmpeg: a long input (a 60 s silence made by the lavfi source) and the flag set at once
    std::atomic<bool> stop{true};
    const ProcessResult c = run_process({ff.path, "-hide_banner", "-nostdin", "-y", "-f", "lavfi", "-i", "anullsrc=d=600", "-f", "null", "-"}, &stop);
    CHECK(c.started && c.cancelled);
}

// the game's rows (needs --data): every string table, the spoken infos with speaker, voice, info, quest, stage directions and the shipped speech files
TEST(voice_game_rows) {
    const auto g = game_data();
    if (!g) return;
    const auto t0 = std::chrono::steady_clock::now();
    const auto rows = game_lines(*g);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    size_t spoken = 0, infos = 0, shipped_en = 0;
    for (auto& l : rows) {
        spoken += l.spoken;
        infos += l.kind == "infos";
        if (auto it = l.shipped.find("English"); it != l.shipped.end() && it->second) ++shipped_en;
    }
    std::printf("  %zu rows (%zu infos, %zu spoken, %zu with English speech) in %.0f ms\n", rows.size(), infos, spoken, shipped_en, ms);
    CHECK(infos > 14000 && spoken > infos && shipped_en > 10000);
    const StringLine* l = line_by_id(rows, "INFO_FILLERX2_00002231");
    CHECK(l != nullptr);
    if (!l) return;
    CHECK(l->spoken && l->speaker == "Tucker" && l->voice == "Fincher" && l->object == "FILLERX2_00111" && l->quest == "Tucker_ArenaFight");
    CHECK_EQ(l->audio.at("English"), std::string("INFO_FILLERX2_00002231_Fincher_English_%_Tucker.wav"));
    CHECK(l->shipped.at("English") && l->text.at("German").find("Das war gut") == 0);
    Project p;
    CHECK(audio_status(p, *l, "English") == AudioStatus::Recorded);
    CHECK(audio_status(p, *l, "Polish") == AudioStatus::None);  // the game has no Polish voice
    const StringLine* svm = line_by_id(rows, "SVM_Abrax_Attack00");
    CHECK(svm && svm->spoken && svm->voice == "Abrax" && svm->stage.at("English") == "Swinging a sword");
}
