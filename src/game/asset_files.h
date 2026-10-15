#pragma once

#include <string>

namespace spass::game {

// Writes <spass>/generated/<mod>/<rel> (confined to that directory, atomic) and makes the engine's name cache see it (the directory is mounted on the
// first write and remounted lazily after later ones). false and a reason in `error` on failure. (lua_assets.cpp)
bool writeGeneratedAsset(const std::string& mod, const std::string& rel, const std::string& data, std::string& error);

// Remounts the generated directories that received files since they were mounted, so the engine finds them; returns how many. (lua_assets.cpp)
int refreshAssetMounts();

}  // namespace spass::game
