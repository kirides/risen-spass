// The UI font: a system sans-serif font when one is found (more readable than ImGui's built-in bitmap font), else the built-in one.
#include <imgui.h>

#include <filesystem>
#include <string>
#include <vector>

#include "core/log.h"

namespace mk {

void load_fonts() {
    ImGuiIO& io = ImGui::GetIO();
    std::vector<std::string> candidates;
#ifdef _WIN32
    if (const char* windir = std::getenv("WINDIR")) {
        candidates.push_back(std::string(windir) + "\\Fonts\\segoeui.ttf");
        candidates.push_back(std::string(windir) + "\\Fonts\\arial.ttf");
    }
#elif defined(__APPLE__)
    candidates = {"/System/Library/Fonts/Supplemental/Arial.ttf", "/Library/Fonts/Arial.ttf", "/System/Library/Fonts/Geneva.ttf"};
#else
    candidates = {"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "/usr/share/fonts/TTF/DejaVuSans.ttf", "/usr/share/fonts/dejavu/DejaVuSans.ttf",
                  "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf", "/usr/share/fonts/noto/NotoSans-Regular.ttf"};
#endif
    std::error_code ec;
    for (auto& c : candidates) {
        if (!std::filesystem::exists(c, ec)) continue;
        if (io.Fonts->AddFontFromFileTTF(c.c_str(), 17.0f)) {
            log_debug("font: " + c);
            return;
        }
    }
    io.Fonts->AddFontDefault();
}

}  // namespace mk
