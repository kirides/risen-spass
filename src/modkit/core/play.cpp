#include "core/play.h"
#include "core/i18n.h"

#include <iterator>
#include <regex>
#include <sstream>

#include "core/log.h"
#include "core/paths.h"

#ifdef _WIN32
#include <windows.h>
#include <tlhelp32.h>
#endif

namespace mk {

PlayPlan play_plan(const std::string& game_dir) {
    PlayPlan p;
    if (game_dir.empty()) {
        p.problem = "Set the game folder (the Risen folder with bin\\Risen.exe) in Window > Settings.";
        return p;
    }
    const fs::path bin = u8(game_dir) / "bin";
    std::error_code ec;
    if (!fs::exists(bin / "Risen.exe", ec)) p.problem = "There is no bin\\Risen.exe in " + game_dir + ".";
    else if (!fs::exists(bin / "spass", ec) && !fs::exists(bin / "sentry.dll", ec)) p.problem = "The mod layer is not installed in " + game_dir + " (scripts\\install.ps1).";
    p.mods_dir = u8str(bin / "spass" / "mods");
    p.log_file = u8str(bin / "spass" / "spass.log");
    return p;
}

#ifdef _WIN32
namespace {
// the process image paths of every Risen.exe
std::vector<fs::path> risen_processes() {
    std::vector<fs::path> out;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return out;
    PROCESSENTRY32W e{};
    e.dwSize = sizeof e;
    for (BOOL ok = Process32FirstW(snap, &e); ok; ok = Process32NextW(snap, &e)) {
        if (_wcsicmp(e.szExeFile, L"Risen.exe") != 0) continue;
        if (HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, e.th32ProcessID)) {
            wchar_t buf[MAX_PATH * 2];
            DWORD n = static_cast<DWORD>(std::size(buf));
            if (QueryFullProcessImageNameW(h, 0, buf, &n)) out.push_back(fs::path(std::wstring(buf, n)));
            CloseHandle(h);
        }
    }
    CloseHandle(snap);
    return out;
}
}  // namespace
#endif

bool game_running(const std::string& game_dir) {
#ifdef _WIN32
    if (game_dir.empty()) return false;
    const fs::path bin = u8(game_dir) / "bin";
    for (auto& exe : risen_processes())
        if (path_within(exe, bin)) return true;
#else
    (void)game_dir;
#endif
    return false;
}

std::string config_with_watch(const std::string& text) {
    // already on?
    if (std::regex_search(text, std::regex(R"(^[ \t]*watch[ \t]*=[ \t]*true\b)", std::regex::multiline))) return text;
    std::vector<std::string> lines;
    std::istringstream in(text);
    for (std::string l; std::getline(in, l);) {
        if (!l.empty() && l.back() == '\r') l.pop_back();
        lines.push_back(l);
    }
    // an existing `watch = false` inside [mods] is replaced, else the key goes right after the [mods] header, else a new table at the end
    bool in_mods = false, done = false;
    size_t header = 0;
    for (size_t i = 0; i < lines.size();) {
        const std::string& l = lines[i];
        const size_t a = l.find_first_not_of(" \t");
        if (a != std::string::npos && l[a] == '[') {
            in_mods = l.compare(a, 6, "[mods]") == 0;
            if (in_mods && !done) {
                header = i;
                done = true;
            }
        } else if (in_mods && std::regex_search(l, std::regex(R"(^\s*watch\s*=)"))) {
            lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(i));
            continue;
        }
        ++i;
    }
    if (done) lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(header) + 1, "watch = true");
    if (!done) {
        if (!lines.empty() && !lines.back().empty()) lines.push_back("");
        lines.push_back("[mods]");
        lines.push_back("watch = true");
    }
    std::string out;
    for (auto& l : lines) out += l + "\n";
    return out;
}

std::string config_local_windowed(const std::string& xml, bool& changed) {
    static const std::regex full(R"(FullScreen\s*=\s*"true")", std::regex::icase);
    const std::string out = std::regex_replace(xml, full, "FullScreen=\"false\"");
    changed = out != xml;
    return out;
}

