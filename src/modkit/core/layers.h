// Mod layers (docs/editor/REQUIREMENTS.md section 3.12 / 7.6): a named group of the project's placed NPCs, world spawns and container fills that is switched on
// and off as a whole, like the game's story sectors. A layer starts enabled or not, may switch itself on / off when a condition first holds (the checks of the
// event graphs' Condition node) and is switched by the Events graphs (Layer node, Layer check). Exported as data/layers.lua; the members carry a `layer` field
// in data/npcs.lua and data/world.lua; modkit/layers.lua runs it.
#pragma once

#include <string>
#include <vector>

#include "core/dialog.h"
#include "core/gamedata.h"
#include "core/project.h"

namespace mk {

// the layer a member belongs to (nullptr = none); NPC by unique name, spawn / container by uid
const LayerDef* layer_of_npc(const Project& p, const std::string& npc);
const LayerDef* layer_of_spawn(const Project& p, const std::string& uid);
const LayerDef* layer_of_container(const Project& p, const std::string& uid);
LayerDef* find_layer(Project& p, const std::string& name);
const LayerDef* find_layer(const Project& p, const std::string& name);

// data/layers.lua: [ { name, enabled, enable_when?, disable_when? } ]; problems are appended to `issues` (node = the layer's uid)
json layers_data(const Project& p, std::vector<DialogIssue>* issues = nullptr);
// the layer's data entry rebuilt from data/layers.lua (import); members are set by the importer
LayerDef layer_from_data(const json& entry);

// the game's sectors that start switched off, for the Events editor's picker (names are file base names, `World::SetSectorStatus`)
struct StockSector {
    std::string name;
    bool enabled_on_startup = true;
};
std::vector<StockSector> stock_sectors(const GameData& g, const std::string& world = "World");

// a colour (0xAARRGGBB) for a layer, stable by its position in the list
unsigned layer_color(size_t index);

}  // namespace mk
