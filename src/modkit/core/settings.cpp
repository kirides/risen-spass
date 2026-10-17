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
        s.world = j.value("world", std::string("World"));
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
    j["world"] = world;
    j["recent"] = recent;
    try {
        write_text(config_dir() / "settings.json", j.dump(2) + "\n");
    } catch (const std::exception& e) {
        log_warn(std::string("settings: cannot save: ") + e.what());
    }
}

}  // namespace mk
