// File system helpers of the editor: UTF-8 paths, the per-user configuration directory, safety checks for export targets.
#pragma once

#include <filesystem>
#include <string>

namespace mk {

namespace fs = std::filesystem;

// UTF-8 <-> path (std::filesystem::u8path is deprecated in C++20 but is the portable way on MSVC, where narrow strings are ANSI)
fs::path u8(const std::string& s);
std::string u8str(const fs::path& p);

// %APPDATA%\risen-modkit, $XDG_CONFIG_HOME/risen-modkit (~/.config), ~/Library/Application Support/risen-modkit; created on demand
fs::path config_dir();

// whether `inner` is `outer` or below it (both made absolute and normalised; case-insensitive on Windows)
bool path_within(const fs::path& inner, const fs::path& outer);

std::string read_text(const fs::path& p);                       // throws std::runtime_error
void write_text(const fs::path& p, const std::string& text);    // creates directories; writes only when the content differs; throws
bool write_text_if_changed(const fs::path& p, const std::string& text);  // the same, returns whether it wrote

}  // namespace mk
