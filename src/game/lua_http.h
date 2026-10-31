#pragma once

#include <optional>
#include <string>
#include <vector>

#include <sol/sol.hpp>

#include "lua/mods.h"

namespace spass::game {

// ---- risen.json -------------------------------------------------------------------------------------------------------------------

// JSON text -> Lua value. Objects and arrays become tables (arrays are 1-based sequences), numbers become Lua numbers, `null` in an object
// leaves the key out and in an array becomes `risen.json.null` (a sentinel, so the array keeps its length). nullopt + `error` on bad JSON or nesting over 64 levels.
std::optional<sol::object> jsonDecode(sol::state_view lua, const std::string& text, std::string& error);
// Lua value -> JSON text. A table with the keys 1..n is an array (an empty table too), any other table an object (number keys become strings).
// `pretty` indents. nullopt + `error` for functions, userdata, NaN/infinity and tables nested over 64 levels (cycles).
std::optional<std::string> jsonEncode(const sol::object& value, bool pretty, std::string& error);

// risen.json.{decode,encode,null}
void bindJson(sol::state_view lua, sol::table risen);

// ---- risen.http (docs/LUA_API.md "Network access") ----------------------------------------------------------------------------------

// Per-mod view: risen.http.{get_text,get_json,post_json,request,cancel,available}. The mod's `network` entry in mod.toml and the player's switches
// (mods.toml, the mod manager) decide whether a request is made.
void bindHttpModApi(sol::state_view lua, sol::table api, const lua::ModInfo& mod);

// Delivers the finished requests to their callbacks; called once per frame on the game thread.
void httpPump();

// Hot reload: the callbacks of the mod's requests that are still running are dropped.
void httpReleaseOwner(const std::string& mod, std::vector<std::string>& released);

}  // namespace spass::game
