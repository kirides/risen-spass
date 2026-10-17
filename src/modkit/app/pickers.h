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
