// Deterministic Lua table writer for the export: a JSON value (object = table with keys, array = list) as a Lua literal. Keys keep the order of
// the JSON object (the exporter inserts them sorted where order does not matter); short tables stay on one line.
#pragma once

#include <string>

#include "core/project.h"

namespace mk {

std::string lua_string(const std::string& s);   // a quoted Lua string with escapes
bool lua_identifier(const std::string& s);       // usable as a bare key (not a keyword)
std::string lua_number(double d);                // shortest text that reads back as the same double; integral values without ".0"
// `indent` is the current indentation level (4 spaces each); tables shorter than `inline_width` characters are written on one line
std::string lua_value(const json& v, int indent = 0, size_t inline_width = 100);

}  // namespace mk