std::vector<std::pair<std::string, std::string>> autostart_mod_files(PlayStart start) {
    const char* call = start == PlayStart::QuickLoad ? "quick_load" : "new_game";
    std::string lua = "-- written by risen-modkit (Play): starts a game on the first tick\nlocal done = false\nrisen.on('tick', function()\n    if done then return end\n    done = true\n    risen.log.info('modkit autostart " +
                      std::string(call) + " -> ' .. tostring(risen.world." + call + "()))\nend)\n";
    std::string toml = std::string("id = \"") + kAutostartModId + "\"\nname = \"Modkit autostart\"\nversion = \"1.0.0\"\n";
    return {{"mod.toml", toml}, {"main.lua", lua}};
}

PlayResult play_game(const PlayOptions& o) {
    PlayResult r;
    const PlayPlan plan = play_plan(o.game_dir);
    if (!plan.problem.empty()) {
        r.message = plan.problem;
        return r;
    }
    const fs::path bin = u8(o.game_dir) / "bin";
    const fs::path spass = bin / "spass";
    try {
        if (game_running(o.game_dir)) {
            std::string cfg;
            try {
                cfg = read_text(spass / "config.toml");
            } catch (...) {
            }
            r.ok = true;
            r.reloaded = std::regex_search(cfg, std::regex(R"(^[ \t]*watch[ \t]*=[ \t]*true\b)", std::regex::multiline));
            r.message = r.reloaded ? tr("The game is running: the layer reloads the mod from the exported files.")
                                   : tr("The game is running without the file watcher: restart it to load the new files (Play starts it with hot reload on).");
            return r;
        }
        // config.toml: hot reload on for the next start (the rest of the player's settings stays)
        std::string cfg;
        try {
            cfg = read_text(spass / "config.toml");
        } catch (...) {
        }
        write_text_if_changed(spass / "config.toml", config_with_watch(cfg));
        // the helper mod that starts the game
        const fs::path auto_dir = spass / "mods" / kAutostartModId;
        if (o.start == PlayStart::Menu) {
            std::error_code ec;
            fs::remove_all(auto_dir, ec);
        } else {
            for (auto& [file, content] : autostart_mod_files(o.start)) write_text_if_changed(auto_dir / file, content);
        }
        if (o.force_windowed) {
            const fs::path cl = u8(o.game_dir) / "data" / "ini" / "ConfigLocal.xml";
            std::error_code ec;
            if (fs::exists(cl, ec)) {
                bool changed = false;
                const std::string out = config_local_windowed(read_text(cl), changed);
                if (changed) {
                    fs::copy_file(cl, fs::path(cl).replace_extension(".xml.modkit-backup"), fs::copy_options::skip_existing, ec);
                    write_text(cl, out);
                    log_info("play: ConfigLocal.xml set to windowed (backup: ConfigLocal.xml.modkit-backup)");
                }
            }
        }
#ifdef _WIN32
        // Risen restarts itself through Steam when it was not started by it; this makes the Steam API treat the process as started by Steam
        SetEnvironmentVariableW(L"SteamAppId", L"40300");
        SetEnvironmentVariableW(L"SteamGameId", L"40300");
        std::wstring cmd = L"\"" + (bin / "Risen.exe").wstring() + L"\" -nosplash";
        STARTUPINFOW si{};
        si.cb = sizeof si;
        PROCESS_INFORMATION pi{};
        if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, bin.wstring().c_str(), &si, &pi)) {
            r.message = trf("Cannot start Risen.exe (error {0}).", {std::to_string(GetLastError())});
            return r;
        }
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        r.ok = true;
        r.message = o.start == PlayStart::Menu ? tr("Risen is starting (about 30 s).")
                                    : o.start == PlayStart::NewGame ? tr("Risen is starting (about 30 s), then a new game starts.") : tr("Risen is starting (about 30 s), then the last quick save loads.");
#else
        r.message = tr("Starting the game is only supported on Windows.");
#endif
    } catch (const std::exception& e) {
        r.ok = false;
        r.message = trf("Play failed: {0}", {e.what()});
    }
    return r;
}

}  // namespace mk
