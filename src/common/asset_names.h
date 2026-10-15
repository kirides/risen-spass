#pragma once

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>

// Pure helpers for resource (asset) names (docs/RE.md section 26). No engine access, so the unit tests cover them.
//
// Properties and shipped data refer to resources by their *raw* name (`It_Wpn_1H_Club.xmsh`, `Ani_Wolf_Monster_Wolf.xac`,
// `ItWpn_SwordMisc_01_Diffuse_01.tga` inside a material); the engine loads the compiled file with an underscore in front of the extension
// (`It_Wpn_1H_Club._xmsh`) and finds it by that bare file name in a flat, case-insensitive name cache of everything mounted.
namespace spass::assets {

enum class Kind { Mesh, Material, Image, Actor, Motion, Collision, Sound, Unknown };

inline std::string lowerAscii(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

// The part after the last '.', lower case, without the dot ("" when there is none).
inline std::string extensionOf(std::string_view name) {
    const auto dot = name.find_last_of('.');
    if (dot == std::string_view::npos || dot + 1 >= name.size()) {
        return {};
    }
    return lowerAscii(name.substr(dot + 1));
}

// Kind of resource an (uncompiled or compiled) name denotes, from its extension.
inline Kind kindOf(std::string_view name) {
    const std::string ext = extensionOf(name);
    if (ext == "xmsh" || ext == "_xmsh") return Kind::Mesh;
    if (ext == "xmat" || ext == "_xmat") return Kind::Material;
    if (ext == "ximg" || ext == "_ximg" || ext == "tga" || ext == "dds" || ext == "png" || ext == "jpg" || ext == "bmp") return Kind::Image;
    if (ext == "xac" || ext == "xmac" || ext == "_xmac") return Kind::Actor;
    if (ext == "xmot" || ext == "_xmot") return Kind::Motion;
    if (ext == "xcom" || ext == "_xcom") return Kind::Collision;
    if (ext == "wav" || ext == "ogg" || ext == "mp3" || ext == "xsnd" || ext == "_xsnd") return Kind::Sound;
    return Kind::Unknown;
}

inline const char* kindName(Kind k) {
    switch (k) {
        case Kind::Mesh: return "mesh";
        case Kind::Material: return "material";
        case Kind::Image: return "image";
        case Kind::Actor: return "actor";
        case Kind::Motion: return "motion";
        case Kind::Collision: return "collision";
        case Kind::Sound: return "sound";
        default: return "unknown";
    }
}

// The file name the engine looks up in its name cache: `X.xmsh` -> `X._xmsh`, `X.tga` -> `X._ximg`, `X.xac` -> `X._xmac`; a name that is
// already compiled (`X._xmsh`) stays as it is. Nothing for names with another extension (or a path: a resource name is a bare file name).
inline std::optional<std::string> compiledName(std::string_view name) {
    if (name.empty() || name.find_first_of("/\\") != std::string_view::npos) {
        return std::nullopt;
    }
    const Kind kind = kindOf(name);
    if (kind == Kind::Unknown) {
        return std::nullopt;
    }
    const std::string ext = extensionOf(name);
    if (!ext.empty() && ext[0] == '_') {
        return std::string(name);
    }
    const std::string stem(name.substr(0, name.size() - ext.size() - 1));
    switch (kind) {
        case Kind::Mesh: return stem + "._xmsh";
        case Kind::Material: return stem + "._xmat";
        case Kind::Image: return stem + "._ximg";
        case Kind::Actor: return stem + "._xmac";
        case Kind::Motion: return stem + "._xmot";
        case Kind::Collision: return stem + "._xcom";
        case Kind::Sound: return stem + "._xsnd";
        default: return std::nullopt;
    }
}

// A path below a directory we own: relative, no drive, no `..`, no empty/dot components, printable, bounded length.
// Both separators are accepted; returns the cleaned path with '/' separators.
inline std::optional<std::string> cleanRelativePath(std::string_view path) {
    if (path.empty() || path.size() > 200) {
        return std::nullopt;
    }
    std::string out;
    std::string part;
    const auto flush = [&]() -> bool {
        if (part.empty() || part == "." || part == "..") {
            return false;
        }
        if (part.back() == '.' || part.back() == ' ') {
            return false;  // Windows would silently strip them
        }
        if (!out.empty()) {
            out += '/';
        }
        out += part;
        part.clear();
        return true;
    };
    for (const char c : path) {
        const auto u = static_cast<unsigned char>(c);
        if (c == '/' || c == '\\') {
            if (!flush()) {
                return std::nullopt;
            }
            continue;
        }
        if (u < 32 || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
            return std::nullopt;
        }
        part += c;
    }
    if (!flush()) {
        return std::nullopt;
    }
    return out;
}

// True when a physical path (as the engine reports it) points at a real file, not into a `.pak` volume
// (`B:\...\data\common\meshes.pak\Items_01\X._xmsh`).
inline bool isLoosePhysicalPath(std::string_view physical) {
    const std::string p = lowerAscii(physical);
    return !p.empty() && p.find(".pak\\") == std::string::npos && p.find(".pak/") == std::string::npos;
}

}  // namespace spass::assets
