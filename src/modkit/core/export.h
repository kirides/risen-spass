// Export of a project as a self-contained mod of the layer (docs/editor/REQUIREMENTS.md section 7). render_mod() is the pure part (relative path ->
// file content, deterministic); export_mod() checks the target folder, writes what changed, copies the asset files and removes stale generated files.
#pragma once

#include <map>
#include <string>
#include <vector>

#include "core/project.h"

namespace mk {

class GameData;

constexpr int kRuntimeVersion = 1;

std::map<std::string, std::string> render_mod(const Project& p);
// the data tables of the export as JSON (the same content as data/*.lua; the importer and the tests use it)
json balance_spec(const Project& p);
json npcs_data(const Project& p);  // data/npcs.lua: the persistent NPC declarations
json quests_data(const Project& p);  // data/quests.lua: the quests (risen.quest.define) and their flow
json world_data(const Project& p);   // data/world.lua: things spawned into the world and items added to containers, once per savegame
json behaviors_data(const Project& p);  // data/behaviors.lua: the behaviour scripts (modules) and their attachments to NPCs and templates
// the file of a behaviour inside the exported mod: "behaviors/<name>.lua" (dots and dashes become _, require() reads dots as folders)
std::string behavior_module_file(const std::string& name);

struct ExportOptions {
    std::string project_dir;     // where the project file is: asset files are relative to it
    std::string game_data_dir;   // the export refuses targets inside it
    bool allow_foreign = false;  // write into a non-empty folder without modkit/project.json (never set by the UI)
    const GameData* game = nullptr;  // the game sounds dialogue lines use are unpacked from it (an error names them when it is missing)
};

struct ExportResult {
    bool ok = false;
    std::vector<std::string> written, unchanged, removed;
    std::vector<std::string> errors;
};

ExportResult export_mod(const Project& p, const std::string& out_dir, const ExportOptions& opts);

std::string runtime_lua();  // the embedded modkit/runtime.lua
std::string events_lua();   // the embedded modkit/events.lua (exported with the event graphs)

}  // namespace mk
