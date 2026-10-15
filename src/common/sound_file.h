#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Pure helpers for custom voice lines (docs/RE.md section 28). No engine access, so the unit tests cover them.
//
// The engine plays a *compiled* sound resource (`X._xsnd`): a `GR01`/`SN04` container around an ordinary audio file that FMOD opens from memory
// (shipped speech is MP3). `probe` recognises the formats a modder can supply (Ogg Vorbis, WAV, MP3) and measures their length, `buildXsnd`
// wraps the bytes in the container the engine loads, `normalizeLanguage` maps language codes to the engine's voice language names.
namespace spass::sound {

struct Info {
    std::string format;           // "ogg", "wav" or "mp3"
    int channels = 0;
    int sampleRate = 0;
    int bitsPerSample = 0;        // 0 when not applicable (compressed)
    std::uint64_t durationMs = 0;
};

namespace detail {

inline std::uint32_t le32(const unsigned char* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<std::uint32_t>(p[3]) << 24); }
inline std::uint16_t le16(const unsigned char* p) { return static_cast<std::uint16_t>(p[0] | (p[1] << 8)); }
inline std::uint64_t le64(const unsigned char* p) { return le32(p) | (static_cast<std::uint64_t>(le32(p + 4)) << 32); }

inline std::optional<Info> probeOgg(const unsigned char* d, std::size_t n, std::string& err) {
    // first page: "OggS" version 0, header type, granule(8) serial(4) seq(4) crc(4) nsegs(1) segtable, then the first packet
    if (n < 27 + 1 || d[4] != 0) {
        err = "not a valid Ogg file";
        return std::nullopt;
    }
    const std::size_t nsegs = d[26];
    std::size_t pos = 27 + nsegs;
    if (pos + 30 > n) {
        err = "truncated Ogg file";
        return std::nullopt;
    }
    if (std::memcmp(d + pos, "OpusHead", 8) == 0) {
        err = "Ogg Opus is not supported: use Ogg Vorbis (e.g. ffmpeg -i in.opus -c:a libvorbis out.ogg)";
        return std::nullopt;
    }
    if (std::memcmp(d + pos + 1, "vorbis", 6) != 0 || d[pos] != 1) {
        err = "the Ogg file does not contain Vorbis audio (only Ogg Vorbis is supported)";
        return std::nullopt;
    }
    Info info;
    info.format = "ogg";
    info.channels = d[pos + 11];
    info.sampleRate = static_cast<int>(le32(d + pos + 12));
    if (info.channels < 1 || info.channels > 8 || info.sampleRate < 4000 || info.sampleRate > 192000) {
        err = "the Ogg Vorbis header is invalid";
        return std::nullopt;
    }
    // length: the granule position of the last page
    std::uint64_t granule = 0;
    for (std::size_t i = n >= 27 ? n - 27 : 0;; --i) {
        if (std::memcmp(d + i, "OggS", 4) == 0 && d[i + 4] == 0) {
            granule = le64(d + i + 6);
            break;
        }
        if (i == 0) {
            break;
        }
    }
    if (granule == 0 || granule == ~std::uint64_t{0}) {
        err = "cannot determine the length of the Ogg file (no final page)";
        return std::nullopt;
    }
    info.durationMs = (granule * 1000 + static_cast<std::uint64_t>(info.sampleRate) / 2) / static_cast<std::uint64_t>(info.sampleRate);
    return info;
}

inline std::optional<Info> probeWav(const unsigned char* d, std::size_t n, std::string& err) {
    if (n < 12 || std::memcmp(d + 8, "WAVE", 4) != 0) {
        err = "not a valid WAV file";
        return std::nullopt;
    }
    std::size_t pos = 12;
    bool haveFmt = false;
    std::uint16_t tag = 0, channels = 0, bits = 0, blockAlign = 0;
    std::uint32_t rate = 0, byteRate = 0;
    std::uint64_t dataSize = 0;
    bool haveData = false;
    while (pos + 8 <= n) {
        const std::uint32_t size = le32(d + pos + 4);
        const std::size_t body = pos + 8;
        if (std::memcmp(d + pos, "fmt ", 4) == 0) {
            if (size < 16 || body + 16 > n) {
                err = "the WAV 'fmt ' chunk is truncated";
                return std::nullopt;
            }
            tag = le16(d + body);
            channels = le16(d + body + 2);
            rate = le32(d + body + 4);
            byteRate = le32(d + body + 8);
            blockAlign = le16(d + body + 12);
            bits = le16(d + body + 14);
            if (tag == 0xFFFE && size >= 26 && body + 26 <= n) {
                tag = le16(d + body + 24);  // the first two bytes of the sub-format GUID
            }
            haveFmt = true;
        } else if (std::memcmp(d + pos, "data", 4) == 0) {
            haveData = true;
            const std::uint64_t remain = n - body;
            dataSize = (size == 0 || size == 0xFFFFFFFFu || size > remain) ? remain : size;
            break;
        }
        pos = body + size + (size & 1u);
    }
    if (!haveFmt || !haveData) {
        err = "the WAV file has no 'fmt ' or 'data' chunk";
        return std::nullopt;
    }
    if (tag != 1 && tag != 3 && tag != 2 && tag != 0x11) {
        err = "unsupported WAV encoding (supported: PCM, IEEE float, ADPCM); convert it, e.g. ffmpeg -i in.wav -c:a pcm_s16le out.wav";
        return std::nullopt;
    }
    if (channels < 1 || channels > 8 || rate < 4000 || rate > 192000 || dataSize == 0) {
        err = "the WAV file has an invalid format or no audio data";
        return std::nullopt;
    }
    if ((tag == 1 && bits != 8 && bits != 16 && bits != 24 && bits != 32) || (tag == 3 && bits != 32 && bits != 64)) {
        err = "unsupported WAV sample size";
        return std::nullopt;
    }
    Info info;
    info.format = "wav";
    info.channels = channels;
    info.sampleRate = static_cast<int>(rate);
    info.bitsPerSample = bits;
    if (byteRate == 0) {
        byteRate = rate * blockAlign;
    }
    if (byteRate == 0) {
        err = "the WAV file has an invalid byte rate";
        return std::nullopt;
    }
    info.durationMs = (dataSize * 1000 + byteRate / 2) / byteRate;
    return info;
}

// MPEG audio layer III frame header at p: size of the frame in bytes and its samples/rate, or 0 when it is not a valid header.
struct Mp3Frame {
    std::size_t bytes = 0;
    int samples = 0;
    int rate = 0;
    int channels = 0;
};

inline Mp3Frame mp3Header(const unsigned char* p) {
    static const int kRates[3][3] = {{44100, 48000, 32000}, {22050, 24000, 16000}, {11025, 12000, 8000}};  // [version][index]
    static const int kBitrates1[16] = {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0};
    static const int kBitrates2[16] = {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0};
    Mp3Frame f;
    if (p[0] != 0xFF || (p[1] & 0xE0) != 0xE0) {
        return f;
    }
    const int versionBits = (p[1] >> 3) & 3;  // 3 = MPEG1, 2 = MPEG2, 0 = MPEG2.5, 1 = reserved
    const int layerBits = (p[1] >> 1) & 3;    // 1 = layer III
    const int brIndex = (p[2] >> 4) & 15;
    const int srIndex = (p[2] >> 2) & 3;
    const int padding = (p[2] >> 1) & 1;
    if (versionBits == 1 || layerBits != 1 || brIndex == 0 || brIndex == 15 || srIndex == 3) {
        return f;
    }
    const int v = versionBits == 3 ? 0 : (versionBits == 2 ? 1 : 2);
    f.rate = kRates[v][srIndex];
    const int bitrate = (v == 0 ? kBitrates1[brIndex] : kBitrates2[brIndex]) * 1000;
    f.samples = v == 0 ? 1152 : 576;
    f.bytes = static_cast<std::size_t>((v == 0 ? 144 : 72) * bitrate / f.rate + padding);
    f.channels = ((p[3] >> 6) & 3) == 3 ? 1 : 2;
    return f;
}

inline std::optional<Info> probeMp3(const unsigned char* d, std::size_t n, std::string& err) {
    std::size_t pos = 0;
    if (n >= 10 && std::memcmp(d, "ID3", 3) == 0) {
        pos = 10 + ((static_cast<std::size_t>(d[6] & 0x7F) << 21) | (static_cast<std::size_t>(d[7] & 0x7F) << 14) |
                    (static_cast<std::size_t>(d[8] & 0x7F) << 7) | (d[9] & 0x7F));
    }
    // find the first frame: a valid header followed by another valid one (or the end of the file)
    std::size_t first = std::string::npos;
    for (std::size_t i = pos; i + 4 <= n; ++i) {
        const Mp3Frame f = mp3Header(d + i);
        if (f.bytes == 0) {
            continue;
        }
        const std::size_t next = i + f.bytes;
        if (next + 4 <= n ? mp3Header(d + next).bytes != 0 : next >= n - 1 || next == n) {
            first = i;
            break;
        }
    }
    if (first == std::string::npos) {
        err = "not a valid MP3 file (no MPEG layer III frames found)";
        return std::nullopt;
    }
    Info info;
    info.format = "mp3";
    std::uint64_t samples = 0;
    std::size_t i = first;
    bool firstFrame = true;
    while (i + 4 <= n) {
        const Mp3Frame f = mp3Header(d + i);
        if (f.bytes == 0) {
            break;
        }
        if (firstFrame) {
            info.sampleRate = f.rate;
            info.channels = f.channels;
            firstFrame = false;
            // a Xing/Info/VBRI header frame carries no audio: skip it when it says "Xing", "Info" (offset depends on version/channels)
            const bool mpeg1 = ((d[i + 1] >> 3) & 3) == 3;
            const std::size_t xing = i + 4 + (mpeg1 ? (f.channels == 1 ? 17 : 32) : (f.channels == 1 ? 9 : 17));
            if (xing + 4 <= n && (std::memcmp(d + xing, "Xing", 4) == 0 || std::memcmp(d + xing, "Info", 4) == 0)) {
                i += f.bytes;
                continue;
            }
        }
        samples += static_cast<std::uint64_t>(f.samples);
        i += f.bytes;
    }
    if (samples == 0 || info.sampleRate == 0) {
        err = "cannot determine the length of the MP3 file";
        return std::nullopt;
    }
    info.durationMs = (samples * 1000 + static_cast<std::uint64_t>(info.sampleRate) / 2) / static_cast<std::uint64_t>(info.sampleRate);
    return info;
}

}  // namespace detail

