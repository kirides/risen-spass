// Pickers: combo boxes with a filter over the game's templates / resources plus the project's own objects.
#pragma once

#include <initializer_list>
#include <string>
#include <vector>

#include "app/app.h"

namespace mk::ui {

// a template name; `kinds` restricts the game templates listed (empty = all); `extra` are project names listed first (e.g. new items).
// Free text is accepted too (a name the catalogue does not know is shown in orange).
bool template_picker(const char* label, std::string& name, const GameData* game, std::initializer_list<TplKind> kinds,
                     const std::vector<std::string>& extra = {}, float width = 0);
// a resource name by compiled extension ("_xmsh"); `strip_compiled` turns "X._xmsh" into "X.xmsh" (the raw name templates use)
bool asset_picker(const char* label, std::string& name, const GameData* game, const char* ext, bool strip_compiled, float width = 0);
// a quest id: the project's quests first, then every quest of the game (compiled_quests.bin) with its English title; free text is accepted too
// (a name nobody knows is shown in orange). `exclude`: a project quest left out (the quest being edited).
bool quest_picker(const char* label, std::string& name, App& app, const std::string& exclude = {}, float width = 0);
// A shipped dialogue info of the game by name: type to filter, the list shows name, speaker, quest and the first line it says (English), and the
// filter matches all of those. Free text is accepted (a name the game does not know is shown in orange).
bool info_picker(const char* label, std::string& name, App& app, float width = 0);
// The name of an NPC of the world (the entity name risen.entity.get takes): every NPC entity of the loaded world and the project's placed NPCs; starts
// loading the world when it is not there.
bool npc_name_picker(const char* label, std::string& name, App& app, float width = 0);
// The ID of a world entity (xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx): the list is filtered to the entities that carry `name_filter` (empty = all NPCs of the
// world; the typed filter then matches name, template, layer and ID). When an entity is picked `name` is set to its name if `name` is not null.
bool entity_id_picker(const char* label, std::string& guid, App& app, const std::string& name_filter, std::string* name = nullptr, float width = 0);
// an item icon (gCItem_PS.IconImage): the project's pictures first (they become cells of the mod's icon atlas), then every icon of the shipped
// templates, each with its picture; type to filter. Free text is accepted (a name nobody knows is shown in orange).
bool icon_picker(const char* label, std::string& icon, App& app, float width = 0);
// an effect of the game (compiled_effects.bin, gCEffect_PS.Effect: "eff_projectile_Fireball_01"); the list opens filtered by `start_filter`
// (e.g. "eff_projectile"). Free text is accepted (a name the game does not know is shown in orange).
bool effect_picker(const char* label, std::string& name, App& app, const char* start_filter = "", float width = 0);
// a label for a template name: the project's label, else the game's English label, else ""
std::string label_of(const App& app, const std::string& name);

// a play / stop button for a sound reference (core/assets.h); `id` keeps buttons of the same reference apart
bool sound_button(App& app, const std::string& ref, const char* id = "##play");
// a sound reference: the project's audio files and compiled sounds first, then the game's sounds (speech by voice language, effects), each with
// a play button. `raw_name`: the value is the raw name the engine plays ("X.wav", SaySound's SoundName) instead of "X._xsnd"; project audio files
// are not offered then.
bool sound_picker(const char* label, std::string& ref, App& app, bool raw_name = false, float width = 0);
// the sound of a dialogue line: one row for every language plus one per language of the mod (node data "sound", core/dialog.h line_sound)
bool sound_rows(const char* label, json& sound, App& app);

}  // namespace mk::ui
