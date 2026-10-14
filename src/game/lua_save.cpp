#include "game/lua_save.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

#include <spdlog/spdlog.h>

#include "common/paths.h"
#include "lua/runtime.h"

// Persistent mod data (docs/LUA_API.md, "Persistence"). The savegame format belongs to the engine, so mod data
// lives in a sidecar file per savegame: <spass>/saves/<savegame name>.lua, a plain Lua table literal
//   return { ["mod_id"] = { ["key"] = value, ... }, ... }
// that is read back with an empty environment (it can only describe data, never call anything).

namespace spass::game {
namespace {

constexpr const char* kStoreKey = "spass.save.store";
constexpr const char* kBackupKey = "spass.save.backup";
constexpr int kMaxDepth = 16;

std::filesystem::path sidecarPath(const std::string& saveName) {
    std::string safe = saveName;
    for (char& c : safe) {
        if (std::string_view("\\/:*?\"<>|").find(c) != std::string_view::npos || static_cast<unsigned char>(c) < 32) {
            c = '_';
        }
    }
    return paths::dataDir() / "saves" / (safe + ".lua");
}

sol::table storeTable(sol::state_view lua, const char* key) {
    sol::object o = lua.registry()[key];
    if (o.is<sol::table>()) {
        return o.as<sol::table>();
    }
    sol::table t = lua.create_table();
    lua.registry()[key] = t;
    return t;
}

// ---- serialization --------------------------------------------------------------------------------------------

void writeString(std::string& out, const std::string& s) {
    out += '"';
    for (const unsigned char c : s) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            default:
                if (c < 32) {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\%03d", c);  // fixed width: unambiguous before a digit
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    out += '"';
}

bool writeNumber(std::string& out, double d, std::string& err) {
    if (!std::isfinite(d)) {
        err = "numbers must be finite";
        return false;
    }
    char buf[40];
    if (d == std::floor(d) && std::fabs(d) < 1e15) {
        std::snprintf(buf, sizeof buf, "%lld", static_cast<long long>(d));
    } else {
        std::snprintf(buf, sizeof buf, "%.17g", d);
    }
    out += buf;
    return true;
}

bool writeValue(std::string& out, const sol::object& v, int depth, std::set<const void*>& path, std::string& err);

bool writeTable(std::string& out, const sol::table& t, int depth, std::set<const void*>& path, std::string& err) {
    if (depth > kMaxDepth) {
        err = "tables are nested too deeply";
        return false;
    }
    const void* id = t.pointer();
    if (!path.insert(id).second) {
        err = "a table contains itself";
        return false;
    }
    std::vector<std::pair<sol::object, sol::object>> items;
    for (const auto& kv : t) {
        items.emplace_back(kv.first, kv.second);
    }
    // Deterministic output: numbers first (ascending), then strings (ascending).
    using Item = std::pair<sol::object, sol::object>;
    std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
        const bool an = a.first.get_type() == sol::type::number, bn = b.first.get_type() == sol::type::number;
        if (an != bn) {
            return an;
        }
        if (an) {
            return a.first.as<double>() < b.first.as<double>();
        }
        if (a.first.get_type() == sol::type::string && b.first.get_type() == sol::type::string) {
            return a.first.as<std::string>() < b.first.as<std::string>();
        }
        return false;
    });
    out += '{';
    for (const auto& [k, v] : items) {
        out += '[';
        if (k.get_type() == sol::type::number) {
            if (!writeNumber(out, k.as<double>(), err)) {
                return false;
            }
        } else if (k.get_type() == sol::type::string) {
            writeString(out, k.as<std::string>());
        } else {
            err = "table keys must be strings or numbers";
            return false;
        }
        out += "]=";
        if (!writeValue(out, v, depth + 1, path, err)) {
            return false;
        }
        out += ',';
    }
    out += '}';
    path.erase(id);
    return true;
}

bool writeValue(std::string& out, const sol::object& v, int depth, std::set<const void*>& path, std::string& err) {
    switch (v.get_type()) {
        case sol::type::boolean: out += v.as<bool>() ? "true" : "false"; return true;
        case sol::type::number: return writeNumber(out, v.as<double>(), err);
        case sol::type::string: writeString(out, v.as<std::string>()); return true;
        case sol::type::table: return writeTable(out, v.as<sol::table>(), depth, path, err);
        default: err = "only booleans, numbers, strings and tables can be saved"; return false;
    }
}

// Throws a Lua error when `v` is something the store cannot hold (checked at set() time, so the mistake is
// reported where it was made; the same rules are applied again when writing).
void validate(const sol::object& v) {
    std::string out, err;
    std::set<const void*> path;
    if (!writeValue(out, v, 0, path, err)) {
        throw sol::error("risen.save: " + err);
    }
}

std::string readFile(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

}  // namespace

namespace savestore {

std::string serialize() {
    auto& rt = lua::Runtime::instance();
    auto lock = rt.lock();
    if (!rt.running()) {
        return {};
    }
    sol::state_view lua = rt.lua();
    sol::table store = storeTable(lua, kStoreKey);
    std::vector<std::string> ids;
    for (const auto& kv : store) {
        if (kv.first.get_type() == sol::type::string) {
            ids.push_back(kv.first.as<std::string>());
        }
    }
    std::sort(ids.begin(), ids.end());
    std::string out = "-- risen-spass mod data; written when the savegame was saved\nreturn {\n";
    for (const auto& id : ids) {
        sol::object data = store[id];
        std::string body, err;
        std::set<const void*> path;
        if (!data.is<sol::table>() || !writeValue(body, data, 0, path, err)) {
            spdlog::error("save: data of mod '{}' is not saveable ({}): left out of this savegame", id, err);
            continue;
        }
        out += "[";
        writeString(out, id);
        out += "]=" + body + ",\n";
    }
    out += "}\n";
    return out;
}

bool writeSidecar(const std::string& name, const std::string& text) {
    try {
        const auto path = sidecarPath(name);
        std::filesystem::create_directories(path.parent_path());
        const auto tmp = std::filesystem::path(path).replace_extension(".tmp");
        {
            std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
            out << text;
            if (!out) {
                return false;
            }
        }
        std::filesystem::rename(tmp, path);
        return true;
    } catch (const std::exception& e) {
        spdlog::error("save: cannot write the mod data of savegame '{}': {}", name, e.what());
        return false;
    }
}

void removeSidecar(const std::string& name) {
    std::error_code ec;
    std::filesystem::remove(sidecarPath(name), ec);
}

void beginLoad(const std::string& name) {
    auto& rt = lua::Runtime::instance();
    auto lock = rt.lock();
    if (!rt.running()) {
        return;
    }
    sol::state_view lua = rt.lua();
    lua.registry()[kBackupKey] = storeTable(lua, kStoreKey);
    sol::table fresh = lua.create_table();

    const auto path = sidecarPath(name);
    if (std::filesystem::exists(path)) {
        const std::string text = readFile(path);
        sol::load_result chunk = lua.load(text, "=savedata", sol::load_mode::text);
        bool ok = chunk.valid();
        if (ok) {
            sol::protected_function fn = chunk;
            sol::environment env(lua, sol::create);  // empty: the file can only describe data
            sol::set_environment(env, fn);
            sol::protected_function_result r = fn();
            if (r.valid() && r.get_type() == sol::type::table) {
                fresh = r.get<sol::table>();
            } else {
                ok = false;
            }
        }
        if (!ok) {
            spdlog::error("save: the mod data of savegame '{}' is damaged and was ignored", name);
        } else {
            spdlog::info("save: loaded the mod data of savegame '{}'", name);
        }
    } else {
        spdlog::info("save: savegame '{}' has no mod data (new game state)", name);
    }
    lua.registry()[kStoreKey] = fresh;
}

void restoreBackup() {
    auto& rt = lua::Runtime::instance();
    auto lock = rt.lock();
    if (!rt.running()) {
        return;
    }
    sol::state_view lua = rt.lua();
    sol::object backup = lua.registry()[kBackupKey];
    if (backup.is<sol::table>()) {
        lua.registry()[kStoreKey] = backup;
    }
    lua.registry()[kBackupKey] = sol::lua_nil;
}

void commitLoad() {
    auto& rt = lua::Runtime::instance();
    auto lock = rt.lock();
    if (rt.running()) {
        rt.lua().registry()[kBackupKey] = sol::lua_nil;
    }
}

void clear() {
    auto& rt = lua::Runtime::instance();
    auto lock = rt.lock();
    if (rt.running()) {
        sol::state_view lua = rt.lua();
        lua.registry()[kStoreKey] = lua.create_table();
    }
}

}  // namespace savestore

void bindSave() {
    lua::Runtime::instance().addModBinding([](sol::state_view lua, sol::table api, const lua::ModInfo& mod) {
        const std::string id = mod.id;
        // The mod's own table inside the store; created on first use.
        auto data = [lua, id](bool create) -> sol::optional<sol::table> {
            sol::state_view L = lua;
            sol::table store = storeTable(L, kStoreKey);
            sol::object o = store[id];
            if (o.is<sol::table>()) {
                return o.as<sol::table>();
            }
            if (!create) {
                return sol::nullopt;
            }
            sol::table t = L.create_table();
            store[id] = t;
            return t;
        };
        const auto checkKey = [](const sol::object& k) {
            if (k.get_type() != sol::type::string && k.get_type() != sol::type::number) {
                throw sol::error("risen.save: keys must be strings or numbers");
            }
        };

        sol::table save = lua.create_table();
        // risen.save.get(key [, default])
        save.set_function("get", [data, checkKey](sol::object key, sol::object def) -> sol::object {
            checkKey(key);
            if (auto t = data(false)) {
                sol::object v = (*t)[key];
                if (v.valid() && v.get_type() != sol::type::lua_nil) {
                    return v;
                }
            }
            return def;
        });
        // risen.save.set(key, value): booleans, numbers, strings and (acyclic) tables of those; nil removes the key.
        save.set_function("set", [data, checkKey](sol::object key, sol::object value) {
            checkKey(key);
            if (!value.valid() || value.get_type() == sol::type::lua_nil) {
                if (auto t = data(false)) {
                    (*t)[key] = sol::lua_nil;
                }
                return;
            }
            validate(value);
            (*data(true))[key] = value;
        });
        save.set_function("remove", [data, checkKey](sol::object key) {
            checkKey(key);
            if (auto t = data(false)) {
                (*t)[key] = sol::lua_nil;
            }
        });
        // risen.save.keys() -> sorted list of the keys currently stored
        save.set_function("keys", [data, lua]() {
            sol::state_view L = lua;
            sol::table out = L.create_table();
            std::vector<std::string> names;
            if (auto t = data(false)) {
                for (const auto& kv : *t) {
                    names.push_back(kv.first.get_type() == sol::type::string ? kv.first.as<std::string>()
                                                                             : std::to_string(kv.first.as<double>()));
                }
            }
            std::sort(names.begin(), names.end());
            int i = 1;
            for (const auto& n : names) {
                out[i++] = n;
            }
            return out;
        });
        save.set_function("clear", [data]() {
            if (auto t = data(false)) {
                for (const auto& kv : *t) {
                    (*t)[kv.first] = sol::lua_nil;
                }
            }
        });
        api["save"] = save;
    });
}

}  // namespace spass::game
