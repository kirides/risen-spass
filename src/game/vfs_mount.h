#pragma once

#include <string>
#include <vector>

namespace spass::game {

// Mounts the physical directory `physicalPath` into the engine's virtual file system under `virtualPath`, the way
// eCVirtualFileSystem::Mount does for the entries of mountlist_packed.ini (absolute virtual path, directory created,
// Windows separators). A mounted directory also acts as an overlay root: a file `a/b.txt` inside it is found when the
// engine opens the relative path `a/b.txt`, and shadows a packed file with the same path (docs/RE.md §16).
bool mountDirectory(const std::string& virtualPath, const std::string& physicalPath);
bool unmountDirectory(const std::string& virtualPath);

// A directory of loose resource files mounted for a mod (docs/RE.md section 26). `kind` is "assets" (the mod's `assets` directory, mounted at
// startup), "generated" (<spass>/generated/<mod>, written by risen.assets.write) or "runtime" (risen.assets.mount).
struct AssetMount {
    std::string mod;
    std::string virtualPath;
    std::string physical;
    std::string kind;
};

// Mounts `dir` for `mod` (virtual name mods/<mod>/<kind>[/<n>]) unless that physical directory is already mounted for it; true when it is mounted now.
bool mountAssetDirectory(const std::string& mod, const std::string& kind, const std::string& physicalDir);
// Unmounts and mounts the directory again so that files created since the mount become visible in the engine's name cache.
bool remountAssetDirectory(const std::string& virtualPath);
const std::vector<AssetMount>& assetMounts();

// Hooks eCVirtualFileSystem::ProcessMountList so that every mod with `data_overlay = true` is mounted right after the
// game's own mount list, i.e. before the engine loads any resource (experimental, see docs/RE.md §17). Call from installHooks().
void installModMountHook();

}  // namespace spass::game