// Recognises the format by its content (not the file name) and measures it. On failure returns nullopt and a reason in `error`.
inline std::optional<Info> probe(std::string_view bytes, std::string& error) {
    const auto* d = reinterpret_cast<const unsigned char*>(bytes.data());
    const std::size_t n = bytes.size();
    if (n < 16) {
        error = "the file is empty or too small to be audio";
        return std::nullopt;
    }
    std::optional<Info> info;
    if (std::memcmp(d, "OggS", 4) == 0) {
        info = detail::probeOgg(d, n, error);
    } else if (std::memcmp(d, "RIFF", 4) == 0) {
        info = detail::probeWav(d, n, error);
    } else if (std::memcmp(d, "ID3", 3) == 0 || (d[0] == 0xFF && (d[1] & 0xE0) == 0xE0)) {
        info = detail::probeMp3(d, n, error);
    } else if (std::memcmp(d, "GR01", 4) == 0) {
        error = "this is already a compiled engine resource (put it into the assets directory as it is instead of importing it)";
        return std::nullopt;
    } else {
        error = "unsupported audio format (supported: Ogg Vorbis, WAV, MP3)";
        return std::nullopt;
    }
    if (info && info->durationMs == 0) {
        error = "the audio is empty";
        return std::nullopt;
    }
    if (info && info->durationMs > 30u * 60u * 1000u) {
        error = "the audio is longer than 30 minutes";
        return std::nullopt;
    }
    return info;
}

