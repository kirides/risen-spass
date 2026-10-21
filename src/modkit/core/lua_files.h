// Hand-written Lua of a project (Project::lua_files, the Lua panel): files next to the project that the export copies into lua/ of the mod.
// main.lua loads them protected before the content is installed (a file that fails is logged with the mod's [id] and skipped, the content stays) and
// calls the hooks they return: before_install(data) may change the generated data tables, after_install(runtime) runs once the content is installed.
// custom.lua (the mod folder's own file, never in the project) is loaded the same way, last.
#pragma once

#include <string>
#include <vector>

#include "core/project.h"

namespace mk {

struct LuaFileInfo {
    bool ok = false;
    std::string error;                 // the syntax error (with the file name and line) or why the file cannot be read
    std::vector<std::string> hooks;    // before_install / after_install, as far as the text shows them
    std::vector<std::string> scripts;  // names given to risen.script.define("Name", ...) in the text (Run script nodes and dialogue Script commands call them)
};

// compiles the code without running it (LuaJIT's parser) and reads hooks and script names from the text
LuaFileInfo read_lua_file_code(const std::string& code, const std::string& chunk_name);
// the same for a file; cached by path, size and modification time (thread-safe)
LuaFileInfo lua_file_info(const std::string& path);

// a Lua identifier (letters, digits, _; not starting with a digit), at most 64 characters, not "custom" ("" = fine)
std::string lua_file_name_problem(const std::string& name);
std::string lua_module_file(const std::string& name);  // "lua/<name>.lua" in the mod
std::string lua_module_name(const std::string& name);  // "lua.<name>" for require
// a commented starter file for "New Lua file"
std::string lua_file_template(const std::string& name, const std::string& mod_id);

}  // namespace mk
