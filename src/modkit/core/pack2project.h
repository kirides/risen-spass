// risen-pack2mod --project (docs/PACK2MOD.md "Editor project"): the converter's results as a risen-modkit project. The mapping lives here, in the editor's
// core, because it needs the project model; risen-pack2mod reaches it through p2m::set_project_writer (src/pack2mod/convert.h), so a layer-only build
// of the converter stays free of the editor.
#pragma once

#include <iosfwd>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "core/gamedata.h"
#include "core/genome.h"
#include "core/project.h"
#include "pack2mod/convert.h"

namespace mk {

// what the project cannot hold and where it went instead
struct Pack2ProjectReport {
    struct Entry {
        std::string area, name, reason;
    };
    std::vector<Entry> unmapped;
    std::map<std::string, int> counts;  // "items changed", "recipes new", ... (what ended up in the project)
    std::vector<std::string> notes;
    // the left-overs in the converter's own data format (JSON here; written as unmapped/*.lua and unmapped/strings/*.csv)
    json leftover_patches = json::object();   // template -> { Set = { Property = value } }
    json leftover_dialog = json::object();    // the converter's dialog.lua (XP rewards and trader refills on shipped infos)
    json leftover_teachers = json::array();   // world.lua `teachers`
    std::map<std::string, std::map<std::string, std::string>> leftover_strings;  // language -> id -> text
    // what a comparison with the layer mod must not expect in the project: whole templates, and single settings ("Template|key")
    std::set<std::string> missing_templates, missing_settings;
};

// a world layer of the game and the mod's version of it (already parsed; the tests build them by hand)
struct LayerPair {
    std::string name;  // "Harbour_Town_NPC_01"
    GFile base, mod;   // base without entities = the game has no such layer
};
// what the mod's layers do to the world: entities moved (by entity ID), removed, added (spawned from their creator template)
struct WorldChanges {
    std::vector<EntityRef> moves, removals;
    std::vector<WorldSpawn> spawns;
    int new_npcs = 0, skipped_template = 0, scale_changes = 0;
};
WorldChanges world_changes(const std::vector<LayerPair>& layers, const std::map<std::string, std::string>& template_guids);
std::vector<LayerPair> parse_layer_pairs(const p2m::ProjectInput& in, std::vector<std::string>& problems);

// the pure mapping (the unit tests call it with hand-made tables); `game` (optional) classifies the template patches and is not needed otherwise; `layers` (optional)
// replaces the world layers read from `in.world_layers`
Project project_from_pack2mod(const p2m::ProjectInput& in, const GameData* game, Pack2ProjectReport& rep, const std::vector<LayerPair>* layers = nullptr);

// Compare the layer mod the converter wrote (<mod_dir>/data/*.lua, strings/*.csv, assets/) with the mod the editor's exporter makes of the project
// (<export_dir>): the effective template settings, the recipes, the texts per language, the container fills and the asset files. Differences of what
// the report lists as not mapped are not differences. Empty result = equivalent.
struct CompareStats {  // what a comparison looked at
    int templates = 0, settings = 0, texts = 0, fills = 0, assets = 0, dialog_ops = 0, teachers = 0, world_edits = 0;
};
std::vector<std::string> compare_mod_with_export(const std::string& mod_dir, const std::string& export_dir, const Pack2ProjectReport& rep, CompareStats* stats = nullptr);

// The world edits (moved, removed and added entities) have no counterpart in the layer mod. They are checked against the world layers they were read from:
// the export's moves recompose the mod's matrix, removals are entities the mod's layer lacks, spawns sit where the mod's new entities are.
std::vector<std::string> verify_world_edits(const std::vector<LayerPair>& layers, const std::string& export_dir, CompareStats* stats = nullptr);

// the whole writer: project file, asset folder check, unmapped/, report section, optional verification (p2m::ProjectWriter)
p2m::ProjectResult write_pack2mod_project(const p2m::ProjectInput& in, std::ostream& log);

// makes risen-pack2mod's --project available (the program calls it once at start)
void register_pack2mod_project_writer();

}  // namespace mk
