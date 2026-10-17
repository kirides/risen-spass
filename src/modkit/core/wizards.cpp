#include "core/wizards.h"

#include <algorithm>
#include <cctype>
#include <set>

#include "core/dialog.h"
#include "core/graph.h"
#include "core/paths.h"
#include "core/validate.h"

namespace mk {

namespace {

std::string sanitize(const std::string& s) {
    std::string out;
    for (unsigned char c : s) out += (std::isalnum(c) && c < 0x80) || c == '_' ? static_cast<char>(c) : '_';
    return out;
}

json text_json(const Text& t) {
    json o = json::object();
    for (auto& [k, v] : t)
        if (!v.empty()) o[k] = v;
    return o;
}

// the first language that has a text (the mod's own order)
std::string first_text(const Project& p, const Text& t) {
    for (auto& l : p.mod.languages)
        if (auto it = t.find(l); it != t.end() && !it->second.empty()) return it->second;
    return t.empty() ? std::string() : t.begin()->second;
}

// `t`, or `fallback` written into the mod's first language when the form left it empty
Text or_default(const Project& p, Text t, const std::string& fallback) {
    if (first_text(p, t).empty()) t[p.mod.languages.empty() ? std::string("English") : p.mod.languages.front()] = fallback;
    return t;
}

bool template_taken(const Project& p, const GameData* g, const std::string& name) {
    const std::string l = p2m::lower(name);
    for (auto& i : p.items)
        if (i.mode == Mode::New && p2m::lower(i.name) == l) return true;
    for (auto& r : p.recipes)
        if (r.mode == Mode::New && p2m::lower(r.name) == l) return true;
    for (auto& n : p.npc_templates)
        if (n.mode == Mode::New && p2m::lower(n.name) == l) return true;
    for (auto& s : p.spells)
        if (s.mode == Mode::New && p2m::lower(s.name) == l) return true;
    return g && g->tpl(name);
}

std::string unique_template(const Project& p, const GameData* g, std::string want) {
    if (!template_taken(p, g, want)) return want;
    for (int i = 2;; ++i)
        if (!template_taken(p, g, want + "_" + std::to_string(i))) return want + "_" + std::to_string(i);
}

std::string prefix_of(const Project& p) {
    std::string s = p.mod.id.empty() ? std::string("Mod") : p.mod.id;
    s = sanitize(s);
    s[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(s[0])));
    return s;
}

bool info_taken(const Project& p, const std::string& name, const std::set<std::string>& extra) {
    if (extra.count(name)) return true;
    for (auto& d : p.dialogs)
        for (auto& n : d.graph.nodes)
            if (n.type == "option" && info_name(p, n) == name) return true;
    return false;
}

DialogDef* dialog_of(Project& p, const std::string& npc) {
    for (auto& d : p.dialogs)
        if (d.owner == npc) return &d;
    return nullptr;
}

// where new nodes go below what a graph holds
double next_free_y(const Graph& g) {
    double y = -260;
    for (auto& n : g.nodes) y = std::max(y, n.y);
    return y + 260;
}

GraphNode& add_node(Graph& g, GraphKind k, const char* type, double x, double y) {
    g.nodes.push_back(make_node(k, type, x, y));
    return g.nodes.back();
}

}  // namespace

std::string suggest_quest_id(const Project& p, const std::string& what) {
    const std::string base = "Q_" + prefix_of(p) + "_" + sanitize(what.empty() ? std::string("Job") : what);
    auto taken = [&](const std::string& n) { return p.quest(n) != nullptr; };
    if (!taken(base)) return base;
    for (int i = 2;; ++i)
        if (!taken(base + "_" + std::to_string(i))) return base + "_" + std::to_string(i);
}