// The compiled sound resource the engine loads (RE.md section 28): "GR01" "SN04", u32 header size 0x28, u32 size of the property stream, u32 offset
// of the payload, u32 payload size, FILETIME, source extension (8 bytes), the property stream (class eCSoundResource2: Duration in ms, Flags, Links),
// then the audio file unchanged. `flags` 0 = ordinary 3D-capable voice (what shipped speech uses).
inline std::string buildXsnd(std::string_view payload, std::uint64_t durationMs, std::uint64_t fileTime, std::uint8_t flags = 0) {
    static const unsigned char kStream[151] = {
        0x01, 0x00, 0x01, 0x01, 0x00, 0x01, 0x10, 0x00, 0x65, 0x43, 0x53, 0x6f, 0x75, 0x6e, 0x64, 0x52,
        0x65, 0x73, 0x6f, 0x75, 0x72, 0x63, 0x65, 0x32, 0x01, 0x00, 0x00, 0xc9, 0x00, 0xc9, 0x00, 0x74,
        0x00, 0x00, 0x00, 0xc9, 0x00, 0x03, 0x00, 0x00, 0x00, 0x08, 0x00, 0x44, 0x75, 0x72, 0x61, 0x74,
        0x69, 0x6f, 0x6e, 0x04, 0x00, 0x6c, 0x6f, 0x6e, 0x67, 0x1e, 0x00, 0x04, 0x00, 0x00, 0x00, 0x75,
        0x07, 0x00, 0x00, 0x05, 0x00, 0x46, 0x6c, 0x61, 0x67, 0x73, 0x04, 0x00, 0x63, 0x68, 0x61, 0x72,
        0x1e, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x05, 0x00, 0x4c, 0x69, 0x6e, 0x6b, 0x73, 0x2a, 0x00,
        0x62, 0x54, 0x4f, 0x62, 0x6a, 0x41, 0x72, 0x72, 0x61, 0x79, 0x3c, 0x73, 0x74, 0x72, 0x75, 0x63,
        0x74, 0x20, 0x65, 0x43, 0x53, 0x6f, 0x75, 0x6e, 0x64, 0x52, 0x65, 0x73, 0x6f, 0x75, 0x72, 0x63,
        0x65, 0x32, 0x3a, 0x3a, 0x53, 0x4c, 0x69, 0x6e, 0x6b, 0x3e, 0x1e, 0x00, 0x05, 0x00, 0x00, 0x00,
        0x01, 0x00, 0x00, 0x00, 0x00, 0xc9, 0x00,
    };
    constexpr std::size_t kDurationOffset = 63;  // inside the stream
    constexpr std::size_t kFlagsOffset = 86;
    std::string out;
    out.reserve(0x28 + sizeof(kStream) + payload.size());
    const auto put32 = [&](std::uint32_t v) {
        for (int i = 0; i < 4; ++i) out.push_back(static_cast<char>((v >> (8 * i)) & 0xFF));
    };
    out.append("GR01SN04", 8);
    put32(0x28);
    put32(static_cast<std::uint32_t>(sizeof(kStream)));
    put32(static_cast<std::uint32_t>(0x28 + sizeof(kStream)));
    put32(static_cast<std::uint32_t>(payload.size()));
    for (int i = 0; i < 8; ++i) out.push_back(static_cast<char>((fileTime >> (8 * i)) & 0xFF));
    out.append(".wav\0\xfe\xfe\xfe", 8);
    std::string stream(reinterpret_cast<const char*>(kStream), sizeof(kStream));
    for (int i = 0; i < 4; ++i) stream[kDurationOffset + i] = static_cast<char>((durationMs >> (8 * i)) & 0xFF);
    stream[kFlagsOffset] = static_cast<char>(flags);
    out += stream;
    out.append(payload.data(), payload.size());
    return out;
}

