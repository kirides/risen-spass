#pragma once

#include <filesystem>

namespace spass::game {

// Makes the game see `dir` as %LOCALAPPDATA% and %APPDATA% (SHGetFolderPathA / SHGetSpecialFolderPathA, which is how
// the engine expands "$(localappdata)" in mountlist_packed.ini and where it keeps ConfigUser.xml, savegames and
// screenshots). Development aid for isolated test copies (config.toml [debug] app_data). Call before enableAll().
bool installFolderRedirect(const std::filesystem::path& dir);

}  // namespace spass::game
