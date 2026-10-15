// ---- custom voice lines (common/sound_file.h): format probing, the compiled container, language names ------------------------------------------
// Included by test_main.cpp inside its anonymous namespace.

namespace sndtest {

inline void put16(std::string& s, unsigned v) { s.push_back(char(v & 0xFF)); s.push_back(char((v >> 8) & 0xFF)); }
inline void put32(std::string& s, std::uint32_t v) { for (int i = 0; i < 4; ++i) s.push_back(char((v >> (8 * i)) & 0xFF)); }

// A RIFF/WAVE with `frames` frames of silence.
inline std::string wav(int tag, int channels, int rate, int bits, std::uint32_t frames, bool extensible = false) {
    const std::uint32_t blockAlign = static_cast<std::uint32_t>(channels * bits / 8);
    const std::uint32_t dataBytes = frames * blockAlign;
    std::string s = "RIFF";
    put32(s, 4 + 8 + (extensible ? 40 : 16) + 8 + dataBytes);
    s += "WAVEfmt ";
    put32(s, extensible ? 40 : 16);
    put16(s, extensible ? 0xFFFE : tag);
    put16(s, channels);
    put32(s, rate);
    put32(s, rate * blockAlign);
    put16(s, blockAlign);
    put16(s, bits);
    if (extensible) {
        put16(s, 22);
        put16(s, bits);
        put32(s, 4);
        put16(s, tag);  // the first two bytes of the sub-format GUID
        s.append(14, '\0');
    }
    s += "data";
    put32(s, dataBytes);
    s.append(dataBytes, '\0');
    return s;
}

// A minimal Ogg Vorbis stream: an identification page and a final page that carries the granule position (the length in samples).
inline std::string ogg(int channels, int rate, std::uint64_t granule, const char* head = "\x01vorbis") {
    std::string s;
    const auto page = [&](unsigned type, std::uint64_t gp, const std::string& packet) {
        s += "OggS";
        s.push_back(0);
        s.push_back(char(type));
        for (int i = 0; i < 8; ++i) s.push_back(char((gp >> (8 * i)) & 0xFF));
        put32(s, 1);
        put32(s, 0);
        put32(s, 0);
        s.push_back(1);
        s.push_back(char(packet.size()));
        s += packet;
    };
    std::string id(head, 7);
    if (id[0] == 'O') {
        id += "d";  // "OpusHead"
    }
    put32(id, 0);
    id.push_back(char(channels));
    put32(id, static_cast<std::uint32_t>(rate));
    id.append(12, '\0');
    page(2, 0, id);
    page(4, granule, std::string(10, 'x'));
    return s;
}

// MPEG-1 layer III, 128 kbit/s, 44.1 kHz, stereo: 417-byte frames of 1152 samples.
inline std::string mp3(int frames, bool id3 = false) {
    std::string s;
    if (id3) {
        s += "ID3";
        s.push_back(3);
        s.push_back(0);
        s.push_back(0);
        s.push_back(0);
        s.push_back(0);
        s.push_back(0);
        s.push_back(10);  // syncsafe size 10
        s.append(10, '\0');
    }
    for (int i = 0; i < frames; ++i) {
        s.push_back(static_cast<char>(0xFF));
        s.push_back(static_cast<char>(0xFB));
        s.push_back(static_cast<char>(0x90));
        s.push_back(char(0x00));
        s.append(417 - 4, '\0');
    }
    return s;
}

}  // namespace sndtest

TEST(sound_probe_wav) {
    using namespace spass::sound;
    std::string why;
    // 1.16 s float32 mono 22.05 kHz: the sample the feature was built with
    const auto f = probe(sndtest::wav(3, 1, 22050, 32, 25600), why);
    CHECK(f && f->format == "wav" && f->channels == 1 && f->sampleRate == 22050 && f->bitsPerSample == 32);
    CHECK(f && f->durationMs == 1161);
    const auto p = probe(sndtest::wav(1, 2, 44100, 16, 44100), why);
    CHECK(p && p->durationMs == 1000 && p->channels == 2);
    const auto x = probe(sndtest::wav(3, 1, 48000, 32, 24000, true), why);  // WAVE_FORMAT_EXTENSIBLE with an IEEE float sub-format
    CHECK(x && x->durationMs == 500);
    CHECK(!probe(sndtest::wav(0x55, 1, 22050, 16, 1000), why));  // MP3-in-WAV is not supported
    CHECK(why.find("unsupported WAV") != std::string::npos);
    CHECK(!probe(sndtest::wav(1, 1, 22050, 12, 1000), why));
    CHECK(!probe(sndtest::wav(1, 1, 22050, 16, 0), why));  // no audio data
}