// Reads the duration (ms) back from a compiled sound produced by buildXsnd or the game (0 when it is not one).
inline std::uint64_t xsndDurationMs(std::string_view file) {
    if (file.size() < 0x28 + 151 || file.substr(0, 8) != std::string_view("GR01SN04", 8)) {
        return 0;
    }
    return detail::le32(reinterpret_cast<const unsigned char*>(file.data()) + 0x28 + 63);
}

// ---- voice languages --------------------------------------------------------------------------------------------------------------------------

inline std::string lowerAscii(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

// The engine's name of a voice language for a code or name ("de", "German", "deutsch" -> "German"); unknown names are kept as written (first letter upper
// case) because a game edition may ship other voice languages. "" for an empty argument.
inline std::string normalizeLanguage(std::string_view name) {
    static const std::map<std::string, std::string> kKnown = {
        {"en", "English"}, {"eng", "English"}, {"english", "English"}, {"en-us", "English"}, {"en-gb", "English"},
        {"de", "German"},  {"ger", "German"},  {"deu", "German"},     {"german", "German"},  {"deutsch", "German"},
        {"fr", "French"},  {"fra", "French"},  {"fre", "French"},     {"french", "French"},  {"francais", "French"},
        {"ru", "Russian"}, {"rus", "Russian"}, {"russian", "Russian"},
        {"pl", "Polish"},  {"pol", "Polish"},  {"polish", "Polish"},
        {"it", "Italian"}, {"ita", "Italian"}, {"italian", "Italian"},
        {"es", "Spanish"}, {"spa", "Spanish"}, {"spanish", "Spanish"},
        {"cs", "Czech"},   {"cz", "Czech"},    {"czech", "Czech"},
    };
    const std::string l = lowerAscii(name);
    if (l.empty()) {
        return {};
    }
    if (const auto it = kKnown.find(l); it != kKnown.end()) {
        return it->second;
    }
    std::string out(name);
    out[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(out[0])));
    return out;
}

// A voice entry name safe for a file name: letters, digits and '_' '-' kept, everything else becomes '_'.
inline std::string safeName(std::string_view s) {
    std::string out;
    for (const char c : s) {
        out += (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-') ? c : '_';
    }
    return out;
}

// Name of the sound the layer makes for a voice line: `INFO_SPASS_<mod>_<key>_<Language>_%_SPASS` (shipped speech names start with INFO_ and carry
// `_%_<speaker>`; the engine reads both for its sound flags). `.wav` is the raw extension a name is written with, `._xsnd` the compiled file.
inline std::string voiceBaseName(std::string_view mod, std::string_view key, std::string_view language) {
    return "INFO_SPASS_" + safeName(mod) + "_" + safeName(key) + "_" + safeName(language) + "_%_SPASS";
}

// The language variant to play: the current voice language, else `default`, else English, else the first one (by name). Empty map -> "".
inline std::string pickLanguage(const std::map<std::string, std::string>& variants, std::string_view current) {
    if (variants.empty()) {
        return {};
    }
    const std::string cur = normalizeLanguage(current);
    if (!cur.empty() && variants.count(cur)) return cur;
    if (variants.count("default")) return "default";
    if (variants.count("English")) return "English";
    return variants.begin()->first;
}

}  // namespace spass::sound
