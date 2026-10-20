// Localisation of the editor (core/i18n.h): the embedded English, the TOML language files, the lookup rules (id suffix, context, placeholders),
// language discovery and the shipped German file.

static const char* kI18nSample = R"(
[meta]
code = "xx"
name = "Testish"
english_name = "Test"

[strings]
"Save" = "Sichern"
"Open project..." = "Projekt öffnen..."
"menu|Open" = "Öffnen"
"Open" = "Offen"
"{0} files written, {1} unchanged" = "{1} unverändert, {0} Dateien geschrieben"
"Untranslated yet" = ""
"two\nlines" = "zwei\nZeilen"
)";

struct I18nReset {
    ~I18nReset() { mk::i18n::set_language("en", {}); }
};

TEST(i18n_english_is_embedded) {
    I18nReset reset;
    mk::i18n::set_language("en", {});
    const char* s = "Save";
    CHECK(mk::tr(s) == s);  // the very same pointer: nothing is looked up or copied
    CHECK_EQ(mk::i18n::language(), std::string("en"));
    CHECK_EQ(mk::i18n::entry_count(), size_t(0));
    CHECK_EQ(mk::trf("{0} files written, {1} unchanged", {"3", "4"}), std::string("3 files written, 4 unchanged"));
    CHECK_EQ(mk::i18n::format("{{literal}} {0}{2}", {"a", "b"}), std::string("{literal} a"));  // an argument that is not there becomes nothing
    CHECK(mk::tr(nullptr) == nullptr);
    CHECK_EQ(std::string(mk::tr("")), std::string());
}

TEST(i18n_translation_lookup) {
    I18nReset reset;
    std::string err;
    CHECK(mk::i18n::set_language_from_toml(kI18nSample, &err));
    CHECK_EQ(mk::i18n::language(), std::string("xx"));
    CHECK_EQ(std::string(mk::tr("Save")), std::string("Sichern"));
    CHECK_EQ(std::string(mk::tr("Open project...")), std::string("Projekt öffnen..."));
    CHECK_EQ(std::string(mk::tr("not in the file")), std::string("not in the file"));
    // an empty value is "not translated yet": English stays
    CHECK_EQ(std::string(mk::tr("Untranslated yet")), std::string("Untranslated yet"));
    CHECK_EQ(mk::i18n::entry_count(), size_t(6));
    CHECK_EQ(std::string(mk::tr("two\nlines")), std::string("zwei\nZeilen"));
    // the ImGui id part is kept: "Save##project" -> "Sichern##project", "Save###id" likewise
    CHECK_EQ(std::string(mk::tr("Save##project")), std::string("Sichern##project"));
    CHECK_EQ(std::string(mk::tr("Save###id")), std::string("Sichern###id"));
    CHECK_EQ(std::string(mk::tr("##only_id")), std::string("##only_id"));
    CHECK_EQ(std::string(mk::tr("Unknown##x")), std::string("Unknown##x"));
    // a stable pointer for the same text within one language
    CHECK(mk::tr("Save##project") == mk::tr("Save##project"));
    // the context: the same English word with two meanings
    CHECK_EQ(std::string(mk::trc("menu", "Open")), std::string("Öffnen"));
    CHECK_EQ(std::string(mk::tr("Open")), std::string("Offen"));
    CHECK_EQ(std::string(mk::trc("other", "Open")), std::string("Offen"));  // no entry for that context: the plain text
    CHECK_EQ(std::string(mk::trc("other", "Save")), std::string("Sichern"));
    // placeholders may be reordered by a translation
    CHECK_EQ(mk::trf("{0} files written, {1} unchanged", {"3", "4"}), std::string("4 unverändert, 3 Dateien geschrieben"));
    // back to English
    CHECK(mk::i18n::set_language("en", {}));
    CHECK_EQ(std::string(mk::tr("Save")), std::string("Save"));
}

TEST(i18n_broken_file_keeps_english) {
    I18nReset reset;
    std::string err;
    CHECK(mk::i18n::set_language_from_toml(kI18nSample, &err));
    CHECK(!mk::i18n::set_language_from_toml("[meta\ncode = ", &err));
    CHECK(!err.empty());
    CHECK_EQ(mk::i18n::language(), std::string("en"));
    CHECK_EQ(std::string(mk::tr("Save")), std::string("Save"));
    CHECK(!mk::i18n::set_language_from_toml("[strings]\n\"a\" = 3\n", &err));  // a value that is no text
    CHECK(!mk::i18n::set_language_from_toml("[strings]\n\"a\" = \"b\"\n", &err));  // no [meta] code
}

