#include "core/audio_convert.h"

#include <cstdio>
#include <cstdlib>
#include <system_error>

#include "core/assets.h"
#include "core/paths.h"
#include "pack2mod/util.h"

namespace mk {

namespace {

std::string tail(const std::string& s, size_t lines = 6) {
    size_t pos = s.size(), n = 0;
    while (pos > 0 && n <= lines) {
        pos = s.rfind('\n', pos - 1);
        if (pos == std::string::npos) {
            pos = 0;
            break;
        }
        ++n;
    }
    std::string t = s.substr(pos);
    while (!t.empty() && (t.front() == '\n' || t.front() == '\r')) t.erase(t.begin());
    return t;
}

std::string ext_of(const std::string& f) {
    const std::string n = p2m::lower(p2m::bare_name(f));
    const auto dot = n.rfind('.');
    return dot == std::string::npos ? std::string() : n.substr(dot + 1);
}

bool is_file(const std::string& p) {
    std::error_code ec;
    return fs::is_regular_file(u8(p), ec);
}

}  // namespace

std::string locate_ffmpeg(const std::string& configured, std::string* error) {
    if (!configured.empty()) {
        if (is_file(configured)) return configured;
        if (error) *error = "the configured ffmpeg '" + configured + "' does not exist";
        return {};
    }
    const char* path = std::getenv("PATH");
#ifdef _WIN32
    return find_in_path("ffmpeg", path ? path : "", is_file, true);
#else
    return find_in_path("ffmpeg", path ? path : "", is_file, false);
#endif
}

FfmpegInfo probe_ffmpeg(const std::string& exe, const ProcessRunner& run) {
    FfmpegInfo f;
    f.path = exe;
    if (exe.empty()) {
        f.error = "ffmpeg was not found";
        return f;
    }
    const ProcessResult v = run({exe, "-hide_banner", "-version"}, nullptr);
    if (!v.started) {
        f.error = v.error.empty() ? "ffmpeg could not be started" : v.error;
        return f;
    }
    const auto at = v.output.find("ffmpeg version ");
    if (v.exit_code != 0 || at == std::string::npos) {
        f.error = exe + " is not ffmpeg (no version line): " + tail(v.output, 2);
        return f;
    }
    const size_t s = at + 15, e = v.output.find_first_of(" \r\n", s);
    f.version = v.output.substr(s, e == std::string::npos ? std::string::npos : e - s);
    const ProcessResult enc = run({exe, "-hide_banner", "-encoders"}, nullptr);
    f.libvorbis = enc.started && enc.output.find(" libvorbis ") != std::string::npos;
    f.ok = true;
    return f;
}

std::string ffmpeg_help() {
#ifdef _WIN32
    return "ffmpeg is not bundled. Get a Windows build (https://www.gyan.dev/ffmpeg/builds/ or https://github.com/BtbN/FFmpeg-Builds/releases, or "
           "'winget install Gyan.FFmpeg'), unpack it and either add its bin folder to PATH or set the path of ffmpeg.exe in Settings > Audio.";
#elif defined(__APPLE__)
    return "ffmpeg is not bundled. Install it with Homebrew ('brew install ffmpeg') or MacPorts, or set the path of an ffmpeg binary in Settings > Audio.";
#else
    return "ffmpeg is not bundled. Install it with your package manager ('sudo apt install ffmpeg', 'sudo dnf install ffmpeg', 'sudo pacman -S ffmpeg') "
           "or set the path of an ffmpeg binary in Settings > Audio.";
#endif
}

std::string converted_ext(const FfmpegInfo& ff) { return ff.libvorbis ? "ogg" : "wav"; }

std::vector<std::string> ffmpeg_convert_args(const FfmpegInfo& ff, const std::string& in, const std::string& out, const ConvertOptions& o) {
    std::vector<std::string> a = {ff.path, "-hide_banner", "-nostdin", "-y", "-loglevel", "error", "-i", in, "-vn", "-map_metadata", "-1"};
    if (o.normalize) {
        char buf[64];
        std::snprintf(buf, sizeof buf, "loudnorm=I=%.1f:TP=-1.5:LRA=11", o.loudness);
        a.insert(a.end(), {"-af", buf});
    }
    if (o.mono) a.insert(a.end(), {"-ac", "1"});
    if (o.sample_rate > 0) a.insert(a.end(), {"-ar", std::to_string(o.sample_rate)});
    if (ff.libvorbis) a.insert(a.end(), {"-c:a", "libvorbis", "-q:a", std::to_string(std::clamp(o.quality, 0, 10)), "-f", "ogg"});
    else a.insert(a.end(), {"-c:a", "pcm_s16le", "-f", "wav"});
    a.push_back(out);
    return a;
}

bool plays_as_is(const std::string& bytes, std::string& why) {
    const auto s = open_sound(bytes, why);
    if (!s) return false;
    if (s->compiled) {
        why = "a compiled game sound (._xsnd) is no recording";
        return false;
    }
    return true;
}

std::string convert_audio(const FfmpegInfo& ff, const std::string& in, const std::string& out, const ConvertOptions& o, const ProcessRunner& run,
                          const std::atomic<bool>* cancel) {
    if (!ff.ok) return ff.error.empty() ? std::string("ffmpeg is not available") : ff.error;
    std::error_code ec;
    fs::create_directories(u8(out).parent_path(), ec);
    const std::string tmp = out + ".part";
    auto args = ffmpeg_convert_args(ff, in, tmp, o);
    const ProcessResult r = run(args, cancel);
    if (r.cancelled) {
        fs::remove(u8(tmp), ec);
        return "cancelled";
    }
    if (!r.started) return r.error;
    if (r.exit_code != 0) {
        fs::remove(u8(tmp), ec);
        return "ffmpeg failed (exit code " + std::to_string(r.exit_code) + "): " + tail(r.output);
    }
    std::string bytes, why;
    try {
        bytes = read_text(u8(tmp));
    } catch (const std::exception& e) {
        return std::string("ffmpeg wrote no file: ") + e.what();
    }
    if (!plays_as_is(bytes, why)) {
        fs::remove(u8(tmp), ec);
        return "the converted file does not play: " + why;
    }
    fs::rename(u8(tmp), u8(out), ec);
    if (ec) {
        fs::remove(u8(out), ec);
        fs::rename(u8(tmp), u8(out), ec);
        if (ec) return "cannot write " + out + ": " + ec.message();
    }
    return {};
}

std::vector<VoiceImportStep> plan_voice_import(const VoiceMatchReport& rep, const VoiceImportOptions& o, const FfmpegInfo* ff,
                                               const std::function<std::string(const std::string&)>& read_file) {
    std::vector<VoiceImportStep> steps;
    const bool have = ff && ff->ok;
    for (const VoiceMatch& m : rep.matched) {
        VoiceImportStep s;
        s.match = m;
        std::string bytes, why;
        try {
            bytes = read_file(m.file);
        } catch (const std::exception& e) {
            s.skip = true;
            s.why = std::string("cannot read the file: ") + e.what();
            steps.push_back(s);
            continue;
        }
        const bool plays = plays_as_is(bytes, why);
        const bool want = o.convert_all || o.convert.normalize;
        if (plays && !(want && have)) {
            s.convert = false;
            s.why = want ? "plays as it is (no ffmpeg for the requested processing)" : "plays as it is";
            s.dest_rel = o.folder + "/" + voice_asset_name(m.line_id, m.language, ext_of(m.file));
        } else if (have) {
            s.convert = true;
            s.why = plays ? "processed (mono / rate / loudness)" : why;
            s.dest_rel = o.folder + "/" + voice_asset_name(m.line_id, m.language, converted_ext(*ff));
        } else {
            s.skip = true;
            s.why = why + " - it needs a conversion and ffmpeg was not found. " + ffmpeg_help();
        }
        steps.push_back(s);
    }
    return steps;
}

std::vector<ImportedVoice> run_voice_import(std::vector<VoiceImportStep>& steps, const std::string& project_dir, const VoiceImportOptions& o,
                                            const FfmpegInfo* ff, const ProcessRunner& run, const std::function<void(size_t, size_t, const std::string&)>& progress,
                                            const std::atomic<bool>* cancel) {
    std::vector<ImportedVoice> out;
    const fs::path base = u8(project_dir);
    size_t i = 0;
    for (auto& s : steps) {
        if (progress) progress(i, steps.size(), s.match.file);
        ++i;
        if (s.skip) continue;
        if (cancel && cancel->load()) {
            s.error = "cancelled";
            continue;
        }
        const fs::path dest = base / u8(s.dest_rel);
        std::error_code ec;
        fs::create_directories(dest.parent_path(), ec);
        if (s.convert) {
            s.error = convert_audio(*ff, s.match.file, u8str(dest), o.convert, run, cancel);
        } else {
            fs::copy_file(u8(s.match.file), dest, fs::copy_options::overwrite_existing, ec);
            if (ec) s.error = "cannot copy: " + ec.message();
        }
        if (!s.error.empty()) continue;
        s.done = true;
        out.push_back({s.match.node_uid, s.match.language, s.dest_rel});
    }
    if (progress) progress(steps.size(), steps.size(), "");
    return out;
}

VoicePreview preview_voice_import(const Project& p, const std::vector<StringLine>& targets, const std::string& folder, const std::string& csv_path,
                                  const std::string& default_language, const VoiceImportOptions& o, const FfmpegInfo* ff) {
    VoicePreview pv;
    std::vector<std::string> files;
    std::error_code ec;
    if (!fs::is_directory(u8(folder), ec)) {
        pv.error = "the folder " + folder + " does not exist";
        return pv;
    }
    for (auto it = fs::recursive_directory_iterator(u8(folder), fs::directory_options::skip_permission_denied, ec); !ec && it != fs::recursive_directory_iterator();
         it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        const std::string f = u8str(it->path());
        if (is_audio_candidate(f)) files.push_back(f);
    }
    std::sort(files.begin(), files.end());
    pv.files = files.size();
    std::vector<CsvAssignment> csv;
    if (!csv_path.empty()) {
        try {
            std::string err;
            csv = csv_assignments(read_text(u8(csv_path)), err);
            if (!err.empty()) {
                pv.error = csv_path + ": " + err;
                return pv;
            }
        } catch (const std::exception& e) {
            pv.error = e.what();
            return pv;
        }
    }
    pv.rep = match_recordings(p, targets, files, p.mod.languages, default_language, csv_path.empty() ? nullptr : &csv);
    pv.steps = plan_voice_import(pv.rep, o, ff, [](const std::string& f) { return read_text(u8(f)); });
    return pv;
}

std::string voice_import_report(const VoiceMatchReport& rep, const std::vector<VoiceImportStep>& steps) {
    std::string s;
    size_t conv = 0, copy = 0, skip = 0;
    for (auto& st : steps) (st.skip ? skip : st.convert ? conv : copy)++;
    s += std::to_string(rep.matched.size()) + " matched (" + std::to_string(copy) + " copied, " + std::to_string(conv) + " converted, " + std::to_string(skip) +
         " cannot be imported), " + std::to_string(rep.unmatched.size()) + " unmatched, " + std::to_string(rep.ambiguous.size()) + " ambiguous, " +
         std::to_string(rep.duplicates.size()) + " duplicate\n";
    for (auto& st : steps) {
        s += (st.skip ? "  SKIP    " : st.convert ? "  CONVERT " : "  COPY    ") + p2m::bare_name(st.match.file) + " -> " + st.match.line_id + " / " + st.match.language;
        if (!st.dest_rel.empty()) s += "  (" + st.dest_rel + ")";
        s += "  [" + st.match.how + "] " + st.why;
        if (!st.error.empty()) s += "  ERROR: " + st.error;
        s += "\n";
    }
    for (auto& [f, why] : rep.unmatched) s += "  UNMATCHED " + p2m::bare_name(f) + ": " + why + "\n";
    for (auto& [f, c] : rep.ambiguous) {
        s += "  AMBIGUOUS " + p2m::bare_name(f) + ": could be";
        for (auto& x : c) s += " " + x;
        s += "\n";
    }
    for (auto& [k, fs_] : rep.duplicates) {
        s += "  DUPLICATE " + k + ":";
        for (auto& f : fs_) s += " " + p2m::bare_name(f);
        s += " (none is taken)\n";
    }
    return s;
}

}  // namespace mk
