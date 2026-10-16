#pragma once

#include <filesystem>
#include <map>
#include <optional>
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
//   assets      = "assets"            # directory of loose resource files (meshes, materials, images ...) mounted at startup;
//                                     # false = none (docs/LUA_API.md "New meshes, textures and icons")
//   retire      = { npcs = [...], quests = [...], ... }   # content older versions of the mod made that is cleaned up
//                                     # (the same table as risen.retire, docs/LUA_API.md "Updating and removing content")
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
    std::string assets = "assets";  // directory (relative to the mod directory) of loose resource files; empty = none
    int apiVersion = 1;
    std::filesystem::path dir;
    std::map<std::string, std::vector<std::string>> retire;  // the `retire` table of mod.toml: kind ("npcs", "quests", ...) -> names
};

// A mod folder that could not become a mod (bad manifest, newer API generation, ...).
struct ModIssue {
    std::string folder;
    std::string id;      // when the manifest got far enough to name one
    std::string reason;
};

// Reads one mod.toml. On failure returns nullopt and puts the reason in `error`.
std::optional<ModInfo> readManifest(const std::filesystem::path& modDir, std::string& error);

// Scans `root` for mods. Invalid manifests are logged and skipped (and described in `issues` when given).
std::vector<ModInfo> discoverMods(const std::filesystem::path& root, std::vector<ModIssue>* issues = nullptr);

// The user's choices outside the mods (<spass>/mods.toml): which mods are switched off and an optional load order.
//
//   order = ["base", "extras"]        # optional: wanted load order (constraints of requires / load_after always win)
//   [enabled]
//   some_mod = false                  # false = not loaded; absent or true = loaded
struct UserConfig {
    std::map<std::string, bool> enabled;
    std::vector<std::string> order;

    bool isEnabled(const std::string& id) const {
        const auto it = enabled.find(id);
        return it == enabled.end() || it->second;
    }
    bool empty() const { return enabled.empty() && order.empty(); }
};

// Reads the file (a missing file is an empty configuration; a damaged one is an empty configuration plus a problem text).
UserConfig loadUserConfig(const std::filesystem::path& file, std::vector<std::string>& problems);
std::string serializeUserConfig(const UserConfig& cfg);
bool saveUserConfig(const std::filesystem::path& file, const UserConfig& cfg, std::string& error);

// Why a mod is not part of the load plan.
struct SkippedMod {
    ModInfo mod;
    std::string kind;    // "disabled" | "missing_dependency" | "dependency_unavailable" | "cycle"
    std::string reason;
};

struct LoadPlan {
    std::vector<ModInfo> ordered;
    std::vector<SkippedMod> skipped;
    std::vector<std::string> warnings;  // entries of the custom order that could not be honoured
};

// Decides what loads and in which order: switched-off mods and mods whose `requires` cannot be met (transitively) are skipped with a reason,
// the rest is ordered so that every `load_after`/`requires` target comes first. Ties are broken by the custom order of `cfg` (listed mods first, in
// the listed order), then by id; without a custom order the result is alphabetical by id. A custom order can never put a mod before its dependencies
// (the dependency wins and the conflict is reported as a warning). Dependency cycles are dropped (and logged).
LoadPlan planLoad(std::vector<ModInfo> mods, const UserConfig& cfg);

// planLoad() with an empty configuration (kept for callers and tests that only care about the order).
std::vector<ModInfo> resolveLoadOrder(std::vector<ModInfo> mods);

// "" when `order` could be used as a custom load order for `mods`; else why not (unknown id, duplicate, a mod listed before something it requires or loads after).
std::string checkOrder(const std::vector<ModInfo>& mods, const std::vector<std::string>& order);

// The mods (by id, in dependency order) that `requires` the mod `id`, directly or through others. Only mods in `mods` are considered.
std::vector<std::string> dependentsOf(const std::vector<ModInfo>& mods, const std::string& id);

// Version strings: "1.2.3", "1.10", "2.0.0-beta" compare by their dot separated numbers, then a suffix after '-' sorts before the plain version.
// Returns -1, 0, 1.
int compareVersions(const std::string& a, const std::string& b);
// Whether `version` satisfies `spec`: "1.2.0" (equal), "<2.0.0", "<=1.5", ">1.0", ">=1.0", "!=1.0", "*" (any), or several separated by a comma (all must hold: ">=1.0,<2.0").
// nullopt when `spec` is not a valid specification.
std::optional<bool> versionMatches(const std::string& spec, const std::string& version);

}  // namespace spass::lua
