#include "core/events.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <set>

namespace mk {

namespace {

using F = EvFieldDef;

F fint(const char* key, const char* label, int def, double mn = -1e9, double mx = 1e9, const char* help = nullptr) {
    F f;
    f.key = key, f.label = label, f.kind = EvField::Int, f.def = def, f.min = mn, f.max = mx, f.help = help;
    return f;
}
F fnum(const char* key, const char* label, double def, double mn = -1e9, double mx = 1e9, const char* help = nullptr) {
    F f;
    f.key = key, f.label = label, f.kind = EvField::Number, f.def = def, f.min = mn, f.max = mx, f.help = help;
    return f;
}
F fstr(const char* key, const char* label, const char* def = "", const char* help = nullptr) {
    F f;
    f.key = key, f.label = label, f.kind = EvField::String, f.def = def, f.help = help;
    return f;
}
F fbool(const char* key, const char* label, bool def, const char* help = nullptr) {
    F f;
    f.key = key, f.label = label, f.kind = EvField::Bool, f.def = def, f.help = help;
    return f;
}
F fcombo(const char* key, const char* label, std::vector<std::string> options, const char* def, const char* help = nullptr) {
    F f;
    f.key = key, f.label = label, f.kind = EvField::Combo, f.def = def, f.options = std::move(options), f.help = help;
    return f;
}
F fkind(EvField k, const char* key, const char* label, const char* help = nullptr) {
    F f;
    f.key = key, f.label = label, f.kind = k, f.def = k == EvField::Text ? json::object() : json(""), f.help = help;
    return f;
}
F req(F f) {
    f.required = true;
    return f;
}
F when(F f, const char* w) {
    f.when = w;
    return f;
}

const std::vector<std::string> kOps = {"==", "~=", ">=", "<=", ">", "<"};
const std::vector<std::string> kStatus = {"Open", "Running", "Success", "Failed", "Obsolete", "Cancelled", "Lost", "Won"};

std::vector<F> position_fields(const char* point_label) {
    return {fcombo("at", point_label, {"point", "entity"}, "point", "A point of the world (x, y, z in game units, 100 = 1 m) or the position of a named entity."),
            when(fnum("x", "X", 0), "at=point"), when(fnum("y", "Y", 0), "at=point"), when(fnum("z", "Z", 0), "at=point"),
            when(req(fstr("entity", "Entity", "", "The unique name of an NPC or object.")), "at=entity")};
}

std::vector<EvNodeSpec> make_specs() {
    std::vector<EvNodeSpec> v;
    auto add = [&](EvNodeSpec s) { v.push_back(std::move(s)); };
    constexpr unsigned kTrig = 0xff3a8a3a, kCond = 0xff2a7a7a, kFlow = 0xff7a5a9a, kAct = 0xff3a6ea5, kEnd = 0xffa53a3a;
    // ---- triggers
    add({"on_session", "Triggers", "On game start / load", "Fires when a world is ready: a new game, a loaded savegame, or both.", kTrig, true, {"next"},
         {fcombo("mode", "When", {"new", "load", "any"}, "new")}});
    add({"on_chapter", "Triggers", "On chapter", "Fires when the story chapter changes (looked at about every half second). Chapter 0 = any change.", kTrig, true, {"next"},
         {fint("chapter", "Chapter", 0, 0, 20, "0 = every change")}});
    add({"on_kill", "Triggers", "On kill", "Fires when an NPC or monster dies: a template (every wolf) or one NPC by unique name.", kTrig, true, {"next"},
         {req(fkind(EvField::Target, "target", "Killed")), fbool("by_player", "Only if the player killed it", false)}});
    add({"on_move", "Triggers", "On player moved",
         "Fires when the player has moved at least this many metres since the last time it fired (or since the world was ready). Turning and tiny steps never fire it.", kTrig, true,
         {"next"}, {fnum("min_m", "Metres", 1, 1, 100000, "At least 1 m: smaller values would only measure noise.")}});
    {
        EvNodeSpec s{"on_area", "Triggers", "On enter / leave area", "Fires when the player enters or leaves a sphere around a point or a named entity. Judged on 1 m steps of movement.", kTrig,
                     true, {"next"}, {}};
        for (auto& f : position_fields("Centre")) s.fields.push_back(f);
        s.fields.push_back(fnum("radius", "Radius (m)", 5, 1, 100000));
        s.fields.push_back(fcombo("on", "Fires when the player", {"enters", "leaves", "enters or leaves"}, "enters"));
        add(std::move(s));
    }
    add({"on_quest", "Triggers", "On quest status", "Fires when a quest changes its status (any quest when the name is empty).", kTrig, true, {"next"},
         {fkind(EvField::Quest, "quest", "Quest"), fcombo("status", "Status", {"any", "Open", "Running", "Success", "Failed", "Obsolete", "Cancelled", "Lost", "Won"}, "Success")}});
    add({"on_item", "Triggers", "On item", "Fires when the player's inventory changes in the chosen way (any item when the name is empty).", kTrig, true, {"next"},
         {fkind(EvField::Item, "item", "Item"),
          fcombo("what", "What happened", {"picked_up", "dropped", "equipped", "unequipped", "added", "removed", "consumed"}, "picked_up",
                 "added / removed: every change of the amount, whatever the cause (picked_up: from the ground, dropped: thrown away).")}});
    add({"on_dialogue", "Triggers", "On dialogue option", "Fires when a dialogue option finished playing.", kTrig, true, {"next"}, {req(fkind(EvField::Info, "info", "Option"))}});
    add({"on_time", "Triggers", "On time of day", "Fires when the in-game clock reaches the time (once per game minute at most).", kTrig, true, {"next"},
         {fint("hour", "Hour", 8, 0, 23), fint("minute", "Minute", 0, 0, 59)}});
    add({"on_variable", "Triggers", "On variable changed", "Fires when a variable was set (a game variable, or a mod variable of Set variable nodes). With a comparison only when the new value passes it.",
         kTrig, true, {"next"},
         {fcombo("scope", "Kind", {"game", "save"}, "game", "game: an engine game variable (dialogues and quests see it too); save: a mod variable kept in the savegame."),
          req(fstr("name", "Variable")), fcombo("op", "Fires", {"changed", "==", "~=", ">=", "<=", ">", "<"}, "changed"), when(fint("value", "Value", 1), "op!=changed")}});
    add({"on_timer", "Triggers", "On timer", "Fires after this many seconds of play (the clock stops in menus and is saved with the game); repeating fires again and again.", kTrig, true,
         {"next"}, {fnum("seconds", "Seconds", 60, 1, 1e7), fbool("repeat", "Repeat", false)}});
    add({"on_signal", "Triggers", "On signal", "Fires when a Send signal node (of any graph) sends this name: connects graphs.", kTrig, true, {"next"}, {req(fstr("name", "Signal"))}});
    // ---- conditions and flow
    add({"condition", "Conditions", "Condition", "Tests one or more checks; goes on at 'then' when they hold, else at 'else'. Chain Condition nodes for nested and / or.", kCond, false,
         {"then", "else"}, {fcombo("mode", "Combine", {"all", "any"}, "all", "all: every check must hold; any: one is enough."), fbool("negate", "Negate the result", false)}});
    add({"once", "Flow", "Once", "Lets the first pass through at 'next' (per savegame); every later pass goes to 'again'.", kFlow, false, {"next", "again"}, {}});
    add({"count", "Flow", "After N passes", "Passes at 'next' only on every N-th arrival (N = 3: the third, with Repeat also the sixth...); counted per savegame.", kFlow, false, {"next"},
         {fint("n", "N", 3, 1, 1000000), fbool("repeat", "Every N-th time (else only the N-th)", false)}});
    add({"cooldown", "Flow", "Cooldown", "Passes at most once per this many seconds of play; arrivals in between are dropped.", kFlow, false, {"next"}, {fnum("seconds", "Seconds", 10, 0, 1e7)}});
    add({"delay", "Flow", "Delay", "Goes on after this many seconds of play. A pending delay is part of the savegame.", kFlow, false, {"next"}, {fnum("seconds", "Seconds", 5, 0, 1e7)}});
    add({"repeat", "Flow", "Repeat", "Runs the 'body' branch N times in a row, then 'done'.", kFlow, false, {"body", "done"}, {fint("count", "Times", 3, 1, 50)}});
    add({"sequence", "Flow", "Sequence", "Runs outputs 1, 2, 3, 4 one after the other. (An output linked to several nodes runs them all, top to bottom.)", kFlow, false, {"1", "2", "3", "4"}, {}});
    add({"end", "Flow", "End", "Ends this path.", kEnd, false, {}, {}});
    // ---- actions
    add({"set_variable", "Actions", "Set variable", "Sets, adds to or clears a variable. game: an engine game variable (whole numbers; dialogue and quest conditions read it); save: a mod variable kept in the savegame.",
         kAct, false, {"next"},
         {fcombo("scope", "Kind", {"game", "save"}, "game"), req(fstr("name", "Variable")), fcombo("op", "Operation", {"set", "add", "clear"}, "set"), when(fint("value", "Value", 1), "op!=clear")}});
    {
        EvNodeSpec s{"spawn_npc", "Actions", "Spawn NPC", "Makes an NPC or monster of a template next to the player or at a point (saved with the game).", kAct, false, {"next"}, {}};
        s.fields.push_back(req(fkind(EvField::Template, "template", "Template")));
        s.fields.push_back(fstr("name", "Unique name", "", "Optional: a name to find it again (letters, digits, _ and -)."));
        s.fields.push_back(fcombo("at", "Where", {"near_player", "point", "entity"}, "near_player", "near_player: x / y / z are an offset from the player."));
        s.fields.push_back(fnum("x", "X", 300));
        s.fields.push_back(fnum("y", "Y", 0));
        s.fields.push_back(fnum("z", "Z", 0));
        s.fields.push_back(when(req(fstr("entity", "Entity")), "at=entity"));
        s.fields.push_back(fbool("peaceful", "Peaceful", false));
        add(std::move(s));
    }
    add({"remove_npc", "Actions", "Remove NPC", "Removes one NPC by unique name, or every NPC of a template within the radius around the player.", kAct, false, {"next"},
         {req(fkind(EvField::Target, "target", "NPC")), fnum("radius", "Radius (m, templates)", 50, 1, 100000)}});
    add({"give_item", "Actions", "Give item", "Puts items into the player's inventory.", kAct, false, {"next"},
         {req(fkind(EvField::Item, "item", "Item")), fint("count", "Count", 1, 1, 100000), fbool("show", "Show in the item log", true)}});
    add({"take_item", "Actions", "Take item", "Takes items from the player's inventory (as many as he has, up to the count).", kAct, false, {"next"},
         {req(fkind(EvField::Item, "item", "Item")), fint("count", "Count", 1, 1, 100000)}});
    add({"give_xp", "Actions", "Give experience", "The player gets experience points (the game's own GiveXP).", kAct, false, {"next"}, {fint("amount", "Amount", 100, 1, 1000000)}});
    add({"quest_action", "Actions", "Quest", "Starts, succeeds, fails or cancels a quest.", kAct, false, {"next"},
         {req(fkind(EvField::Quest, "quest", "Quest")), fcombo("action", "Action", {"start", "succeed", "fail", "cancel"}, "start")}});
    add({"quest_log", "Actions", "Quest log entry", "Adds a line to a quest's log.", kAct, false, {"next"}, {req(fkind(EvField::Quest, "quest", "Quest")), req(fkind(EvField::Text, "text", "Text"))}});
    add({"hud_message", "Actions", "HUD message", "Prints a line into the game's on-screen log. {chapter}, {value}, {item}, {target} and {var:name} are replaced.", kAct, false, {"next"},
         {req(fkind(EvField::Text, "text", "Text")), fcombo("style", "Style", {"log", "note", "experience"}, "note")}});
    add({"play_sound", "Actions", "Play sound", "The player says a sound (a game sound name, or a sound file of the project: sounds/name.ogg).", kAct, false, {"next"}, {req(fstr("sound", "Sound"))}});
    {
        EvNodeSpec s{"teleport", "Actions", "Teleport player", "Moves the player at once (no collision test).", kAct, false, {"next"}, {}};
        for (auto& f : position_fields("To")) s.fields.push_back(f);
        add(std::move(s));
    }
    add({"set_chapter", "Actions", "Set chapter", "Changes the story chapter (quests and infos synchronise; the chapter card is not shown).", kAct, false, {"next"}, {fint("chapter", "Chapter", 2, 1, 20)}});
    add({"set_time", "Actions", "Set time of day", "Sets the in-game clock.", kAct, false, {"next"}, {fint("hour", "Hour", 12, 0, 23), fint("minute", "Minute", 0, 0, 59)}});
    add({"heal", "Actions", "Heal / hurt player", "Adds hit points and mana to the player (negative values take them away).", kAct, false, {"next"}, {fint("hp", "Hit points", 50), fint("mana", "Mana", 0)}});
    add({"run_script", "Actions", "Run script", "Calls a script by name: one defined with risen.script.define in custom.lua, or a game script (the player is self and other).", kAct, false, {"next"},
         {req(fstr("script", "Script")), fint("arg", "Argument", 0)}});
    add({"send_signal", "Actions", "Send signal", "Fires every On signal trigger of that name (in any graph) after this frame.", kAct, false, {"next"}, {req(fstr("name", "Signal"))}});
    add({"log", "Actions", "Write to log", "Writes a line to spass.log (for testing).", kAct, false, {"next"}, {fstr("text", "Text", "event")}});
    add({"comment", "Notes", "Comment", "A note on the canvas; not exported.", 0xff404040, false, {}, {fstr("text", "Text")}});
    return v;
}

std::vector<EvTestSpec> make_tests() {
    return {
        {"var", "Variable", {fcombo("scope", "Kind", {"game", "save"}, "game"), req(fstr("name", "Variable")), fcombo("op", "Is", kOps, "=="), fint("value", "Value", 1)}},
        {"quest", "Quest status", {req(fkind(EvField::Quest, "quest", "Quest")), fcombo("status", "Status", {"Open", "Running", "Success", "Failed", "Obsolete", "Cancelled", "Lost", "Won", "None"}, "Running")}},
        {"item", "Items carried", {req(fkind(EvField::Item, "item", "Item")), fint("count", "At least", 1, 1, 100000)}},
        {"chapter", "Chapter", {fcombo("op", "Is", kOps, "=="), fint("value", "Chapter", 1, 0, 20)}},
        {"level", "Player level", {fcombo("op", "Is", kOps, ">="), fint("value", "Level", 1, 0, 100)}},
        {"skill", "Skill", {req(fstr("skill", "Skill", "", "Strength, Sword, Smith, ... (risen.balance.skills())")), fcombo("op", "Is", kOps, ">="), fint("value", "Value", 1, 0, 1000)}},
        {"alive", "NPC is alive", {req(fstr("who", "NPC", "", "Unique name"))}},
        {"dead", "NPC is dead", {req(fstr("who", "NPC", "", "Unique name"))}},
        {"guild", "Player's guild", {fcombo("guild", "Guild", {"None", "Don", "Dig", "Grd", "Cit", "Inq", "Mag", "Pir"}, "Don")}},
        {"event", "Game event happened", {req(fstr("name", "Event", "", "A game variable that exists (SetGameEvent)"))}},
        {"time", "Time of day",
         {fint("from", "From hour", 8, 0, 23), fint("to", "Until hour (excluded)", 20, 0, 23, "from 22 to 6 spans midnight; the same hour twice = always")}},
        {"area",
         "Player is in an area",
         {fcombo("at", "Centre", {"point", "entity"}, "point"), when(fnum("x", "X", 0), "at=point"), when(fnum("y", "Y", 0), "at=point"), when(fnum("z", "Z", 0), "at=point"),
          when(req(fstr("entity", "Entity")), "at=entity"), fnum("radius", "Radius (m)", 5, 1, 100000)}},
        {"chance", "Random chance", {fint("percent", "Percent", 50, 1, 100)}},
    };
}

std::string str(const json& d, const char* k) { return d.contains(k) && d[k].is_string() ? d[k].get<std::string>() : std::string(); }

bool has_text(const json& t) {
    if (!t.is_object()) return false;
    for (auto& [k, v] : t.items())
        if (v.is_string() && !v.get<std::string>().empty()) return true;
    return false;
}

// "key=a|b" / "key!=a"
bool when_holds(const char* w, const json& data) {
    if (!w) return true;
    const std::string s = w;
    const bool neg = s.find("!=") != std::string::npos;
    const size_t eq = s.find(neg ? "!=" : "=");
    const std::string key = s.substr(0, eq), vals = s.substr(eq + (neg ? 2 : 1));
    const std::string cur = str(data, key.c_str());
    bool hit = false;
    size_t start = 0;
    while (start <= vals.size()) {
        size_t bar = vals.find('|', start);
        if (bar == std::string::npos) bar = vals.size();
        if (vals.substr(start, bar - start) == cur) hit = true;
        start = bar + 1;
    }
    return neg ? !hit : hit;
}

// validates the visible fields of one object against their definitions; `ref` collects the names the object points at
void check_fields(const std::vector<EvFieldDef>& fields, const json& d, const std::function<void(bool, const std::string&)>& problem, std::vector<std::pair<std::string, std::string>>* refs) {
    for (auto& f : fields) {
        if (!event_field_visible(f, d)) continue;
        const bool present = d.contains(f.key) && !d[f.key].is_null();
        const std::string label = f.label;
        switch (f.kind) {
            case EvField::Int:
            case EvField::Number: {
                const double v = present && d[f.key].is_number() ? d[f.key].get<double>() : (f.def.is_number() ? f.def.get<double>() : 0);
                if (!std::isfinite(v) || v < f.min || v > f.max) problem(true, label + ": " + std::to_string(v) + " is out of range");
                break;
            }
            case EvField::Bool:
                break;
            case EvField::Text:
                if (f.required && !has_text(present ? d[f.key] : json())) problem(true, label + " is empty");
                break;
            case EvField::Combo: {
                const std::string v = str(d, f.key);
                if (std::find(f.options.begin(), f.options.end(), v) == f.options.end()) problem(true, label + ": '" + v + "' is not one of the choices");
                break;
            }
            default: {
                const std::string v = str(d, f.key);
                if (v.empty()) {
                    if (f.required) problem(true, label + " is empty");
                } else if (refs) {
                    const char* kind = f.kind == EvField::Item ? "item" : f.kind == EvField::Quest ? "quest" : f.kind == EvField::Target ? "npc" : f.kind == EvField::Template ? "template" : nullptr;
                    if (kind) refs->push_back({kind, v});
                }
            }
        }
    }
}

bool is_var_name(const std::string& s) {
    if (s.empty() || s.size() > 63) return false;
    for (unsigned char c : s)
        if (!(std::isalnum(c) && c < 0x80) && c != '_' && c != '.' && c != '-') return false;
    return true;
}

}  // namespace

const std::vector<EvNodeSpec>& event_specs() {
    static const std::vector<EvNodeSpec> v = make_specs();
    return v;
}
const EvNodeSpec* event_spec(const std::string& type) {
    for (auto& s : event_specs())
        if (type == s.type) return &s;
    return nullptr;
}
const std::vector<EvTestSpec>& event_tests() {
    static const std::vector<EvTestSpec> v = make_tests();
    return v;
}
const EvTestSpec* event_test(const std::string& kind) {
    for (auto& s : event_tests())
        if (kind == s.kind) return &s;
    return nullptr;
}
const std::vector<std::string>& event_groups() {
    static const std::vector<std::string> v = {"Triggers", "Conditions", "Flow", "Actions", "Notes"};
    return v;
}

bool event_field_visible(const EvFieldDef& f, const json& data) { return when_holds(f.when, data); }

json event_default_test(const std::string& kind) {
    json t = json::object();
    t["kind"] = kind;
    if (const EvTestSpec* s = event_test(kind))
        for (auto& f : s->fields)
            if (!f.def.is_null()) t[f.key] = f.def;
    return t;
}

json event_default_data(const std::string& type) {
    json d = json::object();
    const EvNodeSpec* s = event_spec(type);
    if (!s) return d;
    for (auto& f : s->fields)
        if (!f.def.is_null()) d[f.key] = f.def;
    if (type == "condition") d["tests"] = json::array({event_default_test("var")});
    return d;
}

std::string event_node_id(const GraphNode& n) { return n.uid.substr(0, 10); }
std::string event_graph_key(const EventGraphDef& g) { return g.uid.substr(0, 10); }

std::string event_summary(const Project& p, const GraphNode& n) {
    const json& d = n.data;
    auto s = [&](const char* k) { return str(d, k); };
    auto num = [&](const char* k) {
        if (!d.contains(k) || !d[k].is_number()) return std::string("?");
        const double v = d[k].get<double>();
        return v == std::floor(v) ? std::to_string(static_cast<long long>(v)) : std::to_string(v).substr(0, std::to_string(v).find('.') + 3);
    };
    auto first_text = [&](const json& t) {
        if (!t.is_object()) return std::string();
        for (auto& l : p.mod.languages)
            if (t.contains(l) && t[l].is_string() && !t[l].get<std::string>().empty()) return t[l].get<std::string>();
        return std::string();
    };
    const std::string& t = n.type;
    auto pos = [&]() { return s("at") == "entity" ? "at " + s("entity") : "(" + num("x") + ", " + num("y") + ", " + num("z") + ")"; };
    if (t == "on_session") return s("mode") == "any" ? "new game or load" : s("mode") == "load" ? "a savegame is loaded" : "a new game starts";
    if (t == "on_chapter") return d.value("chapter", 0) == 0 ? "the chapter changes" : "chapter " + num("chapter") + " begins";
    if (t == "on_kill") return s("target") + " dies" + (d.value("by_player", false) ? " (by the player)" : "");
    if (t == "on_move") return "player moved " + num("min_m") + " m";
    if (t == "on_area") return "player " + s("on") + " " + (s("at") == "entity" ? s("entity") : pos()) + " r " + num("radius") + " m";
    if (t == "on_quest") return (s("quest").empty() ? std::string("any quest") : s("quest")) + " -> " + s("status");
    if (t == "on_item") return (s("item").empty() ? std::string("any item") : s("item")) + " " + s("what");
    if (t == "on_dialogue") return s("info") + " finished";
    if (t == "on_time") return "clock reaches " + num("hour") + ":" + (d.value("minute", 0) < 10 ? "0" : "") + num("minute");
    if (t == "on_variable") return s("name") + (s("op") == "changed" || s("op").empty() ? " changed" : " " + s("op") + " " + num("value"));
    if (t == "on_timer") return std::string(d.value("repeat", false) ? "every " : "after ") + num("seconds") + " s";
    if (t == "on_signal") return "signal " + s("name");
    if (t == "condition") {
        std::string out;
        if (d.contains("tests") && d["tests"].is_array()) {
            size_t i = 0;
            for (auto& c : d["tests"]) {
                const std::string k = str(c, "kind");
                std::string one = k;
                auto cs = [&](const char* key) { return str(c, key); };
                auto cn = [&](const char* key) { return c.contains(key) && c[key].is_number() ? std::to_string(c[key].get<long long>()) : std::string("?"); };
                if (k == "var") one = cs("name") + " " + cs("op") + " " + cn("value");
                else if (k == "quest") one = "quest " + cs("quest") + " is " + cs("status");
                else if (k == "item") one = "has " + cn("count") + " " + cs("item");
                else if (k == "chapter") one = "chapter " + cs("op") + " " + cn("value");
                else if (k == "level") one = "level " + cs("op") + " " + cn("value");
                else if (k == "skill") one = cs("skill") + " " + cs("op") + " " + cn("value");
                else if (k == "alive") one = cs("who") + " alive";
                else if (k == "dead") one = cs("who") + " dead";
                else if (k == "guild") one = "guild " + cs("guild");
                else if (k == "event") one = "event " + cs("name");
                else if (k == "time") one = "time " + cn("from") + "-" + cn("to") + " h";
                else if (k == "area") one = "in area r " + cn("radius") + " m";
                else if (k == "chance") one = cn("percent") + " % chance";
                out += (i++ ? (s("mode") == "any" ? "\nor " : "\nand ") : "") + one;
            }
        }
        return (d.value("negate", false) ? "not:\n" : "") + out;
    }
    if (t == "count") return (d.value("repeat", false) ? "every " : "the ") + num("n") + (d.value("repeat", false) ? "th pass" : "th pass only");
    if (t == "cooldown") return "at most every " + num("seconds") + " s";
    if (t == "delay") return "wait " + num("seconds") + " s";
    if (t == "repeat") return num("count") + " times";
    if (t == "set_variable") {
        const std::string op = s("op");
        return s("name") + (op == "clear" ? " cleared" : op == "add" ? " += " + num("value") : " = " + num("value")) + (s("scope") == "save" ? "  (save)" : "");
    }
    if (t == "spawn_npc") return s("template") + (s("at") == "near_player" ? " near the player" : " " + pos());
    if (t == "remove_npc") return s("target");
    if (t == "give_item" || t == "take_item") return num("count") + "x " + s("item");
    if (t == "give_xp") return num("amount") + " XP";
    if (t == "quest_action") return s("action") + " " + s("quest");
    if (t == "quest_log") return s("quest") + ": " + first_text(d.value("text", json::object()));
    if (t == "hud_message") return first_text(d.value("text", json::object()));
    if (t == "play_sound") return s("sound");
    if (t == "teleport") return pos();
    if (t == "set_chapter") return "chapter " + num("chapter");
    if (t == "set_time") return num("hour") + ":" + (d.value("minute", 0) < 10 ? "0" : "") + num("minute");
    if (t == "heal") return num("hp") + " HP, " + num("mana") + " mana";
    if (t == "run_script") return s("script");
    if (t == "send_signal") return "signal " + s("name");
    if (t == "log") return s("text");
    if (t == "comment") return s("text");
    return {};
}

namespace {

// objects with their keys in alphabetical order (a Lua table has no order: the data must read back as it was written)
json sorted(const json& j) {
    if (j.is_object()) {
        std::map<std::string, json> m;
        for (auto& [k, v] : j.items()) m[k] = sorted(v);
        json o = json::object();
        for (auto& [k, v] : m) o[k] = v;
        return o;
    }
    if (j.is_array()) {
        json a = json::array();
        for (auto& v : j) a.push_back(sorted(v));
        return a;
    }
    return j;
}

const GraphNode* target_of(const Graph& g, const GraphLink& l) { return g.find(l.to_node); }

}  // namespace

json compile_events(const Project& p, const EventGraphDef& eg, std::vector<DialogIssue>& issues) {
    const Graph& g = eg.graph;
    auto err = [&](const std::string& node, const std::string& m) { issues.push_back({true, node, m}); };
    auto warn = [&](const std::string& node, const std::string& m) { issues.push_back({false, node, m}); };
    json out = json::object();
    out["name"] = eg.name;
    out["key"] = event_graph_key(eg);
    out["enabled"] = eg.enabled;
    if (!p.mod.languages.empty()) out["fallback"] = p.mod.languages.front();
    json nodes = json::array();
    std::vector<const GraphNode*> triggers;
    std::map<std::string, const GraphNode*> by_uid;
    for (auto& n : g.nodes) {
        const EvNodeSpec* spec = event_spec(n.type);
        if (!spec) {
            err(n.uid, "unknown node type '" + n.type + "'");
            continue;
        }
        if (n.type == "comment") continue;
        by_uid[n.uid] = &n;
        if (spec->trigger) triggers.push_back(&n);
        json e = json::object();
        e["id"] = event_node_id(n);
        e["type"] = n.type;
        e["pos"] = json::array({n.x, n.y});
        for (auto& [k, v] : n.data.items()) e[k] = v;
        // a field of a condition's tests without a value takes the default (a hand-edited project)
        json outs = json::object();
        for (const char* pin : spec->outs) {
            std::vector<const GraphLink*> ls = links_from(g, n.uid, pin);
            std::stable_sort(ls.begin(), ls.end(), [&](const GraphLink* a, const GraphLink* b) {
                const GraphNode* x = target_of(g, *a);
                const GraphNode* y = target_of(g, *b);
                if (!x || !y) return false;
                return x->y != y->y ? x->y < y->y : x->x < y->x;
            });
            json ids = json::array();
            for (auto* l : ls)
                if (const GraphNode* t = target_of(g, *l); t && t->type != "comment") ids.push_back(event_node_id(*t));
            if (!ids.empty()) outs[pin] = ids;
        }
        e["outs"] = outs;
        nodes.push_back(sorted(e));
        // field checks
        auto problem = [&](bool error, const std::string& m) { issues.push_back({error, n.uid, std::string(spec->title) + ": " + m}); };
        check_fields(spec->fields, n.data, problem, nullptr);
        if (n.type == "on_variable" || n.type == "set_variable") {
            if (!str(n.data, "name").empty() && !is_var_name(str(n.data, "name"))) err(n.uid, "the variable name may only contain letters, digits, _, . and -");
        }
        if (n.type == "spawn_npc" && !str(n.data, "name").empty()) {
            const std::string nm = str(n.data, "name");
            for (unsigned char c : nm)
                if (!(std::isalnum(c) && c < 0x80) && c != '_' && c != '-') {
                    err(n.uid, "the NPC's unique name may only contain letters, digits, _ and -");
                    break;
                }
        }
        if (n.type == "condition") {
            const json tests = n.data.value("tests", json::array());
            if (!tests.is_array() || tests.empty()) err(n.uid, "Condition: add at least one check");
            else
                for (auto& c : tests) {
                    const EvTestSpec* ts = event_test(str(c, "kind"));
                    if (!ts) {
                        err(n.uid, "Condition: unknown check '" + str(c, "kind") + "'");
                        continue;
                    }
                    check_fields(ts->fields, c, [&](bool error, const std::string& m) { issues.push_back({error, n.uid, std::string("Condition (") + ts->title + "): " + m}); }, nullptr);
                }
        }
    }
    if (triggers.empty()) err("", "the graph has no trigger node (an 'On ...' node starts it)");
    // reachability from the triggers
    std::set<std::string> reached;
    std::vector<const GraphNode*> todo(triggers.begin(), triggers.end());
    while (!todo.empty()) {
        const GraphNode* n = todo.back();
        todo.pop_back();
        if (!reached.insert(n->uid).second) continue;
        for (auto& l : g.links)
            if (l.from_node == n->uid)
                if (const GraphNode* t = g.find(l.to_node); t && t->type != "comment") todo.push_back(t);
    }
    for (auto& [uid, n] : by_uid) {
        if (reached.count(uid)) continue;
        warn(uid, std::string(event_spec(n->type)->title) + " is not reached from any trigger: it never runs");
    }
    for (auto* t : triggers) {
        bool linked = false;
        for (auto& l : g.links) linked |= l.from_node == t->uid;
        if (!linked) warn(t->uid, std::string(event_spec(t->type)->title) + " has nothing linked to it: it does nothing");
    }
    // a loop must contain a Delay (otherwise one event would never finish)
    {
        std::map<std::string, int> state;  // 0 new, 1 on the stack, 2 done
        bool looped = false;
        std::string at;
        std::function<void(const GraphNode*)> dfs = [&](const GraphNode* n) {
            state[n->uid] = 1;
            for (auto& l : g.links) {
                if (l.from_node != n->uid) continue;
                const GraphNode* t = g.find(l.to_node);
                if (!t || t->type == "delay" || t->type == "comment") continue;
                if (state[t->uid] == 1) {
                    looped = true;
                    at = t->uid;
                } else if (state[t->uid] == 0) dfs(t);
            }
            state[n->uid] = 2;
        };
        for (auto& [uid, n] : by_uid)
            if (state[uid] == 0) dfs(n);
        if (looped) err(at, "the links make a loop without a Delay node: it would never end");
    }
    out["nodes"] = nodes;
    return out;
}

std::vector<EventRef> event_refs(const EventGraphDef& eg) {
    std::vector<EventRef> v;
    for (auto& n : eg.graph.nodes) {
        const EvNodeSpec* spec = event_spec(n.type);
        if (!spec) continue;
        std::vector<std::pair<std::string, std::string>> refs;
        check_fields(spec->fields, n.data, [](bool, const std::string&) {}, &refs);
        if (n.type == "condition" && n.data.contains("tests") && n.data["tests"].is_array())
            for (auto& c : n.data["tests"]) {
                if (const EvTestSpec* ts = event_test(str(c, "kind"))) check_fields(ts->fields, c, [](bool, const std::string&) {}, &refs);
                if (str(c, "kind") == "skill" && !str(c, "skill").empty()) refs.push_back({"skill", str(c, "skill")});
                if ((str(c, "kind") == "alive" || str(c, "kind") == "dead") && !str(c, "who").empty()) refs.push_back({"npc", str(c, "who")});
            }
        for (auto& [k, name] : refs) v.push_back({n.uid, k, name});
    }
    return v;
}

json events_data(const Project& p) {
    json list = json::array();
    for (auto& g : p.events) {
        std::vector<DialogIssue> issues;
        list.push_back(compile_events(p, g, issues));
    }
    return list;
}

EventGraphDef event_graph_from_data(const json& entry) {
    EventGraphDef g;
    const std::string key = str(entry, "key");
    const std::string tail = new_uid().substr(10);
    g.uid = (key.size() == 10 ? key : new_uid().substr(0, 10)) + tail;
    g.name = str(entry, "name");
    g.enabled = entry.value("enabled", true);
    std::map<std::string, std::string> uid_of;  // data id -> node uid
    if (entry.contains("nodes") && entry["nodes"].is_array())
        for (auto& e : entry["nodes"]) {
            GraphNode n;
            const std::string id = str(e, "id");
            n.uid = (id.size() == 10 ? id : new_uid().substr(0, 10)) + new_uid().substr(10);
            n.type = str(e, "type");
            if (e.contains("pos") && e["pos"].is_array() && e["pos"].size() == 2) n.x = e["pos"][0].get<double>(), n.y = e["pos"][1].get<double>();
            n.data = json::object();
            for (auto& [k, v] : e.items())
                if (k != "id" && k != "type" && k != "pos" && k != "outs") n.data[k] = v;
            uid_of[id] = n.uid;
            g.graph.nodes.push_back(std::move(n));
        }
    size_t i = 0;
    if (entry.contains("nodes") && entry["nodes"].is_array())
        for (auto& e : entry["nodes"]) {
            const GraphNode& n = g.graph.nodes[i++];
            if (!e.contains("outs") || !e["outs"].is_object()) continue;
            for (auto& [pin, ids] : e["outs"].items())
                for (auto& t : ids)
                    if (t.is_string() && uid_of.count(t.get<std::string>())) g.graph.links.push_back({new_uid(), n.uid, pin, uid_of[t.get<std::string>()], "in"});
        }
    return g;
}

std::vector<std::string> event_signal_names(const Project& p) {
    std::set<std::string> s;
    for (auto& g : p.events)
        for (auto& n : g.graph.nodes)
            if ((n.type == "on_signal" || n.type == "send_signal") && !str(n.data, "name").empty()) s.insert(str(n.data, "name"));
    return {s.begin(), s.end()};
}

bool movement_step(MovementState& s, double x, double y, double z, double min_units) {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) return false;
    if (!s.has_anchor) {
        s.has_anchor = true;
        s.ax = x, s.ay = y, s.az = z;
        return false;
    }
    const double dx = x - s.ax, dy = y - s.ay, dz = z - s.az;
    if (dx * dx + dy * dy + dz * dz < min_units * min_units) return false;
    s.ax = x, s.ay = y, s.az = z;
    return true;
}

}  // namespace mk
