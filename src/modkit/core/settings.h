// Per-user settings of the editor (config_dir()/settings.json): where the game data is, recent projects, UI scale.
#pragma once

#include <string>
#include <vector>

namespace mk {

struct Settings {
    std::string data_dir;    // <Risen>\data (holds common\*.pak and compiled\*.pak)
    std::string export_dir;  // usually <Risen>\bin\spass\mods
    float ui_scale = 1.0f;
    std::string language = "auto";  // UI language (core/i18n.h): "auto" = the system's, "en", or the code of a lang/*.toml file
    std::vector<std::string> recent;  // most recent first
    std::string world = "World";
    std::string game_dir;                // the Risen folder Play starts (bin\Risen.exe); Play exports to its bin\spass\mods
    int play_start = 1;                  // PlayStart: 0 main menu, 1 new game, 2 last quick save
    bool play_windowed = true;           // Play sets FullScreen="false" in <game_dir>\data\ini\ConfigLocal.xml
    bool side_collapsed = false;         // the left column (Mod, Browser) folded into vertical tabs (Window > Collapse the side panels)
    // voice recordings (core/audio_convert.h): the ffmpeg to run ("" = ffmpeg on PATH) and how files are converted
    std::string ffmpeg;
    bool voice_mono = true, voice_normalize = false;
    int voice_rate = 44100;

    void add_recent(const std::string& path);
    static Settings load();  // defaults when the file is missing or broken (logged)
    void save() const;       // logged on failure, never throws
};

}  // namespace mk
