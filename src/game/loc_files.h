#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace spass::game {

// One row of a mod string file.
struct StringRow {
    std::string id;
    std::string text;
};

// Parses a mod string file (UTF-8, optional BOM): `id;text` per line, `,` is accepted as the delimiter when the first
// data line has no `;`. Fields may be quoted (`"` escaped by doubling, may span lines). Blank lines, `#` comment lines
// and a header row whose first field is `id` are skipped. Pure function: no engine access, unit-testable.
std::vector<StringRow> parseStringCsv(std::string_view text);

// Registers the mod bindings: before a mod's main.lua runs, `<mod>/strings/all.csv` and `<mod>/strings/<Language>.csv`
// (e.g. German.csv) are loaded into the localization overlay, and `risen.loc.load_file(path [, language])` loads
// further files relative to the mod directory.
void bindModStrings();

}  // namespace spass::game
