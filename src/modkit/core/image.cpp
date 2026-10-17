#include "core/image.h"

#include <algorithm>
#include <cstring>

#include "pack2mod/util.h"

#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_ONLY_PNG
#define STBI_ONLY_TGA
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#include <stb_image.h>
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include <stb_image_resize2.h>
#define STB_DXT_IMPLEMENTATION
#include <stb_dxt.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

namespace mk {

namespace {

// the property stream of an eCImageResource2 as GUI_IconSets01._ximg has it; the values are patched in, the 32 bytes of the object's own data
// after the properties are 0 (they are only set with SkipMips, which these files do not use)
const unsigned char kImageStream[225] = {
    0x01, 0x00, 0x01, 0x01, 0x00, 0x01, 0x10, 0x00, 0x65, 0x43, 0x49, 0x6d, 0x61, 0x67, 0x65, 0x52, 0x65, 0x73, 0x6f, 0x75, 0x72, 0x63, 0x65, 0x32,
    0x01, 0x00, 0x00, 0xc9, 0x00, 0xc9, 0x00, 0xbe, 0x00, 0x00, 0x00, 0xc9, 0x00, 0x04, 0x00, 0x00, 0x00, 0x05, 0x00, 0x57, 0x69, 0x64, 0x74, 0x68,
    0x03, 0x00, 0x69, 0x6e, 0x74, 0x1e, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x06, 0x00, 0x48, 0x65, 0x69, 0x67, 0x68, 0x74, 0x03,
    0x00, 0x69, 0x6e, 0x74, 0x1e, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x08, 0x00, 0x53, 0x6b, 0x69, 0x70, 0x4d, 0x69, 0x70, 0x73,
    0x04, 0x00, 0x6c, 0x6f, 0x6e, 0x67, 0x1e, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x50, 0x69, 0x78, 0x65, 0x6c, 0x46,
    0x6f, 0x72, 0x6d, 0x61, 0x74, 0x34, 0x00, 0x62, 0x54, 0x50, 0x72, 0x6f, 0x70, 0x65, 0x72, 0x74, 0x79, 0x43, 0x6f, 0x6e, 0x74, 0x61, 0x69, 0x6e,
    0x65, 0x72, 0x3c, 0x65, 0x6e, 0x75, 0x6d, 0x20, 0x65, 0x43, 0x47, 0x66, 0x78, 0x53, 0x68, 0x61, 0x72, 0x65, 0x64, 0x3a, 0x3a, 0x65, 0x45, 0x43,
    0x6f, 0x6c, 0x6f, 0x72, 0x46, 0x6f, 0x72, 0x6d, 0x61, 0x74, 0x3e, 0x1e, 0x00, 0x06, 0x00, 0x00, 0x00, 0xc9, 0x00, 0x15, 0x00, 0x00, 0x00, 0xc9,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};
constexpr size_t kWidthAt = 0x3b, kHeightAt = 0x52, kSkipMipsAt = 0x6c, kFormatAt = 0xbb;
constexpr uint32_t kFmtA8R8G8B8 = 21;
constexpr uint32_t fourcc(const char* s) {
    return static_cast<uint32_t>(s[0]) | (static_cast<uint32_t>(s[1]) << 8) | (static_cast<uint32_t>(s[2]) << 16) | (static_cast<uint32_t>(s[3]) << 24);
}

void put32(std::string& s, size_t at, uint32_t v) {
    for (int i = 0; i < 4; ++i) s[at + i] = static_cast<char>((v >> (8 * i)) & 0xff);
}
void append32(std::string& s, uint32_t v) {
    for (int i = 0; i < 4; ++i) s.push_back(static_cast<char>((v >> (8 * i)) & 0xff));
}

int pow2_at_most(int v, int cap) {
    int p = 1;
    while (p * 2 <= v && p * 2 <= cap) p *= 2;
    return p;
}
int pow2_near(int v, int cap) {
    const int lo = pow2_at_most(std::max(v, 1), cap);
    const int hi = std::min(lo * 2, cap);
    return (v - lo <= hi - v) ? lo : hi;
}

// a 2x2 box filter
Picture half(const Picture& p) {
    Picture h;
    h.width = std::max(1, p.width / 2);
    h.height = std::max(1, p.height / 2);
    h.rgba.resize(static_cast<size_t>(h.width) * h.height * 4);
    for (int y = 0; y < h.height; ++y)
        for (int x = 0; x < h.width; ++x)
            for (int c = 0; c < 4; ++c) {
                int sum = 0, n = 0;
                for (int dy = 0; dy < 2; ++dy)
                    for (int dx = 0; dx < 2; ++dx) {
                        const int sx = std::min(x * 2 + dx, p.width - 1), sy = std::min(y * 2 + dy, p.height - 1);
                        sum += p.rgba[(static_cast<size_t>(sy) * p.width + sx) * 4 + c];
                        ++n;
                    }
                h.rgba[(static_cast<size_t>(y) * h.width + x) * 4 + c] = static_cast<uint8_t>((sum + n / 2) / n);
            }
    return h;
}

std::string dxt_level(const Picture& p, bool alpha) {
    std::string out;
    const int bw = (p.width + 3) / 4, bh = (p.height + 3) / 4;
    unsigned char block[64], dst[16];
    for (int by = 0; by < bh; ++by)
        for (int bx = 0; bx < bw; ++bx) {
            for (int y = 0; y < 4; ++y)
                for (int x = 0; x < 4; ++x) {
                    const int sx = std::min(bx * 4 + x, p.width - 1), sy = std::min(by * 4 + y, p.height - 1);
                    std::memcpy(block + (y * 4 + x) * 4, &p.rgba[(static_cast<size_t>(sy) * p.width + sx) * 4], 4);
                }
            stb_compress_dxt_block(dst, block, alpha ? 1 : 0, STB_DXT_HIGHQUAL);
            out.append(reinterpret_cast<const char*>(dst), alpha ? 16 : 8);
        }
    return out;
}

// DDS header (128 bytes) as the shipped files have it: no pitch / linear size flag, pitch 0
std::string dds_header(int w, int h, int mips, bool compressed, uint32_t fourcc_value) {
    std::string d(128, '\0');
    std::memcpy(d.data(), "DDS ", 4);
    put32(d, 4, 124);
    put32(d, 8, 0x1007u | (mips > 1 ? 0x20000u : 0u));
    put32(d, 12, static_cast<uint32_t>(h));
    put32(d, 16, static_cast<uint32_t>(w));
    put32(d, 28, mips > 1 ? static_cast<uint32_t>(mips) : 0u);
    put32(d, 76, 32);
    if (compressed) {
        put32(d, 80, 0x4);  // DDPF_FOURCC
        put32(d, 84, fourcc_value);
    } else {
        put32(d, 80, 0x41);  // DDPF_RGB | DDPF_ALPHAPIXELS
        put32(d, 88, 32);
        put32(d, 92, 0x00ff0000);
        put32(d, 96, 0x0000ff00);
        put32(d, 100, 0x000000ff);
        put32(d, 104, 0xff000000);
    }
    put32(d, 108, mips > 1 ? 0x401008u : 0x1002u);  // TEXTURE | MIPMAP | COMPLEX, or TEXTURE | ALPHA
    return d;
}

}  // namespace

bool Picture::has_alpha() const {
    for (size_t i = 3; i < rgba.size(); i += 4)
        if (rgba[i] != 255) return true;
    return false;
}

bool is_picture(const std::string& file) {
    const std::string n = p2m::lower(file);
    for (const char* e : {".png", ".tga", ".jpg", ".jpeg", ".bmp"})
        if (p2m::ends_with(n, e)) return true;
    return false;
}

std::optional<Picture> load_picture(const std::string& bytes, std::string& error) {
    int w = 0, h = 0, n = 0;
    unsigned char* px = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(bytes.data()), static_cast<int>(bytes.size()), &w, &h, &n, 4);
    if (!px) {
        error = std::string("not a picture the editor reads (PNG, TGA, JPG, BMP): ") + (stbi_failure_reason() ? stbi_failure_reason() : "?");
        return std::nullopt;
    }
    Picture p;
    p.width = w;
    p.height = h;
    p.rgba.assign(px, px + static_cast<size_t>(w) * h * 4);
    stbi_image_free(px);
    if (w > 8192 || h > 8192) {
        error = "the picture is larger than 8192 pixels";
        return std::nullopt;
    }
    return p;
}

