// Asset files of a project and sounds (docs/editor/REQUIREMENTS.md AST-1..2, DLG-9): what kind a file is, the engine's compiled sound container
// (`._xsnd`, RE.md section 28) and the audio formats the layer takes for voiced lines (Ogg Vorbis, WAV, MP3, recognised by content).
//
// A dialogue line names its sound by a *reference*: the file name of one of the project's audio files ("greet.ogg") or the compiled name of a
// game sound ("INFO_..._English_%_Eldric._xsnd"). The export writes both as files of the mod below `sounds/` (a game sound is unpacked from its
// container: the shipped payloads are MP3) and the line gets `sound = "sounds/<file>"`.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "core/project.h"

namespace mk {

// "mesh", "material", "image", "actor", "motion", "sound" (compiled ._xsnd or raw audio), "picture" (PNG, TGA, JPG, BMP: exported as ._ximg or
// as an icon), "iconlist" (.ximl: registered by the runtime), "other" by the file name
std::string asset_kind(const std::string& file);
bool is_raw_audio(const std::string& file);       // .ogg / .wav / .mp3
bool is_compiled_sound(const std::string& file);  // ._xsnd

struct SoundData {
    std::string audio;    // the playable file (the payload of a compiled sound)
    std::string format;   // "ogg", "wav", "mp3"
    int channels = 0, sample_rate = 0;
    uint64_t duration_ms = 0;
    bool compiled = false;  // it came out of a ._xsnd container
};
// a raw audio file or a compiled sound; nullopt and a reason when it is neither or damaged
std::optional<SoundData> open_sound(const std::string& bytes, std::string& error);

// the voice language a shipped speech file is for ("English" for "..._English_%_Eldric._xsnd" or "SVM_Enzo_Attack00_English._xsnd"), "" otherwise
std::string sound_language(const std::string& name);

// the project's asset with this file name (case-insensitive), nullptr when none
const AssetFile* find_asset(const Project& p, const std::string& file_name);
// the mod-relative file a sound reference is exported as: "sounds/greet.ogg", "sounds/INFO_..._%_Eldric.mp3"
std::string sound_export_path(const std::string& ref);
// the raw name the engine plays for a compiled sound ("X._xsnd" -> "X.wav", the SoundName of a SaySound command)
std::string sound_raw_name(const std::string& compiled);

// ---- pictures and icons (core/image.h)
// the icon name the export writes for an item's / recipe's icon: a project picture becomes its atlas cell "IC_<mod>_<stem>", anything else stays
std::string resolve_icon(const Project& p, const std::string& icon);
// the project pictures items and recipes use as icons (lower-case file names); they go into the icon atlas instead of becoming textures
std::vector<std::string> icon_pictures(const Project& p);
// the image lists the runtime registers (risen.assets.load_icons): "<mod>_Icons" when pictures are icons, plus every .ximl asset (by stem)
std::vector<std::string> icon_lists(const Project& p);
// the files the export generates into assets/ for pictures and icons (lower-case names), to check against the project's own files
std::vector<std::string> generated_asset_names(const Project& p);

}  // namespace mk
