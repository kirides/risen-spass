// The game's own log (<Risen>\bin\spass\spass.log) as a source of problems: the editor tails it while the game runs, keeps the warnings and
// errors that belong to the mod and links each one back to the project object it names (generated names: the info name of a dialogue
// option, the MK_<quest>_<action> script, template and NPC names, quest names).
#pragma once

#include <string>
#include <utility>
#include <vector>

#include "core/project.h"
#include "core/validate.h"

namespace mk {

struct GameLogLine {
    std::string time;   // "12:34:56.789"
    std::string level;  // spdlog level: "error", "warning", "info", ...
    std::string text;   // everything after "[tN] "
};

// "[HH:MM:SS.mmm] [level] [tN] text"; false for a line in another shape (continuation lines, crash banners)
bool parse_game_log_line(const std::string& line, GameLogLine& out);

// What a game log line names inside the project.
struct GameLogRef {
    std::string area;     // the panel the object lives in ("Items", "Dialogues", ...)
    std::string uid;
    std::string subject;  // as the Problems panel shows it
};

// Name -> object for every name the export generates from the project (items, recipes, NPC templates, placed NPCs, dialogue owners and
// info names, quest names and their MK_<quest>_<action> scripts, behaviours).
class ProjectNameIndex {
public:
    explicit ProjectNameIndex(const Project& p);
    // the first project object whose name occurs in `text` as a whole word; nullptr when none
    const GameLogRef* find(const std::string& text) const;
    size_t size() const { return names_.size(); }

private:
    std::vector<std::pair<std::string, GameLogRef>> names_;
};

// The warnings and errors of `lines` that concern the mod: they carry "[<mod id>]" or name a project object. Each becomes a Problem
// (area = the object's panel or "Game", uid = the object). Identical consecutive messages are merged.
std::vector<Problem> game_log_problems(const Project& p, const std::vector<GameLogLine>& lines);

// Reads a growing log file: new complete lines since the last call. A file that became smaller (the game started again and truncated it)
// is read from the start; `reset` is then true.
class LogTail {
public:
    explicit LogTail(std::string path = {}) : path_(std::move(path)) {}
    void set_path(std::string path);
    // appends the new parsed lines to `out`; `reset` tells that `out` should be cleared first
    void poll(std::vector<GameLogLine>& out, bool& reset);
    const std::string& path() const { return path_; }

private:
    std::string path_;
    unsigned long long offset_ = 0;
    std::string partial_;
};

}  // namespace mk
