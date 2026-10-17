#include "core/mesh.h"

#include <algorithm>
#include <cstring>

namespace mk {

namespace {

// a "<u16 len>Name<u16 len>type 1e 00" property header in a GR01 property stream; returns the offset after it or npos
size_t find_prop(const p2m::Bytes& b, const std::string& name, const std::string& type, size_t from) {
    std::string pat;
    p2m::wr16(pat, static_cast<uint16_t>(name.size()));
    pat += name;
    p2m::wr16(pat, static_cast<uint16_t>(type.size()));
    pat += type;
    pat += std::string("\x1e\x00", 2);
    const size_t p = b.find(pat, from);
    return p == std::string::npos ? p : p + pat.size();
}

// every value of a `long` property, in file order
std::vector<uint32_t> long_values(const p2m::Bytes& b, const std::string& name, size_t end) {
    std::vector<uint32_t> v;
    for (size_t p = find_prop(b, name, "long", 0); p != std::string::npos && p + 8 <= end; p = find_prop(b, name, "long", p)) {
        if (p2m::rd32(b, p) == 4) v.push_back(p2m::rd32(b, p + 4));
    }
    return v;
}

// every value of a string property (bCString and the resource string types: u32 size, u16 length, chars)
std::vector<std::string> string_values(const p2m::Bytes& b, const std::string& name, const std::string& type, size_t end) {
    std::vector<std::string> v;
    for (size_t p = find_prop(b, name, type, 0); p != std::string::npos && p + 6 <= end; p = find_prop(b, name, type, p)) {
        const uint16_t n = p2m::rd16(b, p + 4);
        if (p + 6 + n <= b.size()) v.push_back(b.substr(p + 6, n));
    }
    return v;
}

uint32_t decl_type_size(uint8_t t) {
    switch (t) {
        case 0: return 4;   // FLOAT1
        case 1: return 8;   // FLOAT2
        case 2: return 12;  // FLOAT3
        case 3: return 16;  // FLOAT4
        case 4: return 4;   // D3DCOLOR
        case 5: return 4;   // UBYTE4
        case 6: return 4;   // SHORT2
        case 7: return 8;   // SHORT4
        case 8: return 4;   // UBYTE4N
        case 9: return 4;   // SHORT2N
        case 10: return 8;  // SHORT4N
        case 11: return 4;  // USHORT2N
        case 12: return 8;  // USHORT4N
        case 13: return 4;  // UDEC3
        case 14: return 4;  // DEC3N
        case 15: return 4;  // FLOAT16_2
        case 16: return 8;  // FLOAT16_4
    }
    return 0;
}

}  // namespace

MeshData decode_mesh(const p2m::Bytes& b) {
    if (b.size() < 0x28 || b.compare(0, 8, "GR01MS02") != 0) throw p2m::Error("not a GR01/MS02 mesh");
    const size_t payload = p2m::rd32(b, 16);
    if (payload >= b.size()) throw p2m::Error("payload outside the file");
    MeshData m;
    const auto fi = long_values(b, "FirstIndex", payload), ic = long_values(b, "IndexCount", payload);
    const auto fv = long_values(b, "FirstVertex", payload), vc = long_values(b, "VertexCount", payload);
    const auto mats = string_values(b, "MaterialName", "bCString", payload);
    if (fi.empty() || fi.size() != ic.size() || fi.size() != fv.size() || fi.size() != vc.size()) throw p2m::Error("sub-meshes are incomplete");
    for (size_t i = 0; i < fi.size(); ++i) {
        SubMesh s{i < mats.size() ? mats[i] : std::string(), fi[i], ic[i], fv[i], vc[i]};
        m.vertex_count = std::max(m.vertex_count, fv[i] + vc[i]);
        m.index_count = std::max(m.index_count, fi[i] + ic[i]);
        m.submeshes.push_back(std::move(s));
    }
    // Boundary: the last bCBox of the stream
    size_t bp = std::string::npos;
    for (size_t p = find_prop(b, "Boundary", "bCBox", 0); p != std::string::npos; p = find_prop(b, "Boundary", "bCBox", p)) bp = p;
    if (bp != std::string::npos && bp + 4 + 24 <= payload)
        for (int k = 0; k < 3; ++k) {
            m.box_min[k] = p2m::rdf(b, bp + 4 + 4 * k);
            m.box_max[k] = p2m::rdf(b, bp + 16 + 4 * k);
        }
    // the vertex declaration (stream 0)
    size_t p = payload;
    static const std::string kEnd("\xff\x00\x00\x00\x11\x00\x00\x00", 8);
    for (int n = 0;; ++n) {
        if (n > 64 || p + 8 > b.size()) throw p2m::Error("no end of the vertex declaration");
        if (b.compare(p, 8, kEnd) == 0) {
            p += 8;
            break;
        }
        const uint16_t stream = p2m::rd16(b, p), off = p2m::rd16(b, p + 2);
        const uint8_t type = static_cast<uint8_t>(b[p + 4]), usage = static_cast<uint8_t>(b[p + 6]), uidx = static_cast<uint8_t>(b[p + 7]);
        p += 8;
        if (stream != 0) continue;
        m.stride = std::max(m.stride, static_cast<uint32_t>(off + decl_type_size(type)));
        if (usage == 0 && type == 2 && m.pos < 0) m.pos = off;           // POSITION, FLOAT3
        else if (usage == 3 && type == 2 && m.normal < 0) m.normal = off;  // NORMAL, FLOAT3
        else if (usage == 10 && type == 4 && uidx == 0 && m.color < 0) m.color = off;  // COLOR 0
        else if (usage == 5 && type == 1 && uidx == 0 && m.uv < 0) m.uv = off;          // TEXCOORD 0
    }
    if (m.pos < 0) throw p2m::Error("no FLOAT3 position in the vertex declaration");
    const size_t vsz = static_cast<size_t>(m.vertex_count) * m.stride;
    // the index header: <index data size> 0..2 zero dwords <format 101|102> <1>, then the vertex data
    size_t vb = std::string::npos;
    for (const auto& [fmt, isize] : {std::pair<uint32_t, uint32_t>{102, 4}, {101, 2}}) {
        const uint32_t isz = m.index_count * isize;
        std::string pat;
        p2m::wr32(pat, isz);
        for (size_t q = b.find(pat, p); q != std::string::npos && q < p + 1024; q = b.find(pat, q + 1)) {
            for (size_t gap : {4, 8, 12}) {
                if (q + gap + 8 > b.size()) continue;
                if (p2m::rd32(b, q + gap) == fmt && p2m::rd32(b, q + gap + 4) == 1 && q + gap + 8 + vsz + isz <= b.size()) {
                    vb = q + gap + 8;
                    m.index32 = isize == 4;
                    break;
                }
            }
            if (vb != std::string::npos) break;
        }
        if (vb != std::string::npos) break;
    }
    if (vb == std::string::npos) throw p2m::Error("no index header after the vertex declaration");
    const size_t isz = static_cast<size_t>(m.index_count) * (m.index32 ? 4 : 2);
    m.vertices = b.substr(vb, vsz);
    m.indices = b.substr(vb + vsz, isz);
    return m;
}

std::vector<std::string> material_images(const p2m::Bytes& b) {
    return string_values(b, "ImageFilePath", "bCImageResourceString", b.size());
}

std::string material_texture(const p2m::Bytes& b) {
    const auto imgs = material_images(b);
    if (imgs.empty()) return {};
    std::string pick = imgs.front();
    for (auto& s : imgs)
        if (p2m::contains(p2m::lower(s), "diffuse")) {
            pick = s;
            break;
        }
    // "X.tga" -> "X._ximg"; a name without an extension is already the image name
    const auto dot = pick.rfind('.');
    return (dot == std::string::npos ? pick : pick.substr(0, dot)) + "._ximg";
}

ImageData decode_image(const p2m::Bytes& b) {
    const size_t d = b.find("DDS ");
    if (d == std::string::npos || d + 128 > b.size()) throw p2m::Error("no DDS in the image");
    ImageData img;
    const uint32_t height = p2m::rd32(b, d + 12), width = p2m::rd32(b, d + 16);
    uint32_t mips = p2m::rd32(b, d + 28);
    const uint32_t pf_flags = p2m::rd32(b, d + 80);
    const std::string fourcc = b.substr(d + 84, 4);
    const uint32_t bits = p2m::rd32(b, d + 88), rmask = p2m::rd32(b, d + 92), amask = p2m::rd32(b, d + 104);
    if (!width || !height || width > 8192 || height > 8192) throw p2m::Error("bad DDS size");
    if (mips == 0) mips = 1;
    img.width = width;
    img.height = height;
    uint32_t block = 0, bpp = 0;
    if (pf_flags & 4) {
        if (fourcc == "DXT1") img.format = ImageData::DXT1, block = 8;
        else if (fourcc == "DXT3") img.format = ImageData::DXT3, block = 16;
        else if (fourcc == "DXT5") img.format = ImageData::DXT5, block = 16;
        else throw p2m::Error("DDS format " + fourcc + " is not read");
    } else if (bits == 32) {
        img.format = amask ? (rmask == 0x00ff0000 ? ImageData::BGRA8 : ImageData::RGBA8) : ImageData::BGRX8;
        bpp = 4;
    } else if (bits == 8 && (pf_flags & 0x20000)) {
        img.format = ImageData::L8;
        bpp = 1;
    } else if (bits == 8 && (pf_flags & 2)) {
        img.format = ImageData::L8;  // alpha only: shown as grey
        bpp = 1;
    } else if (bits == 16 && (pf_flags & 0x20000)) {
        img.format = ImageData::A8L8;
        bpp = 2;
    } else {
        throw p2m::Error("DDS pixel format with " + std::to_string(bits) + " bits is not read");
    }
    size_t off = d + 128;
    uint32_t w = width, h = height;
    for (uint32_t i = 0; i < mips && w && h; ++i) {
        const size_t size = block ? static_cast<size_t>(std::max(1u, (w + 3) / 4)) * std::max(1u, (h + 3) / 4) * block : static_cast<size_t>(w) * h * bpp;
        if (off + size > b.size()) break;
        img.levels.push_back({w, h, off - (d + 128), size});
        off += size;
        w = std::max(1u, w / 2);
        h = std::max(1u, h / 2);
        if (i + 1 < mips && w == 1 && h == 1 && block == 0) break;
    }
    if (img.levels.empty()) throw p2m::Error("the DDS has no complete level");
    img.data = b.substr(d + 128, off - (d + 128));
    return img;
}

namespace {

void rgb565(uint16_t c, uint8_t* out) {
    out[0] = static_cast<uint8_t>(((c >> 11) & 31) * 255 / 31);
    out[1] = static_cast<uint8_t>(((c >> 5) & 63) * 255 / 63);
    out[2] = static_cast<uint8_t>((c & 31) * 255 / 31);
}

void color_block(const uint8_t* p, uint8_t rgba[16][4], bool dxt1) {
    const uint16_t c0 = static_cast<uint16_t>(p[0] | (p[1] << 8)), c1 = static_cast<uint16_t>(p[2] | (p[3] << 8));
    uint8_t c[4][4];
    rgb565(c0, c[0]);
    rgb565(c1, c[1]);
    c[0][3] = c[1][3] = 255;
    if (c0 > c1 || !dxt1) {
        for (int k = 0; k < 3; ++k) {
            c[2][k] = static_cast<uint8_t>((2 * c[0][k] + c[1][k]) / 3);
            c[3][k] = static_cast<uint8_t>((c[0][k] + 2 * c[1][k]) / 3);
        }
        c[2][3] = c[3][3] = 255;
    } else {
        for (int k = 0; k < 3; ++k) c[2][k] = static_cast<uint8_t>((c[0][k] + c[1][k]) / 2);
        c[2][3] = 255;
        c[3][0] = c[3][1] = c[3][2] = c[3][3] = 0;
    }
    const uint32_t bits = static_cast<uint32_t>(p[4] | (p[5] << 8) | (p[6] << 16) | (p[7] << 24));
    for (int i = 0; i < 16; ++i) std::memcpy(rgba[i], c[(bits >> (2 * i)) & 3], 4);
}

}  // namespace

std::string decompress_rgba(const ImageData& img, uint32_t level, uint32_t* ow, uint32_t* oh) {
    if (level >= img.levels.size()) return {};
    const auto& L = img.levels[level];
    *ow = L.width;
    *oh = L.height;
    std::string out(static_cast<size_t>(L.width) * L.height * 4, '\0');
    const auto* src = reinterpret_cast<const uint8_t*>(img.data.data() + L.offset);
    auto put = [&](uint32_t x, uint32_t y, const uint8_t* px) {
        if (x < L.width && y < L.height) std::memcpy(&out[(static_cast<size_t>(y) * L.width + x) * 4], px, 4);
    };
    if (img.compressed()) {
        const uint32_t bw = std::max(1u, (L.width + 3) / 4), bh = std::max(1u, (L.height + 3) / 4);
        const size_t bs = img.format == ImageData::DXT1 ? 8 : 16;
        for (uint32_t by = 0; by < bh; ++by)
            for (uint32_t bx = 0; bx < bw; ++bx) {
                const uint8_t* blk = src + (static_cast<size_t>(by) * bw + bx) * bs;
                uint8_t px[16][4];
                color_block(img.format == ImageData::DXT1 ? blk : blk + 8, px, img.format == ImageData::DXT1);
                if (img.format == ImageData::DXT3) {
                    for (int i = 0; i < 16; ++i) px[i][3] = static_cast<uint8_t>(((blk[i / 2] >> (4 * (i & 1))) & 15) * 17);
                } else if (img.format == ImageData::DXT5) {
                    const uint8_t a0 = blk[0], a1 = blk[1];
                    uint8_t a[8] = {a0, a1};
                    for (int k = 2; k < 8; ++k)
                        a[k] = a0 > a1 ? static_cast<uint8_t>(((8 - k) * a0 + (k - 1) * a1) / 7)
                                       : (k < 6 ? static_cast<uint8_t>(((6 - k) * a0 + (k - 1) * a1) / 5) : (k == 6 ? 0 : 255));
                    uint64_t bits = 0;
                    for (int k = 0; k < 6; ++k) bits |= static_cast<uint64_t>(blk[2 + k]) << (8 * k);
                    for (int i = 0; i < 16; ++i) px[i][3] = a[(bits >> (3 * i)) & 7];
                }
                for (int i = 0; i < 16; ++i) put(bx * 4 + static_cast<uint32_t>(i % 4), by * 4 + static_cast<uint32_t>(i / 4), px[i]);
            }
    } else {
        for (uint32_t y = 0; y < L.height; ++y)
            for (uint32_t x = 0; x < L.width; ++x) {
                uint8_t px[4];
                const size_t i = static_cast<size_t>(y) * L.width + x;
                switch (img.format) {
                    case ImageData::BGRA8: px[0] = src[i * 4 + 2], px[1] = src[i * 4 + 1], px[2] = src[i * 4], px[3] = src[i * 4 + 3]; break;
                    case ImageData::BGRX8: px[0] = src[i * 4 + 2], px[1] = src[i * 4 + 1], px[2] = src[i * 4], px[3] = 255; break;
                    case ImageData::RGBA8: std::memcpy(px, src + i * 4, 4); break;
                    case ImageData::L8: px[0] = px[1] = px[2] = src[i], px[3] = 255; break;
                    case ImageData::A8L8: px[0] = px[1] = px[2] = src[i * 2], px[3] = src[i * 2 + 1]; break;
                    default: px[0] = px[1] = px[2] = px[3] = 255;
                }
                put(x, y, px);
            }
    }
    return out;
}

}  // namespace mk
