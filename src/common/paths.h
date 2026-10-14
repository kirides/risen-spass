#pragma once

#include <filesystem>

#include <windows.h>

namespace spass::paths {

// Must be called once from DllMain with our own module handle.
void init(HMODULE self);

// Directory containing Risen.exe (and our proxy DLL).
const std::filesystem::path& binDir();

// <binDir>/spass : logs, config.toml and the mods directory live here.
const std::filesystem::path& dataDir();

// <dataDir>/mods : every sub-directory containing a mod.toml is a mod.
const std::filesystem::path& modsDir();

}  // namespace spass::paths
