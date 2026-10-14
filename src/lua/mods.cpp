#include "lua/mods.h"

#include <algorithm>
#include <map>
#include <set>

#include <spdlog/spdlog.h>
#include <toml++/toml.hpp>

namespace spass::lua {
namespace {

bool validId(const std::string& id) {
    return !id.empty() && std::all_of(id.begin(), id.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
    });
}

std::vector<std::string> readStringArray(const toml::table& t, std::string_view key) {
    std::vector<std::string> out;
    if (const auto* arr = t[key].as_array()) {
        for (const auto& el : *arr) {
            if (auto s = el.value<std::string>()) {
                out.push_back(*s);
            }
        }
    }
    return out;
}

}  // namespace

std::vector<ModInfo> discoverMods(const std::filesystem::path& root) {
    std::vector<ModInfo> mods;
    std::error_code ec;
    if (!std::filesystem::is_directory(root, ec)) {
        spdlog::info("mods: directory {} does not exist, no mods loaded", root.string());
        return mods;
    }
    for (const auto& entry : std::filesystem::directory_iterator(root, ec)) {
        if (!entry.is_directory()) {
            continue;
        }
        const auto manifest = entry.path() / "mod.toml";
        if (!std::filesystem::exists(manifest)) {
            continue;
        }
        try {
            const toml::table t = toml::parse_file(manifest.string());
            ModInfo m;
            m.id = t["id"].value_or(std::string{});
            if (!validId(m.id)) {
                spdlog::error("mods: {}: missing or invalid 'id' (allowed: a-z 0-9 _)", manifest.string());
                continue;
            }
            m.name = t["name"].value_or(m.id);
            m.version = t["version"].value_or(std::string{"0.0.0"});
            m.author = t["author"].value_or(std::string{});
            m.description = t["description"].value_or(std::string{});
            m.main = t["main"].value_or(std::string{"main.lua"});
            m.loadAfter = readStringArray(t, "load_after");
            m.requires_ = readStringArray(t, "requires");
            m.dir = entry.path();
            mods.push_back(std::move(m));
        } catch (const toml::parse_error& e) {
            spdlog::error("mods: {}: {}", manifest.string(), e.description());
        }
    }
    std::sort(mods.begin(), mods.end(), [](const ModInfo& a, const ModInfo& b) { return a.id < b.id; });
    return mods;
}

std::vector<ModInfo> resolveLoadOrder(std::vector<ModInfo> mods) {
    std::map<std::string, ModInfo> byId;
    for (auto& m : mods) {
        if (!byId.emplace(m.id, m).second) {
            spdlog::error("mods: duplicate id '{}' ({}) ignored", m.id, m.dir.string());
        }
    }

    // Drop mods whose hard requirements are missing (transitively).
    for (bool changed = true; changed;) {
        changed = false;
        for (auto it = byId.begin(); it != byId.end();) {
            const auto missing = std::find_if(it->second.requires_.begin(), it->second.requires_.end(),
                                              [&](const std::string& r) { return !byId.contains(r); });
            if (missing != it->second.requires_.end()) {
                spdlog::error("mods: '{}' requires '{}' which is not available, skipping", it->first, *missing);
                it = byId.erase(it);
                changed = true;
            } else {
                ++it;
            }
        }
    }

    std::vector<ModInfo> ordered;
    std::set<std::string> done;
    while (done.size() < byId.size()) {
        bool progressed = false;
        for (const auto& [id, m] : byId) {  // std::map iterates in id order => deterministic
            if (done.contains(id)) {
                continue;
            }
            const auto ready = [&](const std::vector<std::string>& deps) {
                return std::all_of(deps.begin(), deps.end(), [&](const std::string& d) {
                    return done.contains(d) || !byId.contains(d);  // load_after on a missing mod is ignored
                });
            };
            if (ready(m.requires_) && ready(m.loadAfter)) {
                ordered.push_back(m);
                done.insert(id);
                progressed = true;
                break;  // restart so ordering stays "smallest ready id first"
            }
        }
        if (!progressed) {
            for (const auto& [id, m] : byId) {
                if (!done.contains(id)) {
                    spdlog::error("mods: '{}' is part of a dependency cycle, skipping", id);
                }
            }
            break;
        }
    }
    return ordered;
}

}  // namespace spass::lua
