#include "core/gamelog.h"

#include <algorithm>
#include <cctype>
#include <fstream>

#include "core/dialog.h"
#include "core/paths.h"

namespace mk {

bool parse_game_log_line(const std::string& line, GameLogLine& out) {
    // [12:34:56.789] [error] [t1234] text
    if (line.size() < 20 || line[0] != '[') return false;
    const size_t e1 = line.find(']');
    if (e1 == std::string::npos || e1 < 9) return false;
    for (size_t i = 1; i < e1; ++i)
        if (!std::isdigit(static_cast<unsigned char>(line[i])) && line[i] != ':' && line[i] != '.') return false;
    if (e1 + 3 >= line.size() || line[e1 + 1] != ' ' || line[e1 + 2] != '[') return false;
    const size_t e2 = line.find(']', e1 + 3);
    if (e2 == std::string::npos) return false;
    out.time = line.substr(1, e1 - 1);
    out.level = line.substr(e1 + 3, e2 - e1 - 3);
    size_t t = e2 + 1;
    if (t + 2 < line.size() && line[t] == ' ' && line[t + 1] == '[' && line[t + 2] == 't') {
        const size_t e3 = line.find(']', t);
        if (e3 != std::string::npos) t = e3 + 1;
    }
    while (t < line.size() && line[t] == ' ') ++t;
    out.text = line.substr(t);
    while (!out.text.empty() && (out.text.back() == '\r' || out.text.back() == '\n')) out.text.pop_back();
    return true;
}

namespace {

bool ident_char(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-'; }

// `name` occurs in `text` bounded by non-identifier characters (`prefix`: only the left side is bounded, "MK_<quest>_" is followed by the action)
bool has_word(const std::string& text, const std::string& name, bool prefix) {
    for (size_t pos = text.find(name); pos != std::string::npos; pos = text.find(name, pos + 1)) {
        const bool l = pos == 0 || !ident_char(text[pos - 1]);
        const size_t end = pos + name.size();
        const bool r = prefix || end >= text.size() || !ident_char(text[end]);
        if (l && r) return true;
    }
    return false;
}

bool is_script_prefix(const std::string& n) { return n.size() > 3 && n.rfind("MK_", 0) == 0 && n.back() == '_'; }

}  // namespace

ProjectNameIndex::ProjectNameIndex(const Project& p) {
    auto add = [&](const std::string& name, const char* area, const std::string& uid, const std::string& subject) {
        if (name.size() < 3) return;
        names_.push_back({name, GameLogRef{area, uid, subject}});
    };
    for (auto& i : p.items) add(i.name, "Items", i.uid, i.name);
    for (auto& r : p.recipes) add(r.name, "Recipes", r.uid, r.name);
    for (auto& t : p.npc_templates) add(t.name, "NPCs", t.uid, t.name);
    for (auto& n : p.npcs) add(n.name, "NPCs", n.uid, n.name);
    for (auto& b : p.behaviors) add(b.name, "Behaviours", b.uid, b.name);
    for (auto& d : p.dialogs) {
        for (auto& n : d.graph.nodes)
            if (n.type == "option") add(info_name(p, n), "Dialogues", d.uid, d.name);
    }
    for (auto& q : p.quests) {
        add("MK_" + q.name + "_", "Quests", q.uid, q.name);
        add(q.name, "Quests", q.uid, q.name);
    }
    // longest names first: "My_Knife_Plan" must win over "My_Knife"
    std::stable_sort(names_.begin(), names_.end(), [](auto& a, auto& b) { return a.first.size() > b.first.size(); });
}

const GameLogRef* ProjectNameIndex::find(const std::string& text) const {
    for (auto& [name, ref] : names_)
        if (has_word(text, name, is_script_prefix(name))) return &ref;
    return nullptr;
}

std::vector<Problem> game_log_problems(const Project& p, const std::vector<GameLogLine>& lines) {
    const ProjectNameIndex idx(p);
    const std::string tag = "[" + p.mod.id + "]";
    std::vector<Problem> out;
    std::string last_text;
    for (auto& l : lines) {
        const bool err = l.level == "error" || l.level == "critical";
        const bool warn = l.level == "warning" || l.level == "warn";
        if (!err && !warn) continue;
        const GameLogRef* ref = idx.find(l.text);
        const bool ours = !p.mod.id.empty() && l.text.find(tag) != std::string::npos;
        if (!ref && !ours) continue;
        if (!out.empty() && l.text == last_text) continue;
        last_text = l.text;
        Problem pr;
        pr.severity = err ? Severity::Error : Severity::Warning;
        pr.area = ref ? ref->area : "Game";
        pr.uid = ref ? ref->uid : std::string();
        pr.subject = ref ? ref->subject : p.mod.id;
        pr.message = l.time + "  " + l.text;
        out.push_back(std::move(pr));
    }
    return out;
}

void LogTail::set_path(std::string path) {
    if (path == path_) return;
    path_ = std::move(path);
    offset_ = 0;
    partial_.clear();
}

void LogTail::poll(std::vector<GameLogLine>& out, bool& reset) {
    reset = false;
    if (path_.empty()) return;
    std::error_code ec;
    const auto size = fs::file_size(u8(path_), ec);
    if (ec) {
        if (offset_ != 0) {
            offset_ = 0;
            partial_.clear();
            reset = true;
        }
        return;
    }
    if (size < offset_) {
        offset_ = 0;
        partial_.clear();
        reset = true;
    }
    if (size == offset_) return;
    std::ifstream f(u8(path_), std::ios::binary);
    if (!f) return;
    f.seekg(static_cast<std::streamoff>(offset_));
    std::string chunk(static_cast<size_t>(size - offset_), '\0');
    f.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
    chunk.resize(static_cast<size_t>(f.gcount()));
    offset_ += chunk.size();
    partial_ += chunk;
    size_t start = 0;
    for (size_t nl; (nl = partial_.find('\n', start)) != std::string::npos; start = nl + 1) {
        GameLogLine l;
        if (parse_game_log_line(partial_.substr(start, nl - start), l)) out.push_back(std::move(l));
    }
    partial_.erase(0, start);
}

}  // namespace mk
