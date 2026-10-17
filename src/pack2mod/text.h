// Pure text helpers of the converter (spreadsheet artefacts of mod string tables, CSV quoting, resource names). Ports of the helpers of
// tools/extmod_gen.py / extmod_diff.py; unit tested without any game data.
#pragma once

#include <optional>
#include <string>
#include <vector>

namespace p2m {

// "'Hello'" -> "Hello", backtick and acute accent -> apostrophe, trimmed (extmod_diff.norm_text)
std::string norm_text(const std::string& s);
// norm_text plus the quote artefacts of spreadsheets; nullopt for placeholder cells ("", "-", "...", "?")
std::optional<std::string> norm_cell(const std::string& s);
// one field of strings/*.csv (`id;text`)
std::string csv_field(const std::string& s);
// `Item_Pearl._xmsh` (compiled spelling) and `Item_Pearl.xmsh` name the same resource
std::string norm_mesh(const std::string& s);
// "0; 12; -15.5"
std::string vec_text(const std::vector<double>& v);
// `\._x[a-z]+$` (case-insensitive): a compiled resource file name (._xmsh, ._ximg, ._xmat, ._xmac ...)
bool is_asset_name(const std::string& bare);
// a template name made of letters, digits and underscores can be a Lua identifier prefix: sanitise a folder name into a mod id
std::string make_mod_id(const std::string& folder_name);

}  // namespace p2m
