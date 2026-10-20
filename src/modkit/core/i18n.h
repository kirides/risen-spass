// Localisation of the editor's own texts (not the texts of a mod project: those are Project::strings).
//
// English is embedded: the English text is the lookup key and the fallback, `tr("Open project...")` returns it unchanged when no
// language file is active or the file lacks the entry. Other languages are plain TOML files (`lang/<code>.toml`, see
// docs/editor/LOCALIZATION.md):
//
//   [meta]
//   code = "de"
//   name = "Deutsch"
//   [strings]
//   "Open project..." = "Projekt öffnen..."
//   "menu|Open" = "Öffnen"            # trc("menu", "Open"): the same English word with different meanings
//
// ImGui labels keep their id part: tr("Save##project") translates "Save" and returns "Speichern##project".
// Texts with values use {0}, {1} placeholders (trf), so a translation may reorder them; printf-style strings keep their
// specifiers in any order the translation needs only if it keeps the same ones (tools/i18n.py --check verifies this).
#pragma once

#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

#include "core/paths.h"

namespace mk::i18n {

struct LanguageInfo {
    std::string code;          // "de"
    std::string name;          // "Deutsch" (the language's own name)
    std::string english_name;  // "German"
    std::string path;          // the file
};

// Directories searched for `*.toml` language files: next to the executable (`lang/`), then the per-user config directory
// (`risen-modkit/lang/`, community translations; a file there replaces the shipped one with the same code).
std::vector<fs::path> default_search_dirs();

// Reads the [meta] table of every language file in `dirs`; broken files are logged and skipped. Sorted by code.
std::vector<LanguageInfo> discover(const std::vector<fs::path>& dirs);

// Activates a language: "" or "en" = the embedded English, "auto" = the system language when a file exists for it,
// otherwise a code found in `dirs`. Returns false (English stays active) when the file is missing or broken; `error` says why.
bool set_language(const std::string& code, const std::vector<fs::path>& dirs, std::string* error = nullptr);
// The same for text already read (tests, embedding)
bool set_language_from_toml(std::string_view toml_text, std::string* error = nullptr);

const std::string& language();                 // the active code, "en" for English
std::string system_language();                 // "de" for a German system ("en" when unknown)
size_t entry_count();                          // translated entries of the active language

// The lookup. All return `english` itself (same pointer) when there is nothing to translate; the pointer of a translation stays valid
// until the language changes (switch it between frames).
const char* tr(const char* english);
const char* trc(const char* context, const char* english);
std::string tr(const std::string& english);
// tr + {0} {1} ... replaced by the arguments
std::string trf(const char* english, std::initializer_list<std::string_view> args);
std::string format(std::string_view text, std::initializer_list<std::string_view> args);

// A parsed language file, for tools and tests: key -> translation as written
struct Catalog {
    LanguageInfo info;
    std::vector<std::pair<std::string, std::string>> entries;
};
bool parse_catalog(std::string_view toml_text, Catalog& out, std::string* error);

}  // namespace mk::i18n

// Marks a literal for extraction without translating it where it stands (static tables: `static const char* kModes[] = {N_("a"), N_("b")}`);
// the table is translated where it is shown: tr(kModes[i]).
#define N_(text) text

namespace mk {
using i18n::tr;
using i18n::trc;
using i18n::trf;
}  // namespace mk
