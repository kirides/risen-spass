// Import of an exported mod (docs/editor/REQUIREMENTS.md IMP-1, IMP-2): the project file the export put into modkit/project.json, or, without it
// (or on request), the project rebuilt from the mod's data/*.lua and mod.toml. The data files are plain tables: they are run in an empty LuaJIT state
// (no libraries, an instruction limit) and turned back into the model; dialogue and quest graphs get a fresh layout. Balance sections the editor has
// no panel for (spells, new spells, ...) are kept in Project::extra_balance and exported again unchanged.
#pragma once

#include <string>
#include <vector>

#include "core/project.h"

namespace mk {

struct ImportResult {
    bool ok = false;
    bool from_project_file = false;
    Project project;
    std::vector<std::string> warnings;  // what did not map (and was kept or dropped)
    std::string error;
};

ImportResult import_mod(const std::string& dir, bool prefer_data = false);

// a Lua chunk that returns a table, as JSON (lists become arrays, other tables objects); throws std::runtime_error
json lua_data_to_json(const std::string& chunk, const std::string& name);

}  // namespace mk