std::string suggest_info_name(const Project& p, const std::string& base) {
    const std::string b = p.mod.id + "_" + sanitize(base);
    if (!info_taken(p, b + "_1", {})) return b + "_1";
    for (int i = 2;; ++i)
        if (!info_taken(p, b + "_" + std::to_string(i), {})) return b + "_" + std::to_string(i);
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------

WizardResult wizard_fetch_quest(Project& p, const FetchQuestParams& w) {
    WizardResult r;
    if (const std::string why = npc_name_problem(w.npc); !why.empty()) {
        r.error = "the NPC: " + why;
        return r;
    }
    if (w.quest.empty()) {
        r.error = "the quest needs an id";
        return r;
    }
    for (unsigned char c : w.quest)
        if (!(std::isalnum(c) && c < 0x80) && c != '_') {
            r.error = "the quest id may only contain letters, digits and _";
            return r;
        }
    if (p.quest(w.quest)) {
        r.error = "the project has a quest '" + w.quest + "' already";
        return r;
    }
    if (w.item.empty()) {
        r.error = "choose the item the player has to bring";
        return r;
    }
    if (w.count < 1 || w.count > 999) {
        r.error = "the number of items must be between 1 and 999";
        return r;
    }
    if (w.xp < 0 || w.gold < 0) {
        r.error = "experience and gold must not be negative";
        return r;
    }
    const std::string lang = p.mod.languages.empty() ? std::string("English") : p.mod.languages.front();
    const std::string ask_info = suggest_info_name(p, w.npc + "_" + w.quest + "_Ask");
    std::set<std::string> taken{ask_info};
    std::string done_info = suggest_info_name(p, w.npc + "_" + w.quest + "_Done");
    if (taken.count(done_info) || done_info == ask_info) done_info += "_b";

    // ---- the quest ----
    QuestDef q;
    q.uid = new_uid();
    q.name = w.quest;
    q.title = or_default(p, w.title, "Bring " + std::to_string(w.count) + " " + w.item);
    q.type = "HasItems";
    Graph& qg = q.graph;
    double x = 0;
    GraphNode& start = add_node(qg, GraphKind::Quest, "start", x, 0);
    start.data["trigger"] = "dialog";
    const std::string start_uid = start.uid;
    GraphNode& items = add_node(qg, GraphKind::Quest, "objective", x += 300, 0);
    items.data["kind"] = "items";
    items.data["item"] = w.item;
    items.data["count"] = w.count;
    items.data["text"] = text_json(or_default(p, w.log_text, "Bring " + std::to_string(w.count) + " " + w.item + "."));
    const std::string items_uid = items.uid;
    GraphNode& talk = add_node(qg, GraphKind::Quest, "objective", x += 300, 0);
    talk.data["kind"] = "talk";
    talk.data["info"] = done_info;
    const std::string talk_uid = talk.uid;
    GraphNode& reward = add_node(qg, GraphKind::Quest, "reward", x += 300, 0);
    reward.data["xp"] = w.xp;
    if (w.gold > 0) reward.data["gold"] = w.gold;
    json ritems = json::array();
    for (auto& [item, n] : w.reward_items)
        if (!item.empty() && n > 0) ritems.push_back(json::array({item, n}));
    reward.data["items"] = ritems;
    const std::string reward_uid = reward.uid;
    GraphNode& outcome = add_node(qg, GraphKind::Quest, "outcome", x += 300, 0);
    outcome.data["result"] = "success";
    if (!first_text(p, w.success_log).empty()) outcome.data["text"] = text_json(w.success_log);
    const std::string outcome_uid = outcome.uid;
    add_link(GraphKind::Quest, qg, start_uid, "next", items_uid, "in");
    add_link(GraphKind::Quest, qg, items_uid, "next", talk_uid, "in");
    add_link(GraphKind::Quest, qg, talk_uid, "next", reward_uid, "in");
    add_link(GraphKind::Quest, qg, reward_uid, "next", outcome_uid, "in");

    // ---- the dialogue: two options on the NPC ----
    DialogDef* dlg = dialog_of(p, w.npc);
    bool fresh = dlg == nullptr;
    DialogDef made;
    if (fresh) {
        made.uid = new_uid();
        made.name = sanitize(w.npc) + "_Jobs";
        for (int i = 2; std::any_of(p.dialogs.begin(), p.dialogs.end(), [&](const DialogDef& d) { return d.name == made.name; }); ++i) made.name = sanitize(w.npc) + "_Jobs_" + std::to_string(i);
        made.owner = w.npc;
        dlg = &made;
    }
    Graph& g = dlg->graph;
    const double y = next_free_y(g);
    // option 1: the job (once): line, start the quest
    GraphNode& ask = add_node(g, GraphKind::Dialog, "option", 0, y);
    ask.data["text"] = text_json(or_default(p, w.ask_option, "Do you have work for me?"));
    ask.data["type"] = "Info";
    ask.data["permanent"] = false;
    ask.data["info"] = ask_info;
    const std::string ask_uid = ask.uid;
    GraphNode& ask_line = add_node(g, GraphKind::Dialog, "line", 330, y);
    ask_line.data["speaker"] = "npc";
    ask_line.data["text"] = text_json(or_default(p, w.ask_line, "Bring me " + std::to_string(w.count) + " " + w.item + "."));
    const std::string ask_line_uid = ask_line.uid;
    GraphNode& qstart = add_node(g, GraphKind::Dialog, "quest", 660, y);
    qstart.data["quest"] = w.quest;
    qstart.data["action"] = "start";
    const std::string qstart_uid = qstart.uid;
    GraphNode& ask_end = add_node(g, GraphKind::Dialog, "end", 990, y);
    const std::string ask_end_uid = ask_end.uid;
    add_link(GraphKind::Dialog, g, ask_uid, "then", ask_line_uid, "in");
    add_link(GraphKind::Dialog, g, ask_line_uid, "next", qstart_uid, "in");
    add_link(GraphKind::Dialog, g, qstart_uid, "next", ask_end_uid, "in");
    // option 2: the turn-in (while the quest runs and the player carries the items)
    const double y2 = y + 260;
    GraphNode& done = add_node(g, GraphKind::Dialog, "option", 0, y2);
    done.data["text"] = text_json(or_default(p, w.done_option, "Here is what you wanted."));
    done.data["type"] = "Info";
    done.data["permanent"] = false;
    done.data["info"] = done_info;
    const std::string done_uid = done.uid;
    GraphNode& c1 = add_node(g, GraphKind::Dialog, "cond", -330, y2 - 40);
    c1.data["kind"] = "quest";
    c1.data["args"] = json::object({{"quest", w.quest}, {"status", "Running"}});
    const std::string c1_uid = c1.uid;
    GraphNode& c2 = add_node(g, GraphKind::Dialog, "cond", -330, y2 + 60);
    c2.data["kind"] = "item";
    c2.data["args"] = json::object({{"item", w.item}, {"count", w.count}});
    const std::string c2_uid = c2.uid;
    add_link(GraphKind::Dialog, g, c1_uid, "out", done_uid, "when");
    add_link(GraphKind::Dialog, g, c2_uid, "out", done_uid, "when");
    std::string prev = done_uid, prev_pin = "then";
    double dx = 330;
    if (w.take_items) {
        GraphNode& give = add_node(g, GraphKind::Dialog, "command", dx, y2);
        give.data["command"] = "Give";
        give.data["params"] = json::object({{"Entity1", "player"}, {"Entity2", "npc"}, {"Item", w.item}, {"Amount", w.count}});
        const std::string uid = give.uid;
        add_link(GraphKind::Dialog, g, prev, prev_pin, uid, "in");
        prev = uid;
        prev_pin = "next";
        dx += 330;
    }
    GraphNode& thanks = add_node(g, GraphKind::Dialog, "line", dx, y2);
    thanks.data["speaker"] = "npc";
    thanks.data["text"] = text_json(or_default(p, w.done_line, "Thank you!"));
    const std::string thanks_uid = thanks.uid;
    add_link(GraphKind::Dialog, g, prev, prev_pin, thanks_uid, "in");
    GraphNode& done_end = add_node(g, GraphKind::Dialog, "end", dx + 330, y2);
    const std::string done_end_uid = done_end.uid;
    add_link(GraphKind::Dialog, g, thanks_uid, "next", done_end_uid, "in");

    if (fresh) p.dialogs.push_back(made);
    p.quests.push_back(q);
    r.created.push_back({"Quests", q.uid});
    r.created.push_back({"Dialogues", fresh ? made.uid : dlg->uid});
    if (!p.npc(w.npc)) r.notes.push_back("'" + w.npc + "' is not a placed NPC of the project: it has to be a unique NPC name of the game world (the dialogue owner).");
    (void)lang;
    return r;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------

WizardResult wizard_weapon(Project& p, const WeaponParams& w) {
    WizardResult r;
    if (const std::string why = template_name_problem(w.name); !why.empty()) {
        r.error = "the item name: " + why;
        return r;
    }
    if (w.base.empty()) {
        r.error = "choose the shipped item the weapon is made from";
        return r;
    }
    if (template_taken(p, nullptr, w.name)) {
        r.error = "the project has a template '" + w.name + "' already";
        return r;
    }
    if (w.damage && *w.damage < 0) {
        r.error = "the damage must not be negative";
        return r;
    }
    if (w.value && *w.value < 0) {
        r.error = "the value must not be negative";
        return r;
    }
    if (w.recipe) {
        if (w.station.empty()) {
            r.error = "choose the station the recipe is crafted at";
            return r;
        }
        if (w.ingredients.empty()) {
            r.error = "the recipe needs at least one ingredient";
            return r;
        }
        for (auto& i : w.ingredients)
            if (i.item.empty() || i.count < 1) {
                r.error = "every ingredient needs an item and a count of at least 1";
                return r;
            }
    }
    ItemDef it;
    it.uid = new_uid();
    it.mode = Mode::New;
    it.name = w.name;
    it.base = w.base;
    it.label = or_default(p, w.label, w.name);
    it.description = w.description;
    it.damage = w.damage;
    it.value = w.value;
    if (!w.requirements.empty()) it.requirements = w.requirements;
    p.items.push_back(it);
    r.created.push_back({"Items", it.uid});
    if (w.recipe) {
        RecipeDef rc;
        rc.uid = new_uid();
        rc.mode = Mode::New;
        rc.name = unique_template(p, nullptr, prefix_of(p) + "_Recipe_" + sanitize(w.name));
        rc.station = w.station;
        rc.ingredients = w.ingredients;
        if (!w.skills.empty()) rc.skills = w.skills;
        rc.result = w.name;
        rc.learn = w.learn;
        p.recipes.push_back(rc);
        r.created.push_back({"Recipes", rc.uid});
        if (!w.learn) r.notes.push_back("The recipe is an item the player has to find or buy: place it in a chest or give it to a trader.");
    }
    return r;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------

WizardResult wizard_trader(Project& p, const TraderParams& w) {
    WizardResult r;
    if (const std::string why = npc_name_problem(w.npc); !why.empty()) {
        r.error = "the NPC: " + why;
        return r;
    }
    if (p.npc(w.npc)) {
        r.error = "the project has an NPC '" + w.npc + "' already";
        return r;
    }
    if (w.template_name.empty()) {
        r.error = "choose the NPC template he is made of";
        return r;
    }
    if (w.stock.empty()) {
        r.error = "a trader needs something to sell";
        return r;
    }
    for (auto& s : w.stock)
        if (s.item.empty() || s.amount < 1) {
            r.error = "every stock row needs an item and an amount of at least 1";
            return r;
        }
    // a template of his own with trading switched on (gCDialog_PS.TradeEnabled), so the shipped one stays as it is
    NpcTemplateDef t;
    t.uid = new_uid();
    t.mode = Mode::New;
    t.name = unique_template(p, nullptr, prefix_of(p) + "_" + sanitize(w.npc) + "_T");
    t.base = w.template_name;
    t.label = or_default(p, w.label, w.npc);
    t.properties["gCDialog_PS"]["TradeEnabled"] = true;
    p.npc_templates.push_back(t);

    PlacedNpc n;
    n.uid = new_uid();
    n.name = w.npc;
    n.template_name = t.name;
    n.label = t.label;
    n.peaceful = true;
    n.items = w.stock;
    if (w.near_player) {
        n.offset = true;
        n.position = {150, 0, 0};
    } else {
        n.position = w.position;
    }
    p.npcs.push_back(n);

    DialogDef d;
    d.uid = new_uid();
    d.name = sanitize(w.npc) + "_Trade";
    for (int i = 2; std::any_of(p.dialogs.begin(), p.dialogs.end(), [&](const DialogDef& x) { return x.name == d.name; }); ++i) d.name = sanitize(w.npc) + "_Trade_" + std::to_string(i);
    d.owner = w.npc;
    Graph& g = d.graph;
    GraphNode& opt = add_node(g, GraphKind::Dialog, "option", 0, 0);
    opt.data["text"] = text_json(or_default(p, w.option_text, "Let's trade."));
    opt.data["type"] = "Info";
    opt.data["permanent"] = true;
    opt.data["info"] = suggest_info_name(p, w.npc + "_Trade");
    const std::string opt_uid = opt.uid;
    GraphNode& line = add_node(g, GraphKind::Dialog, "line", 330, 0);
    line.data["speaker"] = "npc";
    line.data["text"] = text_json(or_default(p, w.line_text, "Take a look."));
    const std::string line_uid = line.uid;
    GraphNode& trade = add_node(g, GraphKind::Dialog, "command", 660, 0);
    trade.data["command"] = "DoTrade";
    trade.data["params"] = json::object({{"Entity1", "player"}, {"Entity2", "npc"}});
    const std::string trade_uid = trade.uid;
    add_link(GraphKind::Dialog, g, opt_uid, "then", line_uid, "in");
    add_link(GraphKind::Dialog, g, line_uid, "next", trade_uid, "in");
    p.dialogs.push_back(d);

    r.created.push_back({"NPCs", n.uid});
    r.created.push_back({"Dialogues", d.uid});
    r.notes.push_back("The DoTrade command was not tried in a real trade yet (docs/MODDING_GUIDE.md): test the trader in the game (Play).");
    if (w.near_player) r.notes.push_back("He is made in front of the player when the game first makes him; give him a fixed place in the World view when the mod is done.");
    return r;
}

}  // namespace mk
