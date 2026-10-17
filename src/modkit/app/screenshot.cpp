// --screenshot: the window's back buffer as a PNG (the test loop looks at the UI this way).
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBIW_WINDOWS_UTF8  // the path is UTF-8
#include <stb_image_write.h>

#include <glad/glad.h>

#include <string>
#include <vector>

#include "core/log.h"
#include "core/paths.h"

namespace mk {

bool save_screenshot(const std::string& path, int width, int height) {
    std::vector<unsigned char> px(static_cast<size_t>(width) * height * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(GL_BACK);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    // OpenGL's origin is bottom-left
    std::vector<unsigned char> flipped(px.size());
    const size_t row = static_cast<size_t>(width) * 4;
    for (int y = 0; y < height; ++y) std::copy_n(&px[static_cast<size_t>(height - 1 - y) * row], row, &flipped[static_cast<size_t>(y) * row]);
    for (size_t i = 3; i < flipped.size(); i += 4) flipped[i] = 255;
    std::error_code ec;
    const auto p = u8(path);
    if (p.has_parent_path()) fs::create_directories(p.parent_path(), ec);
    const bool ok = stbi_write_png(path.c_str(), width, height, 4, flipped.data(), static_cast<int>(row)) != 0;
    if (ok) log_info("screenshot: " + path);
    else log_error("screenshot: cannot write " + path);
    return ok;
}

}  // namespace mk
