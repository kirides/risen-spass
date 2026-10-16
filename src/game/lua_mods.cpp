#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <vector>

#include <spdlog/spdlog.h>

#include "game/bindings.h"
#include "game/lua_save.h"
#include "game/migrate_lua.h"
#include "lua/mods.h"
#include "lua/runtime.h"

// risen.mods (docs/LUA_API.md "The mod manager API", "Hot reload"): the list of mods with their state, reload / unload / load, switching mods on and off (kept in
// <spass>/mods.toml), the custom load order, what a mod left in the current savegame, and the version helpers of risen.migrate. The Lua-side pieces (orphan
// finder, retire) sit in migrate_lua.h. Every function validates its arguments and answers `nil, reason` instead of raising.

namespace spass::game {
namespace {

using lua::ModAction;
using lua::ModState;
using lua::ModView;
using lua::ReloadReport;
using lua::Runtime;

using Two = std::tuple<sol::object, sol::object>;

Two okResult(sol::state_view L, sol::object value) { return {value, sol::make_object(L, sol::lua_nil)}; }
Two failResult(sol::state_view L, const std::string& why) { return {sol::make_object(L, sol::lua_nil), sol::make_object(L, why)}; }

bool idArg(const sol::object& o, std::string& id) {
    if (!o.valid() || o.get_type() != sol::type::string) {
        return false;
    }
    id = o.as<std::string>();
    return !id.empty() && id.size() <= 80;
}

sol::table stringList(sol::state_view L, const std::vector<std::string>& v) {
    sol::table t = L.create_table();
    int i = 1;
    for (const std::string& s : v) {
        t[i++] = s;
    }
    return t;
}

// opts: nil or a table of booleans; `known` lists the accepted keys. Returns an error text.
std::string readFlags(const sol::object& opts, std::initializer_list<const char*> known, std::map<std::string, bool>& out) {
    if (!opts.valid() || opts.get_type() == sol::type::lua_nil) {
        return {};
    }
    if (opts.get_type() != sol::type::table) {
        return "the options must be a table or nil";
    }
    for (const auto& kv : opts.as<sol::table>()) {
        if (kv.first.get_type() != sol::type::string) {
            return "the option names must be strings";
        }
        const std::string key = kv.first.as<std::string>();
        if (std::none_of(known.begin(), known.end(), [&](const char* k) { return key == k; })) {
            std::string list;
            for (const char* k : known) {
                list += (list.empty() ? "" : ", ") + std::string(k);
            }
            return "unknown option '" + key.substr(0, 40) + "' (valid: " + list + ")";
        }
        if (kv.second.get_type() != sol::type::boolean) {
            return "option '" + key + "' must be true or false";
        }
        out[key] = kv.second.as<bool>();
    }
    return {};
}

sol::table actionToLua(sol::state_view L, const ModAction& a) {
    sol::table t = L.create_table();
    t["id"] = a.id;
    t["action"] = a.action;
    if (!a.error.empty()) {
        t["error"] = a.error;
    }
    t["version_before"] = a.versionBefore;
    t["version_after"] = a.versionAfter;
    t["handlers_removed"] = a.handlersRemoved;
    t["handlers_added"] = a.handlersAdded;
    t["released"] = stringList(L, a.released);
    t["restart_required"] = stringList(L, a.restart);
    t["state_carried"] = a.stateCarried;
    return t;
}

sol::table reportToLua(sol::state_view L, const ReloadReport& r) {
    sol::table t = L.create_table();
    t["target"] = r.target;
    t["ok"] = r.ok;
    t["queued"] = r.queued;
    if (!r.error.empty()) {
        t["error"] = r.error;
    }
    sol::table actions = L.create_table();
    int i = 1;
    for (const ModAction& a : r.actions) {
        actions[i++] = actionToLua(L, a);
    }
    t["actions"] = actions;
    // what a person wants to read: one line per mod
    std::string summary;
    if (r.queued) {
        summary = "queued: runs at the start of the next frame";
    } else if (!r.ok && r.actions.empty()) {
        summary = "refused: " + r.error;
    } else {
        for (const ModAction& a : r.actions) {
            summary += (summary.empty() ? "" : "; ") + a.id + " " + a.action;
            if (!a.error.empty()) {
                summary += " (" + a.error.substr(0, 120) + ")";
            }
            if (!a.restart.empty()) {
                summary += " [restart needed for " + std::to_string(a.restart.size()) + " thing(s)]";
            }
        }
    }
    t["summary"] = summary;
    return t;
}

// What the mod left in the live store: the entries of its table, counted per key.
sol::table dataToLua(sol::state_view L, const std::string& id) {
    sol::table out = L.create_table();
    sol::object rec = savestore::modRecord(L, id);
    const std::string version = savestore::recordedVersion(L, id);
    out["present"] = rec.valid() && rec.get_type() == sol::type::table;
    if (!version.empty()) {
        out["saved_version"] = version;
    }
    sol::table keys = L.create_table();
    int total = 0;
    int npcs = 0, rules = 0, states = 0, routines = 0;
    if (rec.valid() && rec.get_type() == sol::type::table) {
        for (const auto& kv : rec.as<sol::table>()) {
            if (kv.first.get_type() != sol::type::string) {
                ++total;
                continue;
            }
            const std::string k = kv.first.as<std::string>();
            int n = 1;
            if (kv.second.get_type() == sol::type::table) {
                n = 0;
                for (const auto& e : kv.second.as<sol::table>()) {
                    (void)e;
                    ++n;
                }
            }
            keys[k] = n;
            total += 1;
            if (k == "persistent_npcs") npcs = n;
            else if (k == "spawn_rules") rules = n;
            else if (k == "ai_states") states = n;
            else if (k == "npc_routines") routines = n;
        }
    }
    out["keys"] = keys;
    out["key_count"] = total;
    out["npcs"] = npcs;
    out["spawn_rules"] = rules;
    out["ai_states"] = states;
    out["routines"] = routines;
    return out;
}

// A copy of a saved value for reading (the mod manager looks at the records of mods that are not installed): plain data, nested at most 8 levels.
sol::object copyData(sol::state_view L, const sol::object& v, int depth) {
    if (v.get_type() == sol::type::table) {
        if (depth > 8) {
            return sol::make_object(L, sol::lua_nil);
        }
        sol::table out = L.create_table();
        for (const auto& kv : v.as<sol::table>()) {
            if (kv.first.get_type() == sol::type::string || kv.first.get_type() == sol::type::number) {
                out[kv.first] = copyData(L, kv.second, depth + 1);
            }
        }
        return sol::make_object(L, out);
    }
    if (v.get_type() == sol::type::string || v.get_type() == sol::type::number || v.get_type() == sol::type::boolean) {
        return v;
    }
    return sol::make_object(L, sol::lua_nil);
}

sol::table viewToLua(sol::state_view L, const ModView& v) {
    sol::table t = L.create_table();
    const lua::ModInfo& m = v.info;
    t["id"] = m.id;
    t["name"] = m.name;
    t["version"] = m.version;
    t["author"] = m.author;
    t["description"] = m.description;
    t["dir"] = m.dir.generic_string();
    t["folder"] = m.dir.filename().string();
    t["state"] = lua::modStateName(v.state);
    t["loaded"] = v.state == ModState::Loaded;
    t["failed"] = v.state == ModState::Failed;
    if (!v.kind.empty()) {
        t["kind"] = v.kind;
    }
    if (!v.reason.empty()) {
        t["reason"] = v.reason;
    }
    t["enabled"] = v.userEnabled;
    if (v.orderIndex >= 0) {
        t["load_order"] = v.orderIndex + 1;
    }
    t["requires"] = stringList(L, m.requires_);
    t["load_after"] = stringList(L, m.loadAfter);
    t["needed_by"] = stringList(L, v.neededBy);
    t["api_version"] = m.apiVersion;
    t["reloadable"] = v.reloadable;
    if (!v.reloadNote.empty()) {
        t["reload_note"] = v.reloadNote;
    }
    t["load_count"] = v.loadCount;
    t["restart_required"] = stringList(L, v.restartNotes);
    {
        sol::table content = L.create_table();
        content["quests"] = stringList(L, v.content.count("quests") ? v.content.at("quests") : std::vector<std::string>{});
        content["dialogs"] = stringList(L, v.content.count("dialogs") ? v.content.at("dialogs") : std::vector<std::string>{});
        t["content"] = content;
    }
    if (!m.id.empty() && m.id[0] != '!') {
        t["data"] = dataToLua(L, m.id);
    }
    return t;
}

void runChunk(sol::state_view L, const char* chunk, const char* name, sol::object a, sol::object b = {}, sol::object c = {}) {
    sol::load_result loaded = L.load(chunk, name);
    if (!loaded.valid()) {
        spdlog::error("lua: {} failed to load: {}", name, loaded.get<sol::error>().what());
        return;
    }
    sol::protected_function fn = loaded;
    sol::protected_function_result res = fn(a, b, c);
    if (!res.valid()) {
        spdlog::error("lua: {} failed to run: {}", name, res.get<sol::error>().what());
    }
}

}  // namespace

void installMigrateModApi(sol::state_view L, sol::table api, const lua::ModInfo& mod) {
    runChunk(L, kMigrateChunk, "=risen.migrate", api, sol::make_object(L, mod.id), sol::make_object(L, mod.version));
}

void installRetireModApi(sol::state_view L, sol::table api, const lua::ModInfo& mod) {
    sol::object registrar = L.registry()["spass.retire.register"];
    sol::object toml = sol::make_object(L, sol::lua_nil);
    if (!mod.retire.empty()) {
        sol::table t = L.create_table();
        for (const auto& [kind, names] : mod.retire) {
            sol::table list = L.create_table();
            int i = 1;
            for (const std::string& n : names) {
                list[i++] = n;
            }
            t[kind] = list;
        }
        toml = sol::make_object(L, t);
    }
    sol::load_result loaded = L.load(kRetireChunk, "=risen.retire");
    if (!loaded.valid()) {
        spdlog::error("lua: risen.retire failed to load: {}", loaded.get<sol::error>().what());
        return;
    }
    sol::protected_function fn = loaded;
    sol::protected_function_result res = fn(api, mod.id, toml, registrar);
    if (!res.valid()) {
        spdlog::error("lua: risen.retire failed to run: {}", res.get<sol::error>().what());
    }
}

void bindMods(sol::state_view state, sol::table risen) {
    sol::table mods = state.create_table();

    // risen.mods.list() -> { mod, ... } in load order (loaded mods first, then the others by id)
    mods.set_function("list", [](sol::this_state ts) {
        sol::state_view L(ts);
        sol::table out = L.create_table();
        int i = 1;
        for (const ModView& v : Runtime::instance().modViews()) {
            out[i++] = viewToLua(L, v);
        }
        return out;
    });
    // risen.mods.get(id) -> mod | nil, reason
    mods.set_function("get", [](sol::this_state ts, sol::object idArgument) -> Two {
        sol::state_view L(ts);
        std::string id;
        if (!idArg(idArgument, id)) {
            return failResult(L, "the first argument must be a mod id (a string)");
        }
        const std::optional<ModView> v = Runtime::instance().modView(id);
        if (!v) {
            return failResult(L, "no mod '" + id.substr(0, 60) + "'");
        }
        return okResult(L, sol::make_object(L, viewToLua(L, *v)));
    });
    // risen.mods.reload(id [, { now = false }]) -> report | nil, reason
    mods.set_function("reload", [](sol::this_state ts, sol::object idArgument, sol::object optsArg) -> Two {
        sol::state_view L(ts);
        std::string id;
        if (!idArg(idArgument, id)) {
            return failResult(L, "the first argument must be a mod id (a string)");
        }
        std::map<std::string, bool> flags;
        if (const std::string why = readFlags(optsArg, {"now"}, flags); !why.empty()) {
            return failResult(L, why);
        }
        const ReloadReport r = Runtime::instance().reload(id, flags.count("now") != 0 && flags["now"]);
        if (!r.ok && !r.queued) {
            return {sol::make_object(L, reportToLua(L, r)), sol::make_object(L, r.error)};
        }
        return okResult(L, sol::make_object(L, reportToLua(L, r)));
    });
    // risen.mods.unload(id [, { cascade = false }]) -> report | nil, reason
    mods.set_function("unload", [](sol::this_state ts, sol::object idArgument, sol::object optsArg) -> Two {
        sol::state_view L(ts);
        std::string id;
        if (!idArg(idArgument, id)) {
            return failResult(L, "the first argument must be a mod id (a string)");
        }
        std::map<std::string, bool> flags;
        if (const std::string why = readFlags(optsArg, {"cascade"}, flags); !why.empty()) {
            return failResult(L, why);
        }
        const ReloadReport r = Runtime::instance().unload(id, flags.count("cascade") != 0 && flags["cascade"]);
        if (!r.ok) {
            return {sol::make_object(L, reportToLua(L, r)), sol::make_object(L, r.error)};
        }
        return okResult(L, sol::make_object(L, reportToLua(L, r)));
    });
    // risen.mods.load(id) -> report | nil, reason: loads a mod that is not loaded now (unloaded, failed earlier, switched off for this run only)
    mods.set_function("load", [](sol::this_state ts, sol::object idArgument) -> Two {
        sol::state_view L(ts);
        std::string id;
        if (!idArg(idArgument, id)) {
            return failResult(L, "the first argument must be a mod id (a string)");
        }
        const ReloadReport r = Runtime::instance().load(id);
        if (!r.ok) {
            return {sol::make_object(L, reportToLua(L, r)), sol::make_object(L, r.error)};
        }
        return okResult(L, sol::make_object(L, reportToLua(L, r)));
    });
    // risen.mods.enable(id [, { cascade = false, now = true }]) / disable(...): writes mods.toml; `now` also loads / unloads at once
    const auto setEnabled = [](bool enabled) {
        return [enabled](sol::this_state ts, sol::object idArgument, sol::object optsArg) -> Two {
            sol::state_view L(ts);
            std::string id;
            if (!idArg(idArgument, id)) {
                return failResult(L, "the first argument must be a mod id (a string)");
            }
            std::map<std::string, bool> flags;
            if (const std::string why = readFlags(optsArg, {"cascade", "now"}, flags); !why.empty()) {
                return failResult(L, why);
            }
            const bool cascade = flags.count("cascade") != 0 && flags["cascade"];
            const bool now = flags.count("now") == 0 || flags["now"];
            const ReloadReport r = Runtime::instance().setEnabled(id, enabled, cascade, now);
            if (!r.ok) {
                return {sol::make_object(L, reportToLua(L, r)), sol::make_object(L, r.error)};
            }
            return okResult(L, sol::make_object(L, reportToLua(L, r)));
        };
    };
    mods.set_function("enable", setEnabled(true));
    mods.set_function("disable", setEnabled(false));
    // risen.mods.order() -> { current = {ids}, planned = {ids}, automatic = {ids}, custom = {ids} }
    mods.set_function("order", [](sol::this_state ts) {
        sol::state_view L(ts);
        Runtime& rt = Runtime::instance();
        sol::table t = L.create_table();
        t["current"] = stringList(L, rt.loadOrder());
        t["planned"] = stringList(L, rt.plannedOrder(true));
        t["automatic"] = stringList(L, rt.plannedOrder(false));
        t["custom"] = stringList(L, rt.userConfig().order);
        return t;
    });
    // A plain list of mod ids. Read with raw iteration only: asking a table for its length (lua_len) runs its __len metamethod, which may raise an error through this frame.
    const auto orderArg = [](const sol::object& o, std::vector<std::string>& order) -> std::string {
        if (!o.valid() || o.get_type() != sol::type::table) {
            return "the order must be a list of mod ids";
        }
        double expected = 1;
        for (const auto& kv : o.as<sol::table>()) {
            if (order.size() >= 500) {
                return "the order is too long";
            }
            if (kv.first.get_type() != sol::type::number || kv.first.as<double>() != expected || kv.second.get_type() != sol::type::string) {
                return "the order must be a plain list of mod ids (strings)";
            }
            ++expected;
            order.push_back(kv.second.as<std::string>());
        }
        return {};
    };
    // risen.mods.check_order(list) -> true | nil, reason: whether the list could be the custom load order (nobody before what it requires or loads after)
    mods.set_function("check_order", [orderArg](sol::this_state ts, sol::object list) -> Two {
        sol::state_view L(ts);
        std::vector<std::string> order;
        if (const std::string why = orderArg(list, order); !why.empty()) {
            return failResult(L, why);
        }
        std::vector<lua::ModInfo> infos;
        for (const ModView& v : Runtime::instance().modViews()) {
            if (!v.info.id.empty() && v.info.id[0] != '!') {
                infos.push_back(v.info);
            }
        }
        if (const std::string why = lua::checkOrder(infos, order); !why.empty()) {
            return failResult(L, why);
        }
        return okResult(L, sol::make_object(L, true));
    });
    // risen.mods.set_order(list | nil) -> true | nil, reason: the custom load order kept in mods.toml (nil = the automatic order); takes effect at the next start
    mods.set_function("set_order", [orderArg](sol::this_state ts, sol::object list) -> Two {
        sol::state_view L(ts);
        std::vector<std::string> order;
        if (list.valid() && list.get_type() != sol::type::lua_nil) {
            if (const std::string why = orderArg(list, order); !why.empty()) {
                return failResult(L, why);
            }
        }
        if (const std::string why = Runtime::instance().setOrder(order); !why.empty()) {
            return failResult(L, why);
        }
        return okResult(L, sol::make_object(L, true));
    });
    // risen.mods.config() -> { path, enabled = { id = bool }, order = {ids}, problems = {texts} }
    mods.set_function("config", [](sol::this_state ts) {
        sol::state_view L(ts);
        Runtime& rt = Runtime::instance();
        const lua::UserConfig cfg = rt.userConfig();
        sol::table t = L.create_table();
        t["path"] = rt.userConfigPath().generic_string();
        sol::table en = L.create_table();
        for (const auto& [id, on] : cfg.enabled) {
            en[id] = on;
        }
        t["enabled"] = en;
        t["order"] = stringList(L, cfg.order);
        t["problems"] = stringList(L, rt.configProblems());
        return t;
    });
    // risen.mods.last_report(id) -> report | nil: what the latest reload / unload / load of the mod did
    mods.set_function("last_report", [](sol::this_state ts, sol::object idArgument) -> sol::object {
        sol::state_view L(ts);
        std::string id;
        if (!idArg(idArgument, id)) {
            return sol::make_object(L, sol::lua_nil);
        }
        const auto r = Runtime::instance().lastReport(id);
        return r ? sol::make_object(L, reportToLua(L, *r)) : sol::make_object(L, sol::lua_nil);
    });
    // risen.mods.watch([on [, seconds]]) -> bool: the development file watcher (a changed .lua / .toml / .csv file reloads its mod)
    mods.set_function("watch", [](sol::object on, sol::object seconds) {
        Runtime& rt = Runtime::instance();
        if (on.valid() && on.get_type() == sol::type::boolean) {
            const double s = seconds.valid() && seconds.get_type() == sol::type::number ? seconds.as<double>() : 1.0;
            rt.setWatch(on.as<bool>(), std::isfinite(s) ? s : 1.0);
        }
        return rt.watching();
    });
    // risen.mods.note_restart(text): the calling mod reports something only a restart of the game can change (shown in the reload report and the list)
    mods.set_function("note_restart", [](sol::object text) {
        if (!text.valid() || text.get_type() != sol::type::string) {
            return false;
        }
        const std::string& owner = Runtime::currentOwner();
        if (owner.empty()) {
            return false;
        }
        Runtime::instance().noteRestart(owner, text.as<std::string>().substr(0, 300));
        return true;
    });
    // risen.mods.note_content(kind, name): the quest / dialogue info helpers report what the calling mod defined ("quest", "dialog"), for ModEntry.content
    mods.set_function("note_content", [](sol::object kind, sol::object name) {
        const std::string& owner = Runtime::currentOwner();
        if (owner.empty() || !kind.valid() || !name.valid() || kind.get_type() != sol::type::string || name.get_type() != sol::type::string) {
            return false;
        }
        const std::string k = kind.as<std::string>();
        if (k != "quest" && k != "dialog") {
            return false;
        }
        Runtime::instance().noteContent(owner, k == "quest" ? "quests" : "dialogs", name.as<std::string>().substr(0, 200));
        return true;
    });
    // risen.mods.data(id) -> { present, saved_version, keys, key_count, npcs, spawn_rules, ai_states, routines }: what the mod has in the current savegame data
    mods.set_function("data", [](sol::this_state ts, sol::object idArgument) -> Two {
        sol::state_view L(ts);
        std::string id;
        if (!idArg(idArgument, id) || id[0] == '@') {
            return failResult(L, "the first argument must be a mod id (a string)");
        }
        return okResult(L, sol::make_object(L, dataToLua(L, id)));
    });
    // risen.mods.record(id) -> table | nil: a read-only copy of everything the mod keeps in the current savegame data (also for a mod that is not installed)
    mods.set_function("record", [](sol::this_state ts, sol::object idArgument) -> sol::object {
        sol::state_view L(ts);
        std::string id;
        if (!idArg(idArgument, id) || id[0] == '@') {
            return sol::make_object(L, sol::lua_nil);
        }
        sol::object rec = savestore::modRecord(L, id);
        return rec.valid() && rec.get_type() == sol::type::table ? copyData(L, rec, 0) : sol::make_object(L, sol::lua_nil);
    });
    // risen.mods.forget_record(id, key, subkey) -> bool: removes one entry (store[id][key][subkey]) of a mod's savegame data; explicit, for cleaning up after a removed mod
    mods.set_function("forget_record", [](sol::this_state ts, sol::object idArgument, sol::object keyArg, sol::object subArg) {
        sol::state_view L(ts);
        std::string id;
        if (!idArg(idArgument, id) || id[0] == '@' || !keyArg.valid() || keyArg.get_type() != sol::type::string || !subArg.valid() ||
            subArg.get_type() != sol::type::string) {
            return false;
        }
        sol::object rec = savestore::modRecord(L, id);
        if (!rec.valid() || rec.get_type() != sol::type::table) {
            return false;
        }
        sol::object sub = rec.as<sol::table>()[keyArg.as<std::string>()];
        if (!sub.valid() || sub.get_type() != sol::type::table) {
            return false;
        }
        sol::table t = sub.as<sol::table>();
        const bool had = t[subArg.as<std::string>()].get_type() != sol::type::lua_nil;
        t[subArg.as<std::string>()] = sol::lua_nil;
        return had;
    });
    // risen.mods.saved_version(id) -> string | nil: the version of the mod that wrote its data into the loaded savegame
    mods.set_function("saved_version", [](sol::this_state ts, sol::object idArgument) -> sol::object {
        sol::state_view L(ts);
        std::string id;
        if (!idArg(idArgument, id)) {
            return sol::make_object(L, sol::lua_nil);
        }
        const std::string v = savestore::recordedVersion(L, id);
        return v.empty() ? sol::make_object(L, sol::lua_nil) : sol::make_object(L, v);
    });
    // risen.mods.recorded() -> { id, ... }: every mod id that has data in the current savegame data (also mods that are not installed any more)
    mods.set_function("recorded", [](sol::this_state ts) {
        sol::state_view L(ts);
        return stringList(L, savestore::recordedMods(L));
    });
    // risen.mods.forget_data(id) -> bool: drops the mod's entry from the current savegame data (explicit; the engine's own savegame is not touched)
    mods.set_function("forget_data", [](sol::this_state ts, sol::object idArgument) {
        sol::state_view L(ts);
        std::string id;
        if (!idArg(idArgument, id)) {
            return false;
        }
        return savestore::dropMod(L, id);
    });
    // risen.mods.compare_versions(a, b) -> -1 | 0 | 1; risen.mods.version_matches(spec, version) -> bool
    mods.set_function("compare_versions", [](sol::object a, sol::object b) -> sol::optional<int> {
        if (!a.valid() || !b.valid() || a.get_type() != sol::type::string || b.get_type() != sol::type::string) {
            return sol::nullopt;
        }
        return lua::compareVersions(a.as<std::string>(), b.as<std::string>());
    });
    mods.set_function("version_matches", [](sol::this_state ts, sol::object spec, sol::object version) -> Two {
        sol::state_view L(ts);
        if (!spec.valid() || !version.valid() || spec.get_type() != sol::type::string || version.get_type() != sol::type::string) {
            return failResult(L, "both arguments must be strings");
        }
        const std::optional<bool> r = lua::versionMatches(spec.as<std::string>(), version.as<std::string>());
        if (!r) {
            return failResult(L, "'" + spec.as<std::string>().substr(0, 60) + "' is not a version specification (\"1.2.0\", \"<2.0\", \">=1.0,<1.5\", \"!=1.3\", \"*\")");
        }
        return okResult(L, sol::make_object(L, *r));
    });
    risen["mods"] = mods;

    // the Lua parts that need the whole table: retire / orphans (migrate_lua.h); the registrar they return is what the per-mod chunk of risen.retire reports to
    sol::load_result loaded = state.load(kModsChunk, "=risen.mods");
    if (!loaded.valid()) {
        spdlog::error("lua: risen.mods helpers failed to load: {}", loaded.get<sol::error>().what());
        return;
    }
    sol::protected_function fn = loaded;
    sol::protected_function_result res = fn(risen);
    if (!res.valid()) {
        spdlog::error("lua: risen.mods helpers failed to run: {}", res.get<sol::error>().what());
        return;
    }
    state.registry()["spass.retire.register"] = res.get<sol::object>();

}

}  // namespace spass::game