TEST(sound_probe_ogg) {
    using namespace spass::sound;
    std::string why;
    const auto o = probe(sndtest::ogg(1, 22050, 25600), why);
    CHECK(o && o->format == "ogg" && o->channels == 1 && o->sampleRate == 22050 && o->durationMs == 1161);
    const auto st = probe(sndtest::ogg(2, 44100, 441000), why);
    CHECK(st && st->durationMs == 10000 && st->channels == 2);
    CHECK(!probe(sndtest::ogg(1, 48000, 48000, "OpusHea"), why));
    CHECK(why.find("Opus") != std::string::npos);
    CHECK(!probe(sndtest::ogg(1, 22050, 0), why));  // no length
    CHECK(!probe(sndtest::ogg(1, 22050, 100).substr(0, 30), why));
}

TEST(sound_probe_mp3) {
    using namespace spass::sound;
    std::string why;
    const auto m = probe(sndtest::mp3(100), why);
    CHECK(m && m->format == "mp3" && m->channels == 2 && m->sampleRate == 44100);
    CHECK(m && m->durationMs == 2612);  // 100 * 1152 / 44100
    const auto t = probe(sndtest::mp3(10, true), why);
    CHECK(t && t->durationMs == 261);
    CHECK(!probe(std::string(100, '\xFF'), why));  // sync bytes, no valid frame
    CHECK(!probe(std::string(100, 'a'), why));
    CHECK(why.find("unsupported audio format") != std::string::npos);
    CHECK(!probe(std::string("GR01SN04") + std::string(100, '\0'), why));
    CHECK(why.find("already a compiled") != std::string::npos);
    CHECK(!probe("", why));
}

TEST(sound_xsnd_container) {
    using namespace spass::sound;
    const std::string audio = sndtest::ogg(1, 22050, 25600) + std::string(500, 'z');
    const std::string x = buildXsnd(audio, 1161, 0x01D830B38AC56280ULL);
    CHECK(x.size() == 0x28 + 151 + audio.size());
    CHECK(x.substr(0, 8) == std::string("GR01SN04"));
    const auto u32 = [&](std::size_t off) { return spass::sound::detail::le32(reinterpret_cast<const unsigned char*>(x.data()) + off); };
    CHECK(u32(8) == 0x28 && u32(12) == 151 && u32(16) == 0x28 + 151 && u32(20) == audio.size());
    CHECK(x.substr(0x28 + 151) == audio);
    CHECK(x.find("eCSoundResource2") != std::string::npos && x.find("Duration") != std::string::npos);
    CHECK(xsndDurationMs(x) == 1161);
    CHECK(xsndDurationMs("nope") == 0);
    CHECK(buildXsnd(audio, 1161, 0x01D830B38AC56280ULL) == x);  // deterministic: the same audio never rewrites a file
    CHECK(buildXsnd(audio, 2000, 0x01D830B38AC56280ULL) != x);
    CHECK(xsndDurationMs(buildXsnd(audio, 4294967295ULL, 1)) == 4294967295ULL);
}

TEST(sound_languages_and_names) {
    using namespace spass::sound;
    CHECK(normalizeLanguage("de") == "German" && normalizeLanguage("DE") == "German" && normalizeLanguage("german") == "German");
    CHECK(normalizeLanguage("en") == "English" && normalizeLanguage("English") == "English");
    CHECK(normalizeLanguage("fr") == "French" && normalizeLanguage("ru") == "Russian" && normalizeLanguage("pl") == "Polish");
    CHECK(normalizeLanguage("klingon") == "Klingon");
    CHECK(normalizeLanguage("").empty());
    CHECK(safeName("My Key/1:x") == "My_Key_1_x");
    CHECK(voiceBaseName("my_mod", "Q_1", "German") == "INFO_SPASS_my_mod_Q_1_German_%_SPASS");  // '%' stays: shipped speech has it
    std::map<std::string, std::string> v{{"German", "g"}, {"default", "d"}, {"French", "f"}};
    CHECK(pickLanguage(v, "German") == "German");
    CHECK(pickLanguage(v, "Russian") == "default");  // not available: the default entry
    CHECK(pickLanguage(v, "") == "default");
    v.erase("default");
    CHECK(pickLanguage(v, "Russian") == "French");  // no default and no English: the first by name
    v["English"] = "e";
    CHECK(pickLanguage(v, "Russian") == "English");  // English is the fallback of last resort
    CHECK(pickLanguage({}, "German").empty());
    CHECK(spass::assets::kindOf("x.wav") == spass::assets::Kind::Sound && spass::assets::kindOf("x._xsnd") == spass::assets::Kind::Sound);
    CHECK(spass::assets::compiledName("a_%_b.wav") == std::string("a_%_b._xsnd"));
    CHECK(spass::assets::compiledName("x.ogg") == std::string("x._xsnd"));
}