TEST(i18n_discover_and_select) {
    I18nReset reset;
    const fs::path a = temp_dir("i18n_a"), b = temp_dir("i18n_b");
    write_text(a / "de.toml", "[meta]\ncode = \"de\"\nname = \"Deutsch\"\n[strings]\n\"Save\" = \"Speichern\"\n");
    write_text(a / "pt-br.toml", "[meta]\ncode = \"pt-BR\"\nname = \"Português (Brasil)\"\n[strings]\n\"Save\" = \"Salvar\"\n");
    write_text(a / "broken.toml", "[meta\n");
    write_text(a / "template.toml", "[meta]\ncode = \"\"\n[strings]\n\"Save\" = \"\"\n");  // the template has no code: not a language
    write_text(a / "notes.txt", "not a language file");
    write_text(b / "de.toml", "[meta]\ncode = \"de\"\nname = \"Deutsch (Gemeinschaft)\"\n[strings]\n\"Save\" = \"Sichern\"\n");

    auto langs = mk::i18n::discover({a});
    CHECK_EQ(langs.size(), size_t(2));
    CHECK_EQ(langs[0].code, std::string("de"));
    CHECK_EQ(langs[1].code, std::string("pt-br"));  // codes are compared case-insensitively
    CHECK_EQ(langs[1].name, std::string("Português (Brasil)"));

    // a later directory (the user's) replaces the shipped file with the same code
    langs = mk::i18n::discover({a, b});
    CHECK_EQ(langs.size(), size_t(2));
    CHECK_EQ(langs[0].name, std::string("Deutsch (Gemeinschaft)"));

    CHECK(mk::i18n::set_language("de", {a}));
    CHECK_EQ(std::string(mk::tr("Save")), std::string("Speichern"));
    CHECK(mk::i18n::set_language("de", {a, b}));
    CHECK_EQ(std::string(mk::tr("Save")), std::string("Sichern"));
    CHECK(mk::i18n::set_language("DE-at", {a}));  // region falls back to the language
    CHECK_EQ(std::string(mk::tr("Save")), std::string("Speichern"));
    CHECK(mk::i18n::set_language("pt-BR", {a}));
    CHECK_EQ(std::string(mk::tr("Save")), std::string("Salvar"));
    std::string err;
    CHECK(!mk::i18n::set_language("fr", {a}, &err));
    CHECK(!err.empty());
    CHECK_EQ(mk::i18n::language(), std::string("en"));  // an unknown language leaves English
    CHECK_EQ(std::string(mk::tr("Save")), std::string("Save"));
    CHECK(mk::i18n::set_language("", {a}));
    CHECK(mk::i18n::set_language("en-GB", {a}));
    CHECK_EQ(mk::i18n::language(), std::string("en"));
}

// the printf specifiers and {n} placeholders of a text, sorted: a translation must keep exactly these
static std::string i18n_signature(const std::string& s) {
    std::vector<std::string> parts;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%') {
            if (i + 1 < s.size() && s[i + 1] == '%') {
                ++i;
                continue;
            }
            size_t j = i + 1;
            while (j < s.size() && std::strchr("-+#0123456789.", s[j])) ++j;
            while (j < s.size() && std::strchr("hlzjtL", s[j])) ++j;
            if (j < s.size() && std::strchr("diouxXeEfFgGaAcsp", s[j])) {
                parts.push_back(s.substr(i, j - i + 1));
                i = j;
            }
        } else if (s[i] == '{' && i + 1 < s.size() && std::isdigit((unsigned char)s[i + 1])) {
            const size_t close = s.find('}', i);
            if (close != std::string::npos) parts.push_back(s.substr(i, close - i + 1));
        }
    }
    std::sort(parts.begin(), parts.end());
    std::string out;
    for (auto& p : parts) out += p + "\n";
    return out;
}

TEST(i18n_shipped_language_files) {
    const fs::path dir = fs::path(MODKIT_SOURCE_DIR) / "src" / "modkit" / "lang";
    int files = 0;
    for (const auto& e : fs::directory_iterator(dir)) {
        if (e.path().extension() != ".toml") continue;
        ++files;
        mk::i18n::Catalog c;
        std::string err;
        CHECK(mk::i18n::parse_catalog(read_text(e.path()), c, &err));
        if (e.path().filename() == "template.toml") {
            CHECK(c.info.code.empty());
            CHECK(c.entries.size() > 500);
            continue;
        }
        CHECK(!c.info.code.empty());
        CHECK(!c.info.name.empty());
        CHECK(c.entries.size() > 500);
        int bad = 0, empty = 0;
        for (const auto& [k, v] : c.entries) {
            if (v.empty()) {
                ++empty;
                continue;
            }
            if (i18n_signature(k) != i18n_signature(v)) {
                ++bad;
                std::printf("  %s: placeholders differ: %s -> %s\n", e.path().filename().string().c_str(), k.c_str(), v.c_str());
            }
        }
        CHECK_EQ(bad, 0);
        CHECK(empty < (int)c.entries.size() / 10);  // a shipped language is (nearly) complete; tools/i18n.py check lists the gaps
    }
    CHECK(files >= 2);  // template.toml and de.toml
}

TEST(i18n_german) {
    I18nReset reset;
    const fs::path dir = fs::path(MODKIT_SOURCE_DIR) / "src" / "modkit" / "lang";
    std::string err;
    CHECK(mk::i18n::set_language("de", {dir}, &err));
    CHECK_EQ(mk::i18n::language(), std::string("de"));
    CHECK_EQ(std::string(mk::tr("Save")), std::string("Speichern"));
    CHECK_EQ(std::string(mk::tr("Settings")), std::string("Einstellungen"));
    CHECK_EQ(std::string(mk::tr("Save##project")), std::string("Speichern##project"));
    CHECK_EQ(std::string(mk::tr("Open project...")), std::string("Projekt öffnen..."));
    CHECK_EQ(mk::trf("Saved {0}", {"a.rmk.json"}), std::string("a.rmk.json gespeichert"));
    CHECK_EQ(std::string(mk::tr(kind_name(TplKind::Monster))), std::string("Monster"));
    CHECK_EQ(std::string(kind_label(TplKind::Item)), std::string("Gegenstand"));
    CHECK_EQ(std::string(kind_name(TplKind::Item)), std::string("Item"));  // the logic name stays English
}
