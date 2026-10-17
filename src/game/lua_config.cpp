#include "game/lua_config.h"

#include "game/lua_options.h"

#include <spdlog/spdlog.h>
#include <toml++/toml.hpp>

namespace spass::game {
namespace {

sol::object copyValue(sol::state_view lua, const sol::object& o);

sol::table copyTable(sol::state_view lua, const sol::table& t) {
    sol::table out = lua.create_table();
    for (const auto& [k, v] : t) {
        out[k] = copyValue(lua, v);
    }
    return out;
}

sol::object copyValue(sol::state_view lua, const sol::object& o) {
    if (o.get_type() == sol::type::table) {
        return copyTable(lua, o.as<sol::table>());
    }
    return o;
}

const char* tomlTypeName(const toml::node& n) {
    switch (n.type()) {
        case toml::node_type::string: return "string";
        case toml::node_type::integer: return "integer";
        case toml::node_type::floating_point: return "float";
        case toml::node_type::boolean: return "boolean";
        case toml::node_type::table: return "table";
        case toml::node_type::array: return "array";
        default: return "value";
    }
}

// Merges `file` into `target` (a copy of the defaults), keeping the type of each default.
void merge(sol::state_view lua, const toml::table& file, sol::table& target, const std::string& path, std::vector<std::string>& problems) {
    for (const auto& [key, node] : file) {
        const std::string name(key.str());
        const std::string full = path.empty() ? name : path + "." + name;
        sol::object current = target[name];
        if (!current.valid() || current.get_type() == sol::type::lua_nil) {
            problems.push_back("unknown option '" + full + "' (ignored)");
            continue;
        }
        switch (current.get_type()) {
            case sol::type::boolean:
                if (node.is_boolean()) {
                    target[name] = node.as_boolean()->get();
                } else {
                    problems.push_back("option '" + full + "' must be true or false, not a " + tomlTypeName(node) + " (default kept)");
                }
                break;
            case sol::type::number:
                if (node.is_integer()) {
                    target[name] = static_cast<double>(node.as_integer()->get());
                } else if (node.is_floating_point()) {
                    target[name] = node.as_floating_point()->get();
                } else {
                    problems.push_back("option '" + full + "' must be a number, not a " + tomlTypeName(node) + " (default kept)");
                }
                break;
            case sol::type::string:
                if (node.is_string()) {
                    target[name] = node.as_string()->get();
                } else {
                    problems.push_back("option '" + full + "' must be a string, not a " + tomlTypeName(node) + " (default kept)");
                }
                break;
            case sol::type::table:
                if (const toml::table* sub = node.as_table()) {
                    sol::table child = current.as<sol::table>();
                    merge(lua, *sub, child, full, problems);
                } else {
                    problems.push_back("option '" + full + "' must be a table (default kept)");
                }
                break;
            default:
                problems.push_back("option '" + full + "' has a default of an unsupported type (ignored)");
                break;
        }
    }
}

}  // namespace

sol::table loadModConfig(sol::state_view lua, const std::filesystem::path& file, const sol::table& defaults, std::vector<std::string>& problems,
                         const std::filesystem::path& overrides) {
    sol::table out = copyTable(lua, defaults);
    for (const std::filesystem::path* source : {&file, &overrides}) {
        std::error_code ec;
        if (source->empty() || !std::filesystem::exists(*source, ec)) {
            continue;
        }
        try {
            const toml::table parsed = toml::parse_file(source->string());
            merge(lua, parsed, out, "", problems);
        } catch (const toml::parse_error& e) {
            problems.push_back(std::string("config file ") + source->filename().string() + ": " + std::string(e.description()) + " (line " +
                               std::to_string(e.source().begin.line) + "); " + (source == &file ? "defaults used" : "ignored"));
        }
    }
    return out;
}

void bindConfigModApi(sol::state_view lua, sol::table api, const lua::ModInfo& mod) {
    (void)lua;
    // risen.config(defaults) -> options, problems: the mod's options. `problems` is a list of texts (also written to the log).
    api.set_function("config", [dir = mod.dir, id = mod.id](sol::this_state ts, sol::table defaults) {
        sol::state_view L(ts);
        std::vector<std::string> problems;
        sol::table options = loadModConfig(L, dir / "config.toml", defaults, problems, optionsStorePath(id));
        sol::table list = L.create_table();
        int i = 1;
        for (const std::string& p : problems) {
            spdlog::warn("config: [{}] {}", id, p);
            list[i++] = p;
        }
        return std::make_tuple(options, list);
    });
}

}  // namespace spass::game
