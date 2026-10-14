#include "game/loc_files.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

#include <spdlog/spdlog.h>

#include "game/loc_overlay.h"
#include "lua/runtime.h"

namespace spass::game {

std::vector<StringRow> parseStringCsv(std::string_view text) {
    if (text.size() >= 3 && text.substr(0, 3) == "\xEF\xBB\xBF") {
        text.remove_prefix(3);
    }
    // Delimiter: every data line votes for the first ';' or ',' outside quotes; ';' wins ties.
    char delimiter = ',';
    {
        int semicolons = 0, commas = 0;
        std::size_t pos = 0;
        while (pos < text.size()) {
            std::size_t eol = text.find('\n', pos);
            if (eol == std::string_view::npos) {
                eol = text.size();
            }
            const std::string_view line = text.substr(pos, eol - pos);
            pos = eol + 1;
            const std::size_t first = line.find_first_not_of(" \t\r");
            if (first == std::string_view::npos || line[first] == '#') {
                continue;
            }
            bool quoted = false;
            for (const char c : line) {
                if (c == '"') {
                    quoted = !quoted;
                } else if (!quoted && (c == ';' || c == ',')) {
                    (c == ';' ? semicolons : commas) += 1;
                    break;
                }
            }
        }
        delimiter = semicolons >= commas && semicolons > 0 ? ';' : ',';
    }

    std::vector<std::vector<std::string>> records;
    std::vector<std::string> fields;
    std::string field;
    bool quoted = false, fieldWasQuoted = false, any = false;
    const auto endField = [&] {
        fields.push_back(std::move(field));
        field.clear();
        fieldWasQuoted = false;
    };
    const auto endRecord = [&] {
        if (any || !fields.empty()) {
            endField();
            records.push_back(std::move(fields));
        }
        fields.clear();
        any = false;
    };
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (quoted) {
            if (c == '"') {
                if (i + 1 < text.size() && text[i + 1] == '"') {
                    field += '"';
                    ++i;
                } else {
                    quoted = false;
                }
            } else {
                field += c;
            }
            continue;
        }
        if (c == '"' && field.empty() && !fieldWasQuoted) {
            quoted = true;
            fieldWasQuoted = true;
            any = true;
        } else if (c == delimiter) {
            endField();
            any = true;
        } else if (c == '\n') {
            endRecord();
        } else if (c == '\r') {
            // ignored: CRLF files
        } else {
            // a '#' as the very first character of a record starts a comment line
            if (c == '#' && field.empty() && fields.empty() && !fieldWasQuoted) {
                while (i < text.size() && text[i] != '\n') {
                    ++i;
                }
                continue;
            }
            field += c;
            any = true;
        }
    }
    endRecord();

    std::vector<StringRow> rows;
    for (std::size_t n = 0; n < records.size(); ++n) {
        const auto& r = records[n];
        if (r.size() < 2 || r[0].empty()) {
            continue;
        }
        std::string id = r[0];
        id.erase(0, id.find_first_not_of(" \t"));
        id.erase(id.find_last_not_of(" \t") + 1);
        std::string lower = id;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        if (n == 0 && lower == "id") {
            continue;  // header row
        }
        rows.push_back(StringRow{id, r[1]});
    }
    return rows;
}

namespace {

std::string readAll(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// Loads a file into the overlay; returns the number of strings (or -1 if it cannot be read).
int loadFile(const std::filesystem::path& file, const std::string& language) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(file, ec)) {
        return -1;
    }
    int n = 0;
    for (const StringRow& row : parseStringCsv(readAll(file))) {
        if (setOverlayString(row.id, row.text, language)) {
            ++n;
        }
    }
    return n;
}

}  // namespace

void bindModStrings() {
    lua::Runtime::instance().addModBinding([](sol::state_view lua, sol::table api, const lua::ModInfo& mod) {
        // strings/all.csv first (every language), then strings/<Language>.csv, so a language-specific text wins.
        const std::filesystem::path dir = mod.dir / "strings";
        std::error_code ec;
        if (std::filesystem::is_directory(dir, ec)) {
            if (const int n = loadFile(dir / "all.csv", ""); n >= 0) {
                spdlog::info("loc: [{}] strings/all.csv: {} string(s)", mod.id, n);
            }
            std::vector<std::filesystem::path> files;
            for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
                if (e.is_regular_file() && e.path().extension() == ".csv" && e.path().stem() != "all") {
                    files.push_back(e.path());
                }
            }
            std::sort(files.begin(), files.end());
            for (const auto& f : files) {
                const int n = loadFile(f, f.stem().string());
                spdlog::info("loc: [{}] strings/{}: {} string(s) for language '{}'", mod.id, f.filename().string(), n,
                             f.stem().string());
            }
        }

        // risen.loc.load_file("extra/more.csv" [, "German"]): a file inside the mod directory.
        sol::table loc = lua.create_table();
        sol::table meta = lua.create_table();
        sol::table shared = api["loc"];  // the shared risen.loc table through the api's __index chain
        meta["__index"] = shared;
        loc[sol::metatable_key] = meta;
        loc.set_function("load_file", [root = mod.dir](sol::this_state ts, const std::string& rel,
                                                       sol::optional<std::string> language) -> sol::object {
            sol::state_view L(ts);
            if (rel.empty() || rel.find("..") != std::string::npos || rel.find(':') != std::string::npos ||
                (rel[0] == '/' || rel[0] == '\\')) {
                throw sol::error("risen.loc.load_file: the path must be relative to the mod directory");
            }
            const int n = loadFile(root / rel, language.value_or(""));
            if (n < 0) {
                return sol::make_object(L, sol::lua_nil);
            }
            return sol::make_object(L, n);
        });
        api["loc"] = loc;
    });
}

}  // namespace spass::game
