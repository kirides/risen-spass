// "Play": the exported mod is tried in the game from the editor. Starts Risen windowed (the layer in <Risen>\bin), optionally straight into a
// new game or the last quick save, or, when the game already runs with the file watcher on, lets the layer hot-reload the mod after the export.
#pragma once

#include <string>
#include <vector>

namespace mk {

enum class PlayStart { Menu, NewGame, QuickLoad };

struct PlayOptions {
    std::string game_dir;  // the Risen root (contains bin\Risen.exe)
    PlayStart start = PlayStart::NewGame;
    bool force_windowed = true;  // data\ini\ConfigLocal.xml: FullScreen="false" (a backup is kept next to it)
};

struct PlayPlan {
    std::string mods_dir;    // <game_dir>\bin\spass\mods: where the export goes
    std::string log_file;    // <game_dir>\bin\spass\spass.log
    std::string problem;     // not empty: Play is not possible (missing Risen.exe / layer)
};

// Where things are and whether the folder looks like a Risen install with the layer.
PlayPlan play_plan(const std::string& game_dir);

// a Risen.exe of this install is running
bool game_running(const std::string& game_dir);

// ---- the pure parts (unit-tested) ----
// config.toml of the layer with `[mods] watch = true` (hot reload) and, when `app_data` is not empty, nothing else changed
std::string config_with_watch(const std::string& text);
// ConfigLocal.xml with FullScreen="false"; `changed` tells whether it differed
std::string config_local_windowed(const std::string& xml, bool& changed);
// the tiny helper mod that starts a game on the first tick: {file name, content}
std::vector<std::pair<std::string, std::string>> autostart_mod_files(PlayStart start);
inline constexpr const char* kAutostartModId = "modkit_autostart";

struct PlayResult {
    bool ok = false;
    bool reloaded = false;  // the game was running: the layer reloads the mod from the exported files
    std::string message;
};

// Call after the export succeeded. Prepares config / autostart / windowed mode and starts the game, or reports a running one.
PlayResult play_game(const PlayOptions& o);

}  // namespace mk
