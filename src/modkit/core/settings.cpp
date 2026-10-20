#include "core/settings.h"

#include <algorithm>

#include "core/log.h"
#include "core/paths.h"
#include "core/project.h"

namespace mk {

void Settings::add_recent(const std::string& path) {
    recent.erase(std::remove(recent.begin(), recent.end(), path), recent.end());
    recent.insert(recent.begin(), path);
    if (recent.size() > 10) recent.resize(10);
}

Settings Settings::load() {
    Settings s;
    const auto file = config_dir() / "settings.json";
    std::error_code ec;
    if (!fs::exists(file, ec)) return s;
    try {
        const json j = json::parse(read_text(file));
        s.data_dir = j.value("data_dir", std::string());
        s.export_dir = j.value("export_dir", std::string());
        s.ui_scale = std::clamp(j.value("ui_scale", 1.0f), 0.5f, 3.0f);
        s.language = j.value("language", std::string("auto"));
        s.world = j.value("world", std::string("World"));
        s.game_dir = j.value("game_dir", std::string());
        s.play_start = std::clamp(j.value("play_start", 1), 0, 2);
        s.play_windowed = j.value("play_windowed", true);
        s.ffmpeg = j.value("ffmpeg", std::string());
        s.voice_mono = j.value("voice_mono", true);
        s.voice_normalize = j.value("voice_normalize", false);
        s.voice_rate = std::clamp(j.value("voice_rate", 44100), 0, 192000);
        if (auto r = j.find("recent"); r != j.end() && r->is_array())
            for (auto& e : *r)
                if (e.is_string()) s.recent.push_back(e.get<std::string>());
    } catch (const std::exception& e) {
        log_warn("settings: " + u8str(file) + " ignored: " + e.what());
    }
    return s;
}

void Settings::save() const {
    json j = json::object();
    j["data_dir"] = data_dir;
    j["export_dir"] = export_dir;
    j["ui_scale"] = ui_scale;
    j["language"] = language;
    j["world"] = world;
    j["game_dir"] = game_dir;
    j["play_start"] = play_start;
    j["play_windowed"] = play_windowed;
    j["ffmpeg"] = ffmpeg;
    j["voice_mono"] = voice_mono;
    j["voice_normalize"] = voice_normalize;
    j["voice_rate"] = voice_rate;
    j["recent"] = recent;
    try {
        write_text(config_dir() / "settings.json", j.dump(2) + "\n");
    } catch (const std::exception& e) {
        log_warn(std::string("settings: cannot save: ") + e.what());
    }
}

}  // namespace mk
