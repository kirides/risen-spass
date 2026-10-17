// Pictures -> the engine's compiled images (docs/editor/REQUIREMENTS.md AST-3). A `._ximg` is "GR01" "IM04", a 0x28-byte header, the property stream
// of an eCImageResource2 (Width, Height, SkipMips, PixelFormat = a D3D format) and an ordinary DDS (RE.md sections 26, 49). Textures are written
// as DXT1 (opaque) or DXT5 (with alpha) with a full mip chain, like the shipped ones; GUI images (the icon atlas) as uncompressed A8R8G8B8 without
// mips, like GUI_IconSets01. Item icons of a project become cells of one atlas plus an XML image list (`.ximl`) that the runtime registers with
// risen.assets.load_icons.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace mk {

struct Picture {
    int width = 0, height = 0;
    std::vector<uint8_t> rgba;  // width * height * 4, rows top to bottom
    bool has_alpha() const;
};

bool is_picture(const std::string& file);  // .png .tga .jpg .jpeg .bmp
// PNG, TGA, JPG, BMP; nullopt + `error` otherwise
std::optional<Picture> load_picture(const std::string& bytes, std::string& error);
Picture resize_picture(const Picture& p, int width, int height);

enum class XimgKind {
    Texture,  // DXT1 / DXT5 by alpha, mip chain; the size is brought to powers of two (at most 2048)
    Gui,      // A8R8G8B8, no mips, size as it is
};
// the compiled image; `src_ext` is the label of the source file the engine stores (".tga", ".dds")
std::string build_ximg(const Picture& p, XimgKind kind, const std::string& src_ext = ".dds");

struct IconAtlas {
    Picture texture;
    std::string ximl;  // the image list
    std::vector<std::pair<std::string, std::array<int, 4>>> cells;  // name -> left, top, right, bottom
};
// one texture with a `cell` x `cell` square per icon (scaled), the smallest power of two that holds them; `texture_name` is the raw name the
// list refers to ("X.dds" for the compiled X._ximg)
IconAtlas build_icon_atlas(const std::vector<std::pair<std::string, Picture>>& icons, const std::string& texture_name, int cell = 40);

// the GUI bitmap name of a project picture used as an icon: "IC_<mod id>_<file stem>"
std::string project_icon_name(const std::string& mod_id, const std::string& picture_file);
// the compiled image a project picture becomes: "<stem>._ximg"
std::string picture_compiled_name(const std::string& picture_file);

}  // namespace mk
