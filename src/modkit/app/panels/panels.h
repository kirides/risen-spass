// Factories of the panels (one file each).
#pragma once

#include <memory>

#include "app/app.h"

namespace mk {

std::unique_ptr<Panel> make_project_panel();
std::unique_ptr<Panel> make_log_panel();
std::unique_ptr<Panel> make_settings_panel();
std::unique_ptr<Panel> make_browser_panel();

// window titles, used for the default docking layout
inline constexpr const char* kPanelMod = "Mod";
inline constexpr const char* kPanelLog = "Log";
inline constexpr const char* kPanelSettings = "Settings";
inline constexpr const char* kPanelBrowser = "Browser";

}  // namespace mk
