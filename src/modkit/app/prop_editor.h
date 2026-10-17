// Editor of free property edits (`properties = { Set = { Name = value } }`): a table of the edited properties with a value widget by type, and an
// "add" list of the template's own properties (with the shipped value as the start value).
#pragma once

#include "core/gamedata.h"
#include "core/project.h"

namespace mk::ui {

// `t` is the template the edits apply to (the base of a new template, the shipped one of an edit); nullptr = free entry only
bool prop_edits(const char* id, PropEdits& edits, const GameData* g, const TemplateInfo* t);

// a game value as the JSON the export writes (enum values by name)
json val_to_json(const GameData& g, const p2m::Val& v, const std::string& type);

}  // namespace mk::ui
