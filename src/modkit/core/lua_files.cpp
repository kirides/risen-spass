#include "core/lua_files.h"
#include "core/i18n.h"

#include <luajit/lua.hpp>

#include <filesystem>
#include <map>
#include <mutex>
#include <regex>

#include "core/paths.h"

namespace mk {

LuaFileInfo read_lua_file_code(const std::string& code, const std::string& chunk_name) {
    LuaFileInfo info;
    lua_State* L = luaL_newstate();
    if (!L) {
        info.error = "no Lua state";
        return info;
    }
    if (luaL_loadbuffer(L, code.data(), code.size(), ("=" + chunk_name).c_str()) != 0) {
        info.error = lua_tostring(L, -1) ? lua_tostring(L, -1) : tr("syntax error");
        lua_close(L);
        return info;
    }
    lua_close(L);
    info.ok = true;
    // the code is not run (it may require modules and call the layer at the top level): hooks and script names are read from the text without
    // its comments (block comments --[[ ]] / --[==[ ]==], line comments; a "--" inside a string literal cuts that line short, good enough for hints)
    static const std::regex block(R"(--\[(=*)\[[\s\S]*?\]\1\])"), line(R"(--[^\n]*)");
    const std::string text = std::regex_replace(std::regex_replace(code, block, " "), line, "");
    for (const char* h : {"before_install", "after_install"})
        // `before_install = function ...` in a table, or `function M.before_install(...)`
        if (std::regex_search(text, std::regex(std::string("\\b") + h + "\\s*=|function\\s+[A-Za-z_][A-Za-z0-9_.]*\\." + h + "\\s*\\(")))
            info.hooks.push_back(h);
    static const std::regex define(R"(risen\.script\.define\s*\(\s*["']([A-Za-z_][A-Za-z0-9_]*)["'])");
    for (auto it = std::sregex_iterator(text.begin(), text.end(), define); it != std::sregex_iterator(); ++it) {
        const std::string s = (*it)[1];
        if (std::find(info.scripts.begin(), info.scripts.end(), s) == info.scripts.end()) info.scripts.push_back(s);
    }
    return info;
}

LuaFileInfo lua_file_info(const std::string& path) {
    namespace fs = std::filesystem;
    static std::mutex mu;
    static std::map<std::string, std::pair<std::string, LuaFileInfo>> cache;  // path -> (stamp, info)
    std::error_code ec;
    const fs::path fp = u8(path);
    const auto size = fs::file_size(fp, ec);
    if (ec) {
        LuaFileInfo none;
        none.error = trf("the file {0} cannot be read", {path});
        return none;
    }
    const auto time = fs::last_write_time(fp, ec);
    const std::string stamp = std::to_string(size) + ":" + std::to_string(time.time_since_epoch().count());
    {
        std::lock_guard<std::mutex> lock(mu);
        auto it = cache.find(path);
        if (it != cache.end() && it->second.first == stamp) return it->second.second;
    }
    LuaFileInfo info;
    try {
        info = read_lua_file_code(read_text(fp), u8str(fp.filename()));
    } catch (const std::exception& e) {
        info.error = e.what();
    }
    std::lock_guard<std::mutex> lock(mu);
    cache[path] = {stamp, info};
    return info;
}

std::string lua_file_name_problem(const std::string& name) {
    if (name.empty()) return tr("the Lua file needs a name");
    if (name.size() > 64) return tr("at most 64 characters");
    if (std::isdigit(static_cast<unsigned char>(name[0]))) return tr("the name may not start with a digit");
    for (unsigned char c : name)
        if (!(std::isalnum(c) && c < 0x80) && c != '_') return tr("letters, digits and _ only (it becomes the module name lua.<name>)");
    if (name == "custom") return tr("'custom' is the mod folder's own custom.lua: choose another name");
    return {};
}

std::string lua_module_file(const std::string& name) { return "lua/" + name + ".lua"; }
std::string lua_module_name(const std::string& name) { return "lua." + name; }

std::string lua_file_template(const std::string& name, const std::string& mod_id) {
    return "-- " + name + R"(: hand-written Lua of the mod, loaded by the exported main.lua (risen-modkit, Lua panel).
-- The whole layer API is available (types/risen.d.lua, docs/MODDING_GUIDE.md, docs/LUA_API.md). This file runs before the editor's content is
-- installed; an error here is logged as ")" + "[" + mod_id + R"(]" (the editor's Problems panel shows it) and the rest of the mod keeps working.
--
-- Extension points:
--   * return before_install(data): change the generated tables before they are installed (data.balance is a risen.balance.install spec,
--     data.npcs, data.quests, data.dialogs, data.world, data.events, ... are the files in data/)
--   * return after_install(runtime): runs once the content is installed; `runtime` is require("modkit.runtime")
--   * scripts for the editor: a Run script node (Events) or a dialogue Script command calls a script by name, define it with risen.script.define
--   * events of the exported content: modkit.applied, modkit.quest_started, modkit.objective, modkit.quest_done, modkit.npc_ready, ...
--   * require("modkit.events"): set_enabled(graph, on), fire_signal(name), var_get(name), var_set(name, value); require("modkit.layers"): set, toggle

-- risen.script.define("MyScript", function(ctx) return 0 end)   -- ctx.self, ctx.other, ctx.arg

-- risen.on("modkit.quest_done", function(id, quest, result)
--     if id == ")" + mod_id + R"(" then risen.log.info(quest .. " " .. result) end
-- end)

return {
    -- before_install = function(data)
    --     for _, item in pairs(data.balance.new_items or {}) do item.value = item.value and item.value * 2 end
    -- end,
    -- after_install = function(runtime)
    -- end,
}
)";
}

}  // namespace mk
