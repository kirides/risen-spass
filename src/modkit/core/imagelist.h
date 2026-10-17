// compiled_imagelists.bin (docs/RE.md section 43 and 51): the engine's GUI image collections. Every shipped icon (items, skills, map pieces, GUI elements) is a
// named rectangle of a texture: IC_It_Apple = cells of GUI_IconSets01 (40 x 40 pixels). The editor uses this to draw icon previews wherever items are listed.
#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "pack2mod/util.h"

namespace mk {

struct ImageEntry {
    enum class Kind { Image, Split, Layered } kind = Kind::Image;
    std::string name;        // "IC_It_Apple"
    std::string collection;  // "IMC_Icons"
    std::string texture;     // the image list's texture as written: "GUI_IconSets01.dds"
    int left = 0, top = 0, right = 0, bottom = 0;  // RectOnTexture in pixels
    std::string background, overlay;                // Layered: the two images it is made of
    int width() const { return right - left; }
    int height() const { return bottom - top; }
    // the compiled texture file of `texture`: "GUI_IconSets01._ximg"
    std::string texture_file() const;
};

class ImageCatalog {
public:
    // throws p2m::Error for a file that is not an image list archive; an unknown object inside is skipped (it only costs its own images)
    static ImageCatalog parse(const p2m::Bytes& file);

    const std::vector<ImageEntry>& all() const { return entries_; }
    // by name, case-insensitive (the engine lower-cases bitmap names); nullptr when unknown
    const ImageEntry* find(const std::string& name) const;
    size_t collections() const { return collections_; }

private:
    std::vector<ImageEntry> entries_;
    std::unordered_map<std::string, size_t> by_name_;  // lower-case name
    size_t collections_ = 0;
};

}  // namespace mk
