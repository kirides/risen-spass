#pragma once

#include <string>

namespace spass::game {

// Mounts the physical directory `physicalPath` into the engine's virtual file system under `virtualPath`, the way
// eCVirtualFileSystem::Mount does for the entries of mountlist_packed.ini (absolute virtual path, directory created,
// Windows separators). A mounted directory also acts as an overlay root: a file `a/b.txt` inside it is found when the
// engine opens the relative path `a/b.txt`, and shadows a packed file with the same path (docs/RE.md §16).
bool mountDirectory(const std::string& virtualPath, const std::string& physicalPath);
bool unmountDirectory(const std::string& virtualPath);

// Hooks eCVirtualFileSystem::ProcessMountList so that every mod with `data_overlay = true` is mounted right after the
// game's own mount list, i.e. before the engine loads any resource (experimental, see docs/RE.md §17). Call from installHooks().
void installModMountHook();

}  // namespace spass::game