Picture resize_picture(const Picture& p, int width, int height) {
    if (p.width == width && p.height == height) return p;
    Picture r;
    r.width = width;
    r.height = height;
    r.rgba.resize(static_cast<size_t>(width) * height * 4);
    stbir_resize_uint8_srgb(p.rgba.data(), p.width, p.height, 0, r.rgba.data(), width, height, 0, STBIR_RGBA);
    return r;
}

std::string build_ximg(const Picture& src, XimgKind kind, const std::string& src_ext) {
    std::string payload;
    uint32_t format = kFmtA8R8G8B8;
    int w = src.width, h = src.height;
    if (kind == XimgKind::Texture) {
        w = pow2_near(src.width, 2048);
        h = pow2_near(src.height, 2048);
        Picture level = resize_picture(src, w, h);
        const bool alpha = level.has_alpha();
        format = fourcc(alpha ? "DXT5" : "DXT1");
        int mips = 1;
        for (int s = std::max(w, h); s > 1; s /= 2) ++mips;
        payload = dds_header(w, h, mips, true, format);
        for (int i = 0; i < mips; ++i) {
            payload += dxt_level(level, alpha);
            if (i + 1 < mips) level = half(level);
        }
    } else {
        payload = dds_header(w, h, 1, false, 0);
        payload.reserve(payload.size() + src.rgba.size());
        for (size_t i = 0; i < src.rgba.size(); i += 4) {  // B, G, R, A in memory (masks R 0xff0000 ...)
            payload.push_back(static_cast<char>(src.rgba[i + 2]));
            payload.push_back(static_cast<char>(src.rgba[i + 1]));
            payload.push_back(static_cast<char>(src.rgba[i + 0]));
            payload.push_back(static_cast<char>(src.rgba[i + 3]));
        }
    }
    std::string stream(reinterpret_cast<const char*>(kImageStream), sizeof kImageStream);
    put32(stream, kWidthAt, static_cast<uint32_t>(w));
    put32(stream, kHeightAt, static_cast<uint32_t>(h));
    put32(stream, kSkipMipsAt, 0);
    put32(stream, kFormatAt, format);
    std::string out = "GR01IM04";
    append32(out, 0x28);
    append32(out, static_cast<uint32_t>(stream.size()));
    append32(out, static_cast<uint32_t>(0x28 + stream.size()));
    append32(out, static_cast<uint32_t>(payload.size()));
    out.append(8, '\0');  // file time: fixed, so the same picture gives the same file
    std::string ext = src_ext.substr(0, 4);
    ext.resize(4, '\0');
    out += ext;
    out += std::string("\0\xfe\xfe\xfe", 4);
    out += stream;
    out += payload;
    return out;
}

