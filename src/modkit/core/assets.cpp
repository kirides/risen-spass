#include "core/assets.h"

#include <algorithm>
#include <cstring>

#include "common/sound_file.h"
#include "core/image.h"
#include "pack2mod/util.h"

namespace mk {

namespace {

std::string ext_of(const std::string& file) {
    const std::string n = p2m::lower(p2m::bare_name(file));
    const auto dot = n.rfind('.');
    return dot == std::string::npos ? std::string() : n.substr(dot);
}

uint32_t le32(const std::string& b, size_t at) {
    const auto* p = reinterpret_cast<const unsigned char*>(b.data()) + at;
    return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

}  // namespace

bool is_raw_audio(const std::string& file) {
    const std::string e = ext_of(file);
    return e == ".ogg" || e == ".wav" || e == ".mp3";
}

bool is_compiled_sound(const std::string& file) { return ext_of(file) == "._xsnd"; }

bool is_loose_data_file(const std::string& file) { return p2m::lower(p2m::bare_name(file)) == "compiled_combatspecies.bin" || ext_of(file) == ".g3ws"; }

std::string asset_kind(const std::string& file) {
    const std::string e = ext_of(file);
    if (e == "._xmsh") return "mesh";
    if (e == "._xmat") return "material";
    if (e == "._ximg") return "image";
    if (e == "._xmac") return "actor";
    if (e == "._xmot") return "motion";
    if (e == "._xsnd" || is_raw_audio(file)) return "sound";
    if (is_picture(file)) return "picture";
    if (e == ".ximl") return "iconlist";
    return "other";
}

std::optional<SoundData> open_sound(const std::string& bytes, std::string& error) {
    SoundData d;
    std::string_view audio(bytes);
    if (bytes.size() >= 8 && std::memcmp(bytes.data(), "GR01SN04", 8) == 0) {
        // the container: u32 header size, u32 property stream size, u32 payload offset, u32 payload size (RE.md section 28)
        if (bytes.size() < 0x28) {
            error = "the compiled sound is truncated";
            return std::nullopt;
        }
        const uint32_t off = le32(bytes, 16), size = le32(bytes, 20);
        if (off > bytes.size() || size > bytes.size() - off) {
            error = "the compiled sound is damaged (its payload lies outside the file)";
            return std::nullopt;
        }
        audio = audio.substr(off, size);
        d.compiled = true;
    }
    const auto info = spass::sound::probe(audio, error);
    if (!info) return std::nullopt;
    d.audio.assign(audio.data(), audio.size());
    d.format = info->format;
    d.channels = info->channels;
    d.sample_rate = info->sampleRate;
    d.duration_ms = info->durationMs;
    return d;
}

std::string sound_language(const std::string& name) {
    static const char* kLanguages[] = {"English", "German", "French", "Russian", "Polish", "Italian", "Spanish", "Czech"};
    const std::string n = p2m::bare_name(name);
    for (const char* l : kLanguages) {
        const std::string speech = std::string("_") + l + "_%_";  // INFO_..._<voice>_<Language>_%_<entity>
        const std::string svm = std::string("_") + l + ".";       // SVM_<voice>_<phrase>_<Language>._xsnd
        if (p2m::contains(n, speech) || p2m::contains(n, svm)) return l;
    }
    return {};
}

const AssetFile* find_asset(const Project& p, const std::string& file_name) {
    const std::string k = p2m::lower(p2m::bare_name(file_name));
    for (auto& a : p.assets)
        if (p2m::lower(p2m::bare_name(a.file)) == k) return &a;
    return nullptr;
}

std::string sound_export_path(const std::string& ref) {
    const std::string n = p2m::bare_name(ref);
    // a compiled sound is unpacked; the layer recognises the format by content, the extension is only a label (shipped speech is MP3)
    if (is_compiled_sound(n)) return "sounds/" + n.substr(0, n.size() - 6) + ".mp3";
    return "sounds/" + n;
}

std::string sound_raw_name(const std::string& compiled) {
    if (!is_compiled_sound(compiled)) return compiled;
    return compiled.substr(0, compiled.size() - 6) + ".wav";
}

std::string resolve_icon(const Project& p, const std::string& icon) {
    if (is_picture(icon) && find_asset(p, icon)) return project_icon_name(p.mod.id, icon);
    return icon;
}

std::vector<std::string> icon_pictures(const Project& p) {
    std::vector<std::string> out;
    auto add = [&](const std::optional<std::string>& icon) {
        if (!icon || !is_picture(*icon) || !find_asset(p, *icon)) return;
        const std::string k = p2m::lower(p2m::bare_name(*icon));
        if (std::find(out.begin(), out.end(), k) == out.end()) out.push_back(k);
    };
    for (auto& i : p.items) add(i.icon);
    for (auto& r : p.recipes) add(r.icon);
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<std::string> icon_lists(const Project& p) {
    std::vector<std::string> out;
    if (!icon_pictures(p).empty()) out.push_back(p.mod.id + "_Icons");
    for (auto& a : p.assets) {
        std::string n = p2m::bare_name(a.file);
        if (asset_kind(n) != "iconlist") continue;
        n.erase(n.rfind('.'));
        if (std::find(out.begin(), out.end(), n) == out.end()) out.push_back(n);
    }
    return out;
}

std::vector<std::string> generated_asset_names(const Project& p) {
    std::vector<std::string> out;
    const auto icons = icon_pictures(p);
    for (auto& a : p.assets) {
        const std::string n = p2m::bare_name(a.file);
        if (is_picture(n) && std::find(icons.begin(), icons.end(), p2m::lower(n)) == icons.end()) out.push_back(p2m::lower(picture_compiled_name(n)));
    }
    if (!icons.empty()) {
        out.push_back(p2m::lower(p.mod.id + "_Icons.ximl"));
        out.push_back(p2m::lower(p.mod.id + "_IconAtlas._ximg"));
    }
    return out;
}

}  // namespace mk
