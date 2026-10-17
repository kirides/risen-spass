// Small ImGui helpers shared by the panels. Every helper returns true when the user changed the value (the caller then calls App::touch()).
#pragma once

#include <optional>
#include <string>
#include <vector>

#include <imgui.h>

#include "core/project.h"

namespace mk::ui {

bool input_text(const char* label, std::string& s, ImGuiInputTextFlags flags = 0);
bool input_multiline(const char* label, std::string& s, float height_lines = 3.0f);
// an optional number: a checkbox "set" plus the field; unset = "leave as shipped" (the shipped value is shown as a hint)
bool opt_int(const char* label, std::optional<int>& v, const char* shipped_hint = nullptr, int def = 0);
bool opt_double(const char* label, std::optional<double>& v, const char* shipped_hint = nullptr, double def = 1.0, const char* fmt = "%.3g");
bool opt_text(const char* label, std::optional<std::string>& v, const char* shipped_hint = nullptr);
// text per language: one line per language of the project
bool lang_text(const char* label, Text& t, const std::vector<std::string>& languages, bool multiline = false);
// a list of strings, one per line (edit as text)
bool string_list(const char* label, std::vector<std::string>& v);
// name -> number map (skills, loot) as a table with add / remove; `choices` fills the add combo (empty = free text)
bool int_map(const char* id, IntMap& m, const std::vector<std::string>& choices, int def_value = 1, int min_value = -100000, int max_value = 100000);
void help(const char* text);  // a (?) marker with a tooltip
void section(const char* title);  // a separator with a title
// a combo over strings; returns true on change
bool combo(const char* label, std::string& v, const std::vector<std::string>& items, bool allow_empty = true);
// right-aligned small grey text
void hint(const char* text);

}  // namespace mk::ui