IconAtlas build_icon_atlas(const std::vector<std::pair<std::string, Picture>>& icons, const std::string& texture_name, int cell) {
    IconAtlas a;
    int size = 64;
    while ((size / cell) * (size / cell) < static_cast<int>(icons.size()) && size < 4096) size *= 2;
    const int per_row = std::max(1, size / cell);
    a.texture.width = a.texture.height = size;
    a.texture.rgba.assign(static_cast<size_t>(size) * size * 4, 0);
    std::string images;
    for (size_t i = 0; i < icons.size(); ++i) {
        const int cx = static_cast<int>(i % per_row) * cell, cy = static_cast<int>(i / per_row) * cell;
        const Picture p = resize_picture(icons[i].second, cell, cell);
        for (int y = 0; y < cell; ++y)
            std::memcpy(&a.texture.rgba[(static_cast<size_t>(cy + y) * size + cx) * 4], &p.rgba[static_cast<size_t>(y) * cell * 4], static_cast<size_t>(cell) * 4);
        const std::array<int, 4> r{cx, cy, cx + cell, cy + cell};
        a.cells.push_back({icons[i].first, r});
        images += "\t\t<Image\n\t\t\tClassName=\"eCGuiImage2\"\n\t\t\tImageName=\"" + icons[i].first + "\"\n\t\t\tRectOnTexture=\"" + std::to_string(r[0]) + "; " +
                  std::to_string(r[1]) + "; " + std::to_string(r[2]) + "; " + std::to_string(r[3]) +
                  "\"\n\t\t\tBlendMode=\"eEImageBlend_AlphaBlend\"\n\t\t>\n\t\t</Image>\n";
    }
    a.ximl = "<ImageList\n\tClassName=\"eCGuiImageList2\"\n\tImageSize=\"0; 0\"\n\tTextureName=\"" + texture_name + "\"\n\t>\n\t<Images>\n" + images +
             "\t</Images>\n</ImageList>\n";
    return a;
}

std::string project_icon_name(const std::string& mod_id, const std::string& picture_file) {
    std::string stem = p2m::bare_name(picture_file);
    if (const auto dot = stem.rfind('.'); dot != std::string::npos) stem.erase(dot);
    return "IC_" + mod_id + "_" + stem;
}

std::string picture_compiled_name(const std::string& picture_file) {
    std::string stem = p2m::bare_name(picture_file);
    if (const auto dot = stem.rfind('.'); dot != std::string::npos) stem.erase(dot);
    return stem + "._ximg";
}

}  // namespace mk
