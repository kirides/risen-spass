#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace spass::lua {

// Generation of the Lua API in this build. Bumped when an existing function changes in an incompatible way; a mod that asks for
// a newer one (`api_version` in mod.toml) is not loaded, so it fails with a clear message instead of misbehaving.
inline constexpr int kApiVersion = 1;

// A mod is a directory containing mod.toml:
//
//   id          = "my_mod"            # required, [a-z0-9_]+
//   name        = "My Mod"            # optional
//   version     = "1.0.0"             # optional
//   author      = "someone"           # optional
//   description = "..."               # optional
//   main        = "main.lua"          # optional, default "main.lua"
//   load_after  = ["other_mod"]       # optional ordering constraints
//   requires    = ["base_mod"]        # optional hard dependencies (mod is skipped if missing)
//   api_version = 1                   # optional: the Lua API generation the mod was written for (a newer one is refused)
//   data_overlay = false              # experimental: mount the mod directory into the virtual file system at startup
struct ModInfo {
    std::string id;
    std::string name;
    std::string version;
    std::string author;
    std::string description;
    std::string main = "main.lua";
    std::vector<std::string> loadAfter;
    std::vector<std::string> requires_;
    bool dataOverlay = false;
    int apiVersion = 1;
    std::filesystem::path dir;
};

// Scans `root` for mods. Invalid manifests are logged and skipped.
std::vector<ModInfo> discoverMods(const std::filesystem::path& root);

// Orders mods so that every `load_after`/`requires` target comes first (ties broken by id).
// Mods with missing requirements or dependency cycles are dropped (and logged).
std::vector<ModInfo> resolveLoadOrder(std::vector<ModInfo> mods);

}  // namespace spass::lua
