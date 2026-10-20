// Recordings into the project (docs/editor/REQUIREMENTS.md VOX-5..7): which files the game plays as they are (Ogg Vorbis, WAV PCM / float / ADPCM, MP3:
// what FMOD opens, docs/RE.md section 28) and the conversion of everything else to Ogg Vorbis with an ffmpeg the user has (PATH or the editor's settings;
// it is never bundled). The import job copies or converts the matched files into the project's voice/ folder; the GUI runs it on a worker thread.
#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <vector>

#include "core/process.h"
#include "core/voice.h"

namespace mk {

struct FfmpegInfo {
    bool ok = false;
    std::string path;     // what is run
    std::string version;  // "N-122296-gc2b1ad28a2" / "6.1.1"
    bool libvorbis = false;  // the encoder for Ogg Vorbis (without it the conversion writes 16-bit WAV)
    std::string error;
};
// the ffmpeg to use: the configured path when it is set (an error when it does not exist), else "ffmpeg" / "ffmpeg.exe" on PATH; "" when none
std::string locate_ffmpeg(const std::string& configured, std::string* error = nullptr);
// runs "<exe> -hide_banner -version" and "-encoders"
FfmpegInfo probe_ffmpeg(const std::string& exe, const ProcessRunner& run);
// how to get ffmpeg on this system (one paragraph)
std::string ffmpeg_help();

struct ConvertOptions {
    bool mono = true;          // the shipped speech is mono (3D voices are positioned): -ac 1
    int sample_rate = 44100;   // the shipped speech rate; 0 keeps the file's
    bool normalize = false;    // EBU R128 loudness normalisation (loudnorm, single pass)
    double loudness = -16.0;   // integrated loudness target in LUFS
    int quality = 5;           // libvorbis -q:a (0..10)
};
// the argument vector of one conversion (argv[0] = exe); the output is Ogg Vorbis with libvorbis, else 16-bit PCM WAV
std::vector<std::string> ffmpeg_convert_args(const FfmpegInfo& ff, const std::string& in, const std::string& out, const ConvertOptions& o);
// the extension the conversion writes ("ogg", or "wav" without libvorbis)
std::string converted_ext(const FfmpegInfo& ff);
// converts `in` to `out` and checks that the result plays; "" on success, else the reason (with the tail of ffmpeg's output)
std::string convert_audio(const FfmpegInfo& ff, const std::string& in, const std::string& out, const ConvertOptions& o, const ProcessRunner& run,
                          const std::atomic<bool>* cancel = nullptr);

// whether the game plays a file as it is (the probe of the layer: Ogg Vorbis, WAV, MP3); `why` says what is wrong otherwise
bool plays_as_is(const std::string& bytes, std::string& why);

struct VoiceImportStep {
    VoiceMatch match;
    bool convert = false;    // through ffmpeg
    std::string why;         // why it is converted / why it cannot be imported
    std::string dest_rel;    // "voice/<id>_<Language>.ogg", relative to the project folder
    bool skip = false;       // cannot be imported (no ffmpeg, unreadable)
    bool done = false;
    std::string error;
};
struct VoiceImportOptions {
    ConvertOptions convert;
    bool convert_all = false;   // convert files that already play too (mono / rate / loudness for every file); implied by convert.normalize
    std::string folder = "voice";  // below the project folder
};
// what happens to each matched file: copied, converted or skipped (without ffmpeg a file the game cannot play is skipped with `why`)
std::vector<VoiceImportStep> plan_voice_import(const VoiceMatchReport& rep, const VoiceImportOptions& o, const FfmpegInfo* ff,
                                               const std::function<std::string(const std::string&)>& read_file);
// carries the plan out into `project_dir`; progress(done, total, current file); stops between files and kills ffmpeg on cancel. Returns the files that
// are in place (for apply_voice_import).
std::vector<ImportedVoice> run_voice_import(std::vector<VoiceImportStep>& steps, const std::string& project_dir, const VoiceImportOptions& o,
                                            const FfmpegInfo* ff, const ProcessRunner& run, const std::function<void(size_t, size_t, const std::string&)>& progress,
                                            const std::atomic<bool>* cancel);
// the whole preview: the audio files below `folder` (recursive; other files are not looked at), the CSV (optional), the match against `targets` and the
// plan. `error` is set when the folder or the CSV cannot be read.
struct VoicePreview {
    VoiceMatchReport rep;
    std::vector<VoiceImportStep> steps;
    size_t files = 0;  // audio files found
    std::string error;
};
VoicePreview preview_voice_import(const Project& p, const std::vector<StringLine>& targets, const std::string& folder, const std::string& csv_path,
                                  const std::string& default_language, const VoiceImportOptions& o, const FfmpegInfo* ff);
// a readable report of a match and a plan (the preview of the GUI and the command line)
std::string voice_import_report(const VoiceMatchReport& rep, const std::vector<VoiceImportStep>& steps);

}  // namespace mk
