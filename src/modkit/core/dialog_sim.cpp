#include "core/dialog_sim.h"

#include <algorithm>
#include <cmath>

#include "core/graph.h"

namespace mk {

std::string leaf_key(const json& leaf) { return describe_condition(leaf); }

namespace {

bool compare(double a, const std::string& op, double b) {
    if (op == "!=" || op == "~=") return a != b;
    if (op == "<") return a < b;
    if (op == ">") return a > b;
    if (op == "<=") return a <= b;
    if (op == ">=") return a >= b;
    return a == b;
}

std::string str(const json& c, size_t i, const char* def = "") { return i < c.size() && c[i].is_string() ? c[i].get<std::string>() : std::string(def); }
double num(const json& c, size_t i, double def) { return i < c.size() && c[i].is_number() ? c[i].get<double>() : def; }
bool is_player(const std::string& who) { return who.empty() || who == "player" || who == "hero"; }

bool leaf_value(const json& c, const SimWorld& w) {
    const std::string k = str(c, 0);
    if (k == "quest") {
        auto it = w.quests.find(str(c, 1));
        return it != w.quests.end() && it->second == str(c, 2, "Success");
    }
    if (k == "item") {
        if (!is_player(str(c, 3))) return false;
        auto it = w.items.find(str(c, 1));
        return it != w.items.end() && it->second >= static_cast<int>(num(c, 2, 1));
    }
    if (k == "var") {
        auto it = w.vars.find(str(c, 1));
        if (it == w.vars.end()) return false;
        if (c.size() < 3 || !c[2].is_number()) return true;
        return compare(it->second, str(c, 3, "=="), c[2].get<double>());
    }
    if (k == "event") return w.events.count(str(c, 1)) > 0;
    if (k == "skill") {
        if (!is_player(str(c, 4))) return false;
        auto it = w.skills.find(str(c, 1));
        return compare(it == w.skills.end() ? 0 : it->second, str(c, 3, ">="), num(c, 2, 1));
    }
    if (k == "guild") return is_player(str(c, 2)) && w.guilds.count(str(c, 1)) > 0;
    if (k == "dead") return w.dead.count(str(c, 1)) > 0;
    if (k == "alive") return w.dead.count(str(c, 1)) == 0;
    return false;
}

}  // namespace

bool eval_condition(const json& c, const SimWorld& w) {
    if (!c.is_array() || c.empty() || !c[0].is_string()) return true;
    const std::string k = c[0].get<std::string>();
    if (k == "all" || k == "any") {
        const json list = c.size() > 1 && c[1].is_array() ? c[1] : json::array();
        const bool all = k == "all";
        for (auto& e : list) {
            const bool v = eval_condition(e, w);
            if (all && !v) return false;
            if (!all && v) return true;
        }
        return all;
    }
    if (k == "not") return !(c.size() > 1 && eval_condition(c[1], w));
    auto f = w.forced.find(leaf_key(c));
    if (f != w.forced.end()) return f->second;
    return leaf_value(c, w);
}

void collect_leaves(const json& c, std::vector<json>& out) {
    if (!c.is_array() || c.empty() || !c[0].is_string()) return;
    const std::string k = c[0].get<std::string>();
    if (k == "all" || k == "any") {
        if (c.size() > 1 && c[1].is_array())
            for (auto& e : c[1]) collect_leaves(e, out);
        return;
    }
    if (k == "not") {
        if (c.size() > 1) collect_leaves(c[1], out);
        return;
    }
    const std::string key = leaf_key(c);
    for (auto& l : out)
        if (leaf_key(l) == key) return;
    out.push_back(c);
}

DialogSim::DialogSim(const Project& p, json infos, json actions, std::string language) : infos_(std::move(infos)), actions_(std::move(actions)) {
    if (language.empty() && !p.mod.languages.empty()) language = p.mod.languages.front();
    lang_code_ = language_code(language);
    if (!infos_.is_array()) infos_ = json::array();
    for (auto& i : infos_)
        if (i.contains("when")) collect_leaves(i["when"], leaves_);
    restart();
}

const json* DialogSim::find(const std::string& info) const {
    for (auto& i : infos_)
        if (i.value("name", std::string()) == info) return &i;
    return nullptr;
}

bool DialogSim::available(const json& info) const {
    if (used_.count(info.value("name", std::string())) && !info.value("permanent", false)) return false;
    return !info.contains("when") || eval_condition(info["when"], world);
}

std::string DialogSim::text_of(const json& t) const {
    if (t.is_string()) return t.get<std::string>();
    if (t.is_object()) {
        auto it = t.find(lang_code_);
        if (it != t.end() && it->is_string()) return it->get<std::string>();
        it = t.find("default");
        if (it != t.end() && it->is_string()) return it->get<std::string>();
    }
    return {};
}

void DialogSim::reset_all() {
    world = SimWorld{};
    used_.clear();
    restart();
}

void DialogSim::restart() {
    transcript_.clear();
    current_.clear();
    ended_ = false;
    auto_start();
}

void DialogSim::auto_start() {
    // an Important option opens the conversation: the first one that is on offer
    for (auto& i : infos_)
        if (i.value("type", std::string("Info")) == "Important" && !i.contains("parent") && available(i)) {
            run(i);
            return;
        }
}

void DialogSim::leave() {
    if (ended_) return;
    ended_ = true;
    transcript_.push_back({SimLine::Kind::End, "", "(you walk away)"});
}

void DialogSim::run(const json& info) {
    used_.insert(info.value("name", std::string()));
    auto effect = [&](const std::string& t) { transcript_.push_back({SimLine::Kind::Effect, "", t}); };
    bool first = true, back = false;
    for (auto& l : info.value("lines", json::array())) {
        if (l.contains("speaker")) {
            const std::string sp = l.value("speaker", std::string("npc"));
            SimLine s;
            s.kind = first || sp == "player" ? SimLine::Kind::Player : sp == "npc" ? SimLine::Kind::Npc : SimLine::Kind::Other;
            if (s.kind == SimLine::Kind::Other) s.who = sp;
            s.text = text_of(l.value("text", json()));
            transcript_.push_back(s);
            first = false;
            continue;
        }
        first = false;
        if (l.contains("script")) {
            const std::string script = l["script"].get<std::string>();
            bool known = false;
            for (auto& a : actions_) {
                if (a.value("script", std::string()) != script) continue;
                known = true;
                const std::string q = a.value("quest", std::string()), act = a.value("action", std::string());
                if (act == "start") world.quests[q] = "Running";
                else if (act == "succeed") world.quests[q] = "Success";
                else if (act == "fail") world.quests[q] = "Failed";
                else if (act == "cancel") world.quests[q] = "Cancelled";
                effect("quest " + q + ": " + act);
            }
            if (!known) effect("script " + script);
        } else if (l.contains("log")) {
            effect("log entry (" + l["log"].value("quest", std::string()) + "): " + l["log"].value("text", std::string()));
        } else if (l.contains("command")) {
            const std::string cmd = l["command"].get<std::string>();
            if (cmd == "End") {
                ended_ = true;
                transcript_.push_back({SimLine::Kind::End, "", "(the conversation ends)"});
                return;
            }
            if (cmd == "Back") {
                back = true;
                effect("back to the start");
            } else if (cmd == "SetGameEvent") {
                world.events.insert(l.value("GameEvent", std::string()));
                effect("event " + l.value("GameEvent", std::string()) + " set");
            } else if (cmd == "ClearGameEvent") {
                world.events.erase(l.value("GameEvent", std::string()));
                effect("event " + l.value("GameEvent", std::string()) + " cleared");
            } else if (cmd == "RunQuest" || cmd == "SucceedQuest" || cmd == "CloseQuest") {
                const std::string q = l.value("QuestName", std::string());
                world.quests[q] = cmd == "RunQuest" ? "Running" : "Success";
                effect(cmd + " " + q);
            } else if (cmd == "Give") {
                const std::string item = l.value("Item", std::string());
                const int amount = l.contains("Amount") && l["Amount"].is_number() ? l["Amount"].get<int>() : 1;
                const std::string to = l.value("Entity2", std::string());
                if (is_player(to) && !item.empty()) world.items[item] += amount;
                effect("Give " + std::to_string(amount) + " " + item + (to.empty() ? "" : " to " + to));
            } else if (cmd == "Say" || cmd == "SayConfirm" || cmd == "SaySVM" || cmd == "SaySound") {
                const std::string t = text_of(l.value("text", json()));
                if (!t.empty()) transcript_.push_back({SimLine::Kind::Npc, "", t});
                else effect(cmd);
            } else {
                effect("command " + cmd);
            }
        }
    }
    // what follows: the sub-options; else the parent's list stays (the engine's sub-menu) unless Back returns to the start
    const std::string parent = info.value("parent", std::string());
    current_ = info.value("name", std::string());
    if (!options().empty()) return;
    current_ = back ? std::string() : parent;
    if (options().empty()) current_.clear();
}

std::vector<SimOption> DialogSim::options() const {
    std::vector<SimOption> out;
    if (ended_) return out;
    for (auto& i : infos_) {
        const std::string parent = i.value("parent", std::string());
        if (parent != current_) continue;
        const std::string type = i.value("type", std::string("Info"));
        if (type == "Important" || !available(i)) continue;
        SimOption o;
        o.info = i.value("name", std::string());
        o.type = type;
        o.permanent = i.value("permanent", false);
        const json lines = i.value("lines", json::array());
        if (!lines.empty()) o.text = text_of(lines[0].value("text", json()));
        out.push_back(std::move(o));
    }
    return out;
}

bool DialogSim::choose(const std::string& info) {
    if (ended_) return false;
    const auto opts = options();
    if (std::none_of(opts.begin(), opts.end(), [&](const SimOption& o) { return o.info == info; })) return false;
    const json* i = find(info);
    if (!i) return false;
    run(*i);
    return true;
}

std::vector<std::string> DialogSim::unreachable() const {
    std::vector<std::string> out;
    for (auto& i : infos_) {
        const std::string parent = i.value("parent", std::string());
        if (!parent.empty() && !find(parent)) out.push_back(i.value("name", std::string()));
    }
    return out;
}

}  // namespace mk
