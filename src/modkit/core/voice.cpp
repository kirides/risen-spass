#include "core/voice.h"
#include "core/i18n.h"

#include <algorithm>
#include <set>
#include <unordered_map>

#include "common/sound_file.h"
#include "core/assets.h"
#include "core/dialog.h"
#include "core/gamedata.h"
#include "core/graph.h"
#include "core/paths.h"
#include "pack2mod/util.h"

namespace mk {

namespace {

Text text_from_json(const json& j) {
    Text t;
    if (!j.is_object()) return t;
    for (auto& [k, v] : j.items())
        if (v.is_string() && !v.get<std::string>().empty()) t[k] = v.get<std::string>();
    return t;
}

std::string shorten(const std::string& s, size_t n = 40) { return s.size() > n ? s.substr(0, n) + "..." : s; }

std::string lower(const std::string& s) { return p2m::lower(s); }

void finish_search(StringLine& l) {
    std::string s = l.id + "\n" + l.speaker + "\n" + l.voice + "\n" + l.object + "\n" + l.quest + "\n" + l.kind + "\n" + l.string_id + "\n";
    for (auto& [k, v] : l.text) s += v + "\n";
    for (auto& [k, v] : l.stage) s += v + "\n";
    l.search = lower(s);
}

// the NPC voice of an entity / template (gCNPC_PS.Voice of its template), "" when unknown
std::string voice_of(const Project& p, const GameData* g, const std::string& speaker) {
    if (speaker == "PC_Hero") return "Player";
    if (!g) return {};
    std::string tpl = speaker;
    if (const PlacedNpc* n = p.npc(speaker)) tpl = n->template_name;
    for (auto& t : p.npc_templates)
        if (t.name == tpl && t.mode == Mode::New && !t.base.empty()) tpl = t.base;
    if (const TemplateInfo* t = g->tpl(tpl))
        if (const p2m::Val* v = t->get("gCNPC_PS.Voice"); v && v->is_str()) return v->s;
    return {};
}

const char* kKnownLanguages[] = {"English", "German", "French", "Russian", "Polish", "Italian", "Spanish", "Czech"};

// the language a file name suffix names ("English", "en", "de"), spelled like the project's language when it is one; "" when it is no language
std::string language_from_suffix(const std::string& suffix, const std::vector<std::string>& languages) {
    if (suffix.empty()) return {};
    for (auto& l : languages)
        if (lower(l) == lower(suffix)) return l;
    const std::string n = spass::sound::normalizeLanguage(suffix);
    for (const char* k : kKnownLanguages)
        if (n == k) {
            for (auto& l : languages)
                if (lower(l) == lower(n)) return l;
            return n;
        }
    return {};
}

std::string ext_lower(const std::string& file) {
    const std::string n = lower(p2m::bare_name(file));
    const auto dot = n.rfind('.');
    return dot == std::string::npos ? std::string() : n.substr(dot + 1);
}

std::string stem_of(const std::string& file) {
    std::string n = p2m::bare_name(file);
    const auto dot = n.rfind('.');
    if (dot != std::string::npos) n.erase(dot);
    return n;
}

GraphNode* find_node(Project& p, const std::string& uid) {
    for (auto& d : p.dialogs)
        if (GraphNode* n = d.graph.find(uid)) return n;
    for (auto& q : p.quests)
        if (GraphNode* n = q.graph.find(uid)) return n;
    for (auto& e : p.events)
        if (GraphNode* n = e.graph.find(uid)) return n;
    return nullptr;
}

bool set_text(Text& t, const std::string& lang, const std::string& v) {
    if (v.empty()) return t.erase(lang) > 0;
    auto it = t.find(lang);
    if (it != t.end() && it->second == v) return false;
    t[lang] = v;
    return true;
}

bool set_json_text(json& data, const char* key, const std::string& lang, const std::string& v) {
    json t = data.value(key, json::object());
    if (!t.is_object()) t = json::object();
    if (v.empty()) {
        if (!t.contains(lang)) return false;
        t.erase(lang);
    } else {
        if (t.contains(lang) && t[lang] == v) return false;
        t[lang] = v;
    }
    if (t.empty()) data.erase(key);
    else data[key] = t;
    return true;
}

std::string csv_cell(const std::string& s) {
    if (s.find_first_of(",\"\r\n") == std::string::npos && (s.empty() || (s.front() != ' ' && s.back() != ' '))) return s;
    std::string o = "\"";
    for (char c : s) {
        if (c == '"') o += '"';
        o += c;
    }
    return o + "\"";
}

}  // namespace

std::string text_of(const Text& t, const std::string& language, const std::vector<std::string>& fallback) {
    if (auto it = t.find(language); it != t.end() && !it->second.empty()) return it->second;
    if (auto it = t.find("default"); it != t.end() && !it->second.empty()) return it->second;
    for (auto& f : fallback)
        if (auto it = t.find(f); it != t.end() && !it->second.empty()) return it->second;
    for (auto& [k, v] : t)
        if (!v.empty()) return v;
    return {};
}

std::string voice_line_id(const Project& p, const GraphNode& n) { return p.mod.id + "_" + n.uid.substr(0, 8); }

std::vector<StringLine> project_lines(const Project& p, const GameData* game) {
    std::vector<StringLine> out;
    auto add_node_line = [&](const GraphNode& n, const std::string& kind, const std::string& object, const std::string& object_uid) -> StringLine& {
        StringLine l;
        l.id = voice_line_id(p, n);
        l.kind = kind;
        l.field = n.type;
        l.object = object;
        l.object_uid = object_uid;
        l.node_uid = n.uid;
        l.text = text_from_json(n.data.value("text", json::object()));
        l.stage = text_from_json(n.data.value("stage", json::object()));
        if (auto it = n.data.find("sound"); it != n.data.end() && it->is_object())
            for (auto& [k, v] : it->items())
                if (v.is_string() && !v.get<std::string>().empty()) l.audio[k] = v.get<std::string>();
        out.push_back(std::move(l));
        return out.back();
    };

    // ---- dialogues: every option, then its sequence; the keys the layer gives the lines (<info>_<n>)
    for (auto& d : p.dialogs) {
        const Graph& g = d.graph;
        std::vector<const GraphNode*> options;
        for (auto& n : g.nodes)
            if (n.type == "option") options.push_back(&n);
        std::stable_sort(options.begin(), options.end(), [](const GraphNode* a, const GraphNode* b) { return a->y < b->y; });
        std::set<std::string> listed;
        const std::string owner_voice = voice_of(p, game, d.owner);
        auto speaker_of = [&](const std::string& s) { return s == "player" ? std::string("PC_Hero") : s.empty() || s == "npc" ? d.owner : s; };
        for (const GraphNode* o : options) {
            const std::string info = info_name(p, *o);
            const std::string opt_text = text_of(text_from_json(o->data.value("text", json::object())), "", p.mod.languages);
            const std::string ctx = "Dialogue " + d.name + " > option '" + shorten(opt_text) + "'";
            auto par = links_into(g, o->uid, "parent");
            const GraphNode* parent = par.empty() ? nullptr : g.find(par.front()->from_node);
            if (listed.insert(o->uid).second) {
                StringLine& l = add_node_line(*o, "dialogue", d.name, d.uid);
                l.spoken = true;
                l.speaker = "PC_Hero";
                l.voice = "Player";
                l.listener = d.owner;
                l.order = 1;
                l.runtime_key = info + "_1";
                l.context = ctx;
                if (parent) l.parent = voice_line_id(p, *parent);
            }
            const std::string option_id = voice_line_id(p, *o);
            auto out_links = links_from(g, o->uid, "then");
            const GraphNode* cur = out_links.empty() ? nullptr : g.find(out_links.front()->to_node);
            std::set<std::string> seen;
            int n = 1;
            while (cur && seen.insert(cur->uid).second) {
                ++n;
                const json& dd = cur->data;
                const bool is_line = cur->type == "line";
                const bool is_saysound = cur->type == "command" && dd.value("command", std::string()) == "SaySound";
                const bool has_text = dd.contains("text") && dd["text"].is_object() && !dd["text"].empty();
                if ((is_line || is_saysound || has_text) && listed.insert(cur->uid).second) {
                    StringLine& l = add_node_line(*cur, "dialogue", d.name, d.uid);
                    l.order = n;
                    l.context = ctx;
                    l.parent = option_id;
                    if (is_line) {
                        const std::string sp = dd.value("speaker", std::string("npc"));
                        l.spoken = true;
                        l.speaker = speaker_of(sp);
                        l.listener = sp == "player" ? d.owner : std::string("PC_Hero");
                        l.voice = l.speaker == d.owner ? owner_voice : voice_of(p, game, l.speaker);
                        l.runtime_key = info + "_" + std::to_string(n);
                    } else if (is_saysound) {
                        const json ps = dd.value("params", json::object());
                        std::string sp = ps.is_object() ? ps.value("Speaker", std::string()) : std::string();
                        l.spoken = true;
                        l.field = "command";
                        l.speaker = speaker_of(sp);
                        l.voice = l.speaker == d.owner ? owner_voice : voice_of(p, game, l.speaker);
                        l.runtime_key = info + "_" + std::to_string(n);
                    }
                    if (cur->type == "log") l.quest = dd.value("quest", std::string());
                }
                if (cur->type == "end" || cur->type == "restart" || cur->type == "menu" ||(cur->type == "command" && dd.value("command", std::string()) == "End")) break;
                auto nx = links_from(g, cur->uid, "next");
                cur = nx.empty() ? nullptr : g.find(nx.front()->to_node);
            }
        }
        // lines that no option reaches still have texts (and can get recordings)
        for (auto& n : g.nodes) {
            if (listed.count(n.uid)) continue;
            const bool has_text = n.data.contains("text") && n.data["text"].is_object() && !n.data["text"].empty();
            if (n.type != "line" && !has_text) continue;
            StringLine& l = add_node_line(n, "dialogue", d.name, d.uid);
            l.context = "Dialogue " + d.name + " (not reached by an option)";
            if (n.type == "line") {
                l.spoken = true;
                l.speaker = speaker_of(n.data.value("speaker", std::string("npc")));
                l.voice = voice_of(p, game, l.speaker);
            }
        }
    }
    // ---- quests and event graphs: their nodes' texts (log entries, HUD messages)
    for (auto& q : p.quests) {
        if (!q.title.empty()) {
            StringLine l;
            l.id = "quest." + q.name + ".title";
            l.kind = "quest";
            l.field = "title";
            l.object = q.name;
            l.object_uid = q.uid;
            l.quest = q.name;
            l.text = q.title;
            l.context = "Quest " + q.name + ": title";
            out.push_back(std::move(l));
        }
        for (auto& n : q.graph.nodes) {
            if (!n.data.contains("text") || !n.data["text"].is_object()) continue;
            StringLine& l = add_node_line(n, "quest", q.name, q.uid);
            l.quest = q.name;
            l.context = "Quest " + q.name + " > " + n.type;
        }
    }
    for (auto& e : p.events)
        for (auto& n : e.graph.nodes) {
            if (!n.data.contains("text") || !n.data["text"].is_object()) continue;
            StringLine& l = add_node_line(n, "event", e.name, e.uid);
            l.context = "Event graph " + e.name + " > " + n.type;
            if (n.data.contains("quest") && n.data["quest"].is_string()) l.quest = n.data["quest"].get<std::string>();
        }
    // ---- objects with names and descriptions
    auto add_obj = [&](const char* kind, const std::string& name, const std::string& uid, const char* field, const Text& t, const std::string& sid) {
        if (t.empty()) return;
        StringLine l;
        l.id = std::string(kind) + "." + name + "." + field;
        l.kind = kind;
        l.field = field;
        l.object = name;
        l.object_uid = uid;
        l.text = t;
        l.string_id = sid;
        l.context = std::string(kind) + " " + name + ": " + field;
        out.push_back(std::move(l));
    };
    for (auto& i : p.items) {
        add_obj("item", i.name, i.uid, "label", i.label, "FO_" + i.name);
        add_obj("item", i.name, i.uid, "description", i.description, "ITEMDESC_" + i.name);
    }
    for (auto& r : p.recipes) {
        add_obj("recipe", r.name, r.uid, "label", r.label, "FO_" + r.name);
        add_obj("recipe", r.name, r.uid, "description", r.description, "ITEMDESC_" + r.name);
    }
    for (auto& s : p.spells) {
        add_obj("spell", s.name, s.uid, "label", s.label, "");
        add_obj("spell", s.name, s.uid, "description", s.description, "");
    }
    for (auto& n : p.npc_templates) add_obj("npc_template", n.name, n.uid, "label", n.label, "FO_" + n.name);
    for (auto& n : p.npcs) add_obj("npc", n.name, n.uid, "label", n.label, "FO_" + n.name);
    for (auto& l : p.layers) add_obj("layer", l.name, l.uid, "label", l.label, "");
    for (auto& s : p.strings) {
        StringLine l;
        l.id = s.id;
        l.kind = "string";
        l.field = "text";
        l.object = s.id;
        l.object_uid = s.uid;
        l.text = s.text;
        l.string_id = s.id;
        l.context = "String row " + s.id;
        out.push_back(std::move(l));
    }
    for (auto& l : out) finish_search(l);
    return out;
}

std::vector<StringLine> game_lines(const GameData& g) {
    std::vector<StringLine> out;
    out.reserve(g.string_rows().size());
    const auto& langs = g.languages();
    for (const GameStringRow& r : g.string_rows()) {
        StringLine l;
        l.id = r.id;
        l.game = true;
        l.kind = r.table;
        l.string_id = r.id;
        for (auto& lang : langs) {
            std::string t = g.text_exact(r.id, lang);
            if (!t.empty()) l.text[lang] = std::move(t);
            if (const std::string* sd = r.col(lang + "_StageDir")) l.stage[lang] = *sd;
            if (const std::string* wf = r.col(lang + "_WaveFile")) {
                l.audio[lang] = *wf;
                std::string stem = *wf;
                if (const auto dot = stem.rfind('.'); dot != std::string::npos) stem.erase(dot);
                l.shipped[lang] = g.exists(stem + "._xsnd");
                l.spoken = true;
                if (l.speaker.empty())
                    if (const auto at = stem.find("_%_"); at != std::string::npos) l.speaker = stem.substr(at + 3);
            }
        }
        if (const std::string* v = r.col("Voice")) l.voice = *v;
        if (l.speaker.empty() && l.spoken) l.speaker = l.voice;
        if (const std::string* v = r.col("Info")) l.object = *v;
        if (const std::string* v = r.col("Quest")) l.quest = *v;
        if (const std::string* v = r.col("Parent")) l.parent = *v;
        if (const std::string* v = r.col("Order")) l.order = std::atoi(v->c_str());
        if (const std::string* v = r.col("Owner")) l.listener = l.speaker == *v ? std::string("PC_Hero") : *v;
        l.context = r.table;
        if (!l.object.empty()) l.context += ": info " + l.object + " #" + std::to_string(l.order);
        if (!l.quest.empty()) l.context += ", quest " + l.quest;
        if (const std::string* role = r.col("Role")) l.context += " (" + *role + ")";
        if (const std::string* grp = r.col("Group")) l.context += " (" + *grp + ")";
        finish_search(l);
        out.push_back(std::move(l));
    }
    return out;
}

void mark_overridden(std::vector<StringLine>& game, const std::vector<StringLine>& mod) {
    std::set<std::string> ids;
    for (auto& m : mod)
        if (!m.string_id.empty()) ids.insert(lower(m.string_id));
    for (auto& g : game) g.overridden = ids.count(lower(g.string_id)) != 0;
}

AudioStatus audio_status(const Project& p, const StringLine& l, const std::string& language) {
    if (!l.spoken) return AudioStatus::None;
    if (l.game) {
        auto it = l.shipped.find(language);
        if (it == l.shipped.end()) return AudioStatus::None;  // the game has no voice in this language
        return it->second ? AudioStatus::Recorded : AudioStatus::Missing;
    }
    for (const std::string& k : {language, std::string("default")}) {
        auto it = l.audio.find(k);
        if (it == l.audio.end() || it->second.empty()) continue;
        if (is_compiled_sound(it->second) || find_asset(p, it->second)) return AudioStatus::Recorded;
    }
    return AudioStatus::Missing;
}

const char* audio_status_name(AudioStatus s) {
    switch (s) {
        case AudioStatus::Recorded: return "recorded";
        case AudioStatus::Missing: return "missing";
        default: return "";
    }
}

std::string expected_audio_file(const StringLine& l, const std::string& language) {
    if (!l.spoken) return {};
    if (l.game) {
        auto it = l.audio.find(language);
        return it == l.audio.end() ? std::string() : it->second;
    }
    return voice_asset_name(l.id, language, "ogg");
}

std::string runtime_sound(const Project& p, const StringLine& l, const std::string& language) {
    if (!l.spoken) return {};
    if (l.game) return expected_audio_file(l, language);
    if (l.runtime_key.empty()) return {};
    // the variant the layer registers: the language's own, else the default one the node has
    std::string variant = language;
    if (!l.audio.count(language) && l.audio.count("default")) variant = "default";
    if (variant != "default") variant = spass::sound::normalizeLanguage(variant);
    return spass::sound::voiceBaseName(p.mod.id, l.runtime_key, variant) + ".wav";
}

bool set_line_text(Project& p, const StringLine& l, const std::string& language, const std::string& text) {
    if (l.game) return false;
    if (!l.node_uid.empty()) {
        GraphNode* n = find_node(p, l.node_uid);
        return n && set_json_text(n->data, "text", language, text);
    }
    auto by_uid = [&](auto& v) -> decltype(&v[0]) {
        for (auto& x : v)
            if (x.uid == l.object_uid) return &x;
        return nullptr;
    };
    if (l.kind == "item")
        if (auto* x = by_uid(p.items)) return set_text(l.field == "label" ? x->label : x->description, language, text);
    if (l.kind == "recipe")
        if (auto* x = by_uid(p.recipes)) return set_text(l.field == "label" ? x->label : x->description, language, text);
    if (l.kind == "spell")
        if (auto* x = by_uid(p.spells)) return set_text(l.field == "label" ? x->label : x->description, language, text);
    if (l.kind == "npc_template")
        if (auto* x = by_uid(p.npc_templates)) return set_text(x->label, language, text);
    if (l.kind == "npc")
        if (auto* x = by_uid(p.npcs)) return set_text(x->label, language, text);
    if (l.kind == "layer")
        if (auto* x = by_uid(p.layers)) return set_text(x->label, language, text);
    if (l.kind == "quest" && l.field == "title")
        if (auto* x = by_uid(p.quests)) return set_text(x->title, language, text);
    if (l.kind == "string")
        if (auto* x = by_uid(p.strings)) return set_text(x->text, language, text);
    return false;
}

bool set_line_stage(Project& p, const StringLine& l, const std::string& language, const std::string& text) {
    if (l.game || l.node_uid.empty()) return false;
    GraphNode* n = find_node(p, l.node_uid);
    return n && set_json_text(n->data, "stage", language, text);
}

std::string override_game_line(Project& p, const StringLine& game_row, const std::vector<std::string>& languages) {
    for (auto& s : p.strings)
        if (lower(s.id) == lower(game_row.id)) return {};
    StringRow r;
    r.uid = new_uid();
    r.id = game_row.id;
    for (auto& lang : languages)
        if (auto it = game_row.text.find(lang); it != game_row.text.end() && !it->second.empty()) r.text[lang] = it->second;
    if (r.text.empty() && !game_row.text.empty() && !languages.empty()) r.text[languages.front()] = text_of(game_row.text, "English");
    r.notes = "overrides the game's text (" + game_row.kind + ")";
    p.strings.push_back(r);
    return r.uid;
}

// ---- export ---------------------------------------------------------------------------------------------------------------------------------------

std::vector<VoiceRow> voice_rows(const Project& p, const std::vector<const StringLine*>& lines, const VoiceExportOptions& o) {
    const std::vector<std::string>& langs = o.languages.empty() ? p.mod.languages : o.languages;
    std::vector<VoiceRow> rows;
    for (const StringLine* l : lines) {
        if (!l || (o.spoken_only && !l->spoken)) continue;
        for (const std::string& lang : langs) {
            const AudioStatus st = audio_status(p, *l, lang);
            if (o.missing_only && st != AudioStatus::Missing) continue;
            if (l->game && l->spoken && st == AudioStatus::None && o.spoken_only) continue;  // the game has no voice in this language
            VoiceRow r;
            r.line = l;
            r.language = lang;
            r.status = audio_status_name(st);
            r.expected_file = expected_audio_file(*l, lang);
            r.runtime_sound = runtime_sound(p, *l, lang);
            if (!l->game) {
                if (auto it = l->audio.find(lang); it != l->audio.end()) r.current_file = it->second;
                else if (auto d = l->audio.find("default"); d != l->audio.end()) r.current_file = d->second;
            } else if (st == AudioStatus::Recorded) {
                r.current_file = r.expected_file;
            }
            rows.push_back(std::move(r));
        }
    }
    return rows;
}

const std::vector<std::string>& voice_csv_columns() {
    static const std::vector<std::string> c = {"line_id", "language", "status", "speaker", "voice", "listener", "text", "stage_direction", "context",
                                               "source", "kind", "object", "quest", "parent_line", "order", "expected_file", "runtime_sound",
                                               "current_file", "file", "notes"};
    return c;
}

namespace {
std::vector<std::string> row_cells(const VoiceRow& r) {
    const StringLine& l = *r.line;
    const std::string text = text_of(l.text, r.language), stage = text_of(l.stage, r.language);
    return {l.id, r.language, r.status, l.speaker, l.voice, l.listener, text, stage, l.context, l.game ? "game" : "mod", l.kind + (l.field.empty() ? "" : "/" + l.field),
            l.object, l.quest, l.parent, l.order ? std::to_string(l.order) : std::string(), r.expected_file, r.runtime_sound, r.current_file, "", ""};
}
}  // namespace

std::string voice_csv(const std::vector<VoiceRow>& rows) {
    std::string out = "\xEF\xBB\xBF";
    auto line = [&](const std::vector<std::string>& cells) {
        for (size_t i = 0; i < cells.size(); ++i) out += (i ? "," : "") + csv_cell(cells[i]);
        out += "\r\n";
    };
    line(voice_csv_columns());
    for (auto& r : rows) line(row_cells(r));
    return out;
}

json voice_json(const Project& p, const std::vector<VoiceRow>& rows) {
    json j = json::object();
    j["mod"] = p.mod.id;
    j["name"] = p.mod.name;
    j["generated_by"] = std::string("risen-modkit ") + MK_VERSION;
    j["columns"] = voice_csv_columns();
    json arr = json::array();
    for (auto& r : rows) {
        const auto cells = row_cells(r);
        json o = json::object();
        for (size_t i = 0; i < cells.size(); ++i) {
            const std::string& c = voice_csv_columns()[i];
            if (c == "order") o[c] = r.line->order;
            else o[c] = cells[i];
        }
        arr.push_back(o);
    }
    j["lines"] = arr;
    return j;
}

std::map<std::string, std::string> voice_scripts(const Project& p, const std::vector<VoiceRow>& rows) {
    std::map<std::string, std::vector<const VoiceRow*>> by;
    for (auto& r : rows) {
        if (!r.line->spoken) continue;
        const std::string sp = r.line->speaker.empty() ? std::string("unknown") : r.line->speaker;
        by[spass::sound::safeName(sp) + "_" + spass::sound::safeName(r.language)].push_back(&r);
    }
    std::map<std::string, std::string> out;
    for (auto& [key, list] : by) {
        const StringLine& first = *list.front()->line;
        std::string s = "Voice script: " + (first.speaker.empty() ? std::string("unknown speaker") : first.speaker);
        if (!first.voice.empty()) s += " (voice " + first.voice + ")";
        s += "\r\nMod: " + p.mod.name + " (" + p.mod.id + ")   Language: " + list.front()->language + "   Lines: " + std::to_string(list.size()) + "\r\n";
        s += "Deliver one file per line, named as shown after 'file:' (any audio format; the editor converts it).\r\n";
        s += std::string(78, '=') + "\r\n";
        std::string last_ctx;
        int n = 0;
        for (const VoiceRow* r : list) {
            const StringLine& l = *r->line;
            if (l.context != last_ctx) {
                s += "\r\n## " + l.context + "\r\n";
                last_ctx = l.context;
            }
            s += "\r\n[" + std::to_string(++n) + "] " + l.id + "   file: " + r->expected_file + (r->status == "recorded" ? "   (recorded)" : "") + "\r\n";
            if (!l.listener.empty()) s += "    to: " + l.listener + "\r\n";
            const std::string stage = text_of(l.stage, r->language);
            if (!stage.empty()) s += "    (" + stage + ")\r\n";
            s += "    " + text_of(l.text, r->language) + "\r\n";
        }
        out[key + ".txt"] = s;
    }
    return out;
}

VoiceExportResult write_voice_export(const Project& p, const std::vector<const StringLine*>& lines, const VoiceExportOptions& o, const std::string& out_dir) {
    VoiceExportResult r;
    try {
        const auto rows = voice_rows(p, lines, o);
        r.rows = rows.size();
        const fs::path dir = u8(out_dir);
        write_text(dir / "voice_lines.csv", voice_csv(rows));
        r.written.push_back(u8str(dir / "voice_lines.csv"));
        write_text(dir / "voice_lines.json", voice_json(p, rows).dump(2) + "\n");
        r.written.push_back(u8str(dir / "voice_lines.json"));
        if (o.scripts)
            for (auto& [name, text] : voice_scripts(p, rows)) {
                write_text(dir / "scripts" / u8(name), text);
                r.written.push_back(u8str(dir / "scripts" / u8(name)));
            }
        r.ok = true;
    } catch (const std::exception& e) {
        r.error = e.what();
    }
    return r;
}

// ---- import ---------------------------------------------------------------------------------------------------------------------------------------

std::vector<std::vector<std::string>> parse_csv(const std::string& text) {
    std::vector<std::vector<std::string>> rows;
    size_t i = text.compare(0, 3, "\xEF\xBB\xBF") == 0 ? 3 : 0;
    std::vector<std::string> row;
    std::string cell;
    bool quoted = false, any = false;
    for (; i < text.size(); ++i) {
        const char c = text[i];
        if (quoted) {
            if (c == '"') {
                if (i + 1 < text.size() && text[i + 1] == '"') {
                    cell += '"';
                    ++i;
                } else {
                    quoted = false;
                }
            } else {
                cell += c;
            }
            continue;
        }
        if (c == '"') {
            quoted = true;
            any = true;
        } else if (c == ',') {
            row.push_back(std::move(cell));
            cell.clear();
            any = true;
        } else if (c == '\r' || c == '\n') {
            if (c == '\r' && i + 1 < text.size() && text[i + 1] == '\n') ++i;
            if (any || !cell.empty() || !row.empty()) {
                row.push_back(std::move(cell));
                rows.push_back(std::move(row));
            }
            row.clear();
            cell.clear();
            any = false;
        } else {
            cell += c;
            any = true;
        }
    }
    if (any || !cell.empty() || !row.empty()) {
        row.push_back(std::move(cell));
        rows.push_back(std::move(row));
    }
    return rows;
}

std::vector<CsvAssignment> csv_assignments(const std::string& text, std::string& error) {
    std::vector<CsvAssignment> out;
    const auto rows = parse_csv(text);
    if (rows.empty()) {
        error = tr("the CSV is empty");
        return out;
    }
    int ci = -1, cl = -1, cf = -1;
    for (size_t c = 0; c < rows[0].size(); ++c) {
        const std::string h = lower(rows[0][c]);
        if (h == "line_id" || h == "id") ci = static_cast<int>(c);
        else if (h == "language") cl = static_cast<int>(c);
        else if (h == "file") cf = static_cast<int>(c);
    }
    if (ci < 0 || cf < 0) {
        error = tr("the CSV needs the columns line_id and file (and language)");
        return out;
    }
    for (size_t r = 1; r < rows.size(); ++r) {
        auto cell = [&](int c) { return c >= 0 && static_cast<size_t>(c) < rows[r].size() ? rows[r][static_cast<size_t>(c)] : std::string(); };
        CsvAssignment a{cell(ci), cell(cl), cell(cf)};
        if (a.line_id.empty() || a.file.empty()) continue;
        out.push_back(std::move(a));
    }
    return out;
}

bool is_audio_candidate(const std::string& file) {
    static const std::set<std::string> exts = {"ogg", "oga", "wav", "mp3", "flac", "opus", "m4a", "aac", "aif", "aiff", "wma", "webm", "mka"};
    return exts.count(ext_lower(file)) != 0;
}

std::string voice_asset_name(const std::string& line_id, const std::string& language, const std::string& ext) {
    return spass::sound::safeName(line_id) + "_" + spass::sound::safeName(language) + "." + lower(ext);
}

VoiceMatchReport match_recordings(const Project& p, const std::vector<StringLine>& lines, const std::vector<std::string>& files,
                                  const std::vector<std::string>& languages, const std::string& default_language, const std::vector<CsvAssignment>* csv) {
    VoiceMatchReport rep;
    std::unordered_map<std::string, std::vector<const StringLine*>> by_id, by_key;
    for (auto& l : lines) {
        if (l.game || !l.spoken || l.node_uid.empty()) continue;
        by_id[lower(l.id)].push_back(&l);
        if (!l.runtime_key.empty()) by_key[lower(spass::sound::safeName(l.runtime_key))].push_back(&l);
    }
    std::unordered_map<std::string, std::vector<const CsvAssignment*>> csv_by_file;
    if (csv)
        for (auto& a : *csv) {
            std::string f = a.file;
            std::replace(f.begin(), f.end(), '\\', '/');
            csv_by_file[lower(f)].push_back(&a);
            const std::string bare = lower(p2m::bare_name(f));
            if (bare != lower(f)) csv_by_file[bare].push_back(&a);
        }
    struct Cand {
        const StringLine* line;
        std::string lang, how;
    };
    const std::string mod_prefix = lower("INFO_SPASS_" + spass::sound::safeName(p.mod.id) + "_");
    for (const std::string& file : files) {
        std::string rel = file;
        std::replace(rel.begin(), rel.end(), '\\', '/');
        if (!is_audio_candidate(file)) {
            rep.unmatched.push_back({file, "not an audio file"});
            continue;
        }
        std::vector<Cand> cands;
        std::string why;
        // 1. the CSV names the file
        if (csv) {
            auto it = csv_by_file.find(lower(rel));
            if (it == csv_by_file.end()) it = csv_by_file.find(lower(p2m::bare_name(rel)));
            if (it == csv_by_file.end())  // a path relative to the folder ("German/x.wav")
                for (auto c = csv_by_file.begin(); c != csv_by_file.end(); ++c)
                    if (c->first.find('/') != std::string::npos && lower(rel).size() > c->first.size() &&
                        lower(rel).compare(lower(rel).size() - c->first.size() - 1, std::string::npos, "/" + c->first) == 0) {
                        it = c;
                        break;
                    }
            if (it != csv_by_file.end()) {
                std::set<const CsvAssignment*> uniq(it->second.begin(), it->second.end());
                for (const CsvAssignment* a : uniq) {
                    auto li = by_id.find(lower(a->line_id));
                    if (li == by_id.end()) {
                        why = "the CSV assigns it to '" + a->line_id + "', which is not one of the lines";
                        continue;
                    }
                    std::string lang = a->language.empty() ? default_language : language_from_suffix(a->language, languages);
                    if (lang.empty()) lang = a->language;
                    for (const StringLine* l : li->second) cands.push_back({l, lang, "csv"});
                }
            }
        }
        // 2. by name
        if (cands.empty() && why.empty()) {
            const std::string stem = stem_of(file);
            const std::string ls = lower(stem);
            // the layer's own name: INFO_SPASS_<mod>_<key>_<Language>_%_SPASS
            if (ls.rfind(mod_prefix, 0) == 0 && ls.size() > mod_prefix.size() + 8 && ls.compare(ls.size() - 8, 8, "_%_spass") == 0) {
                const std::string mid = stem.substr(mod_prefix.size(), stem.size() - mod_prefix.size() - 8);
                const auto us = mid.rfind('_');
                if (us != std::string::npos) {
                    const std::string key = lower(mid.substr(0, us)), lang_part = mid.substr(us + 1);
                    std::string lang = lower(lang_part) == "default" ? std::string("default") : language_from_suffix(lang_part, languages);
                    if (auto it = by_key.find(key); it != by_key.end() && !lang.empty())
                        for (const StringLine* l : it->second) cands.push_back({l, lang, "runtime name"});
                }
            }
            if (cands.empty()) {
                const auto us = stem.rfind('_');
                if (us != std::string::npos) {
                    const std::string lang = language_from_suffix(stem.substr(us + 1), languages);
                    if (!lang.empty())
                        if (auto it = by_id.find(lower(stem.substr(0, us))); it != by_id.end())
                            for (const StringLine* l : it->second) cands.push_back({l, lang, "name"});
                }
                if (auto it = by_id.find(ls); it != by_id.end())
                    for (const StringLine* l : it->second) cands.push_back({l, default_language, "name (default language)"});
            }
            if (cands.empty()) why = "no line has this name (expected <line id>_<Language>, e.g. " +
                                     (by_id.empty() ? std::string("my_mod_1a2b3c4d_English") : voice_asset_name(by_id.begin()->second.front()->id, default_language, "ogg")) + ")";
        }
        if (cands.empty()) {
            rep.unmatched.push_back({file, why});
            continue;
        }
        // one node and language each
        std::vector<Cand> uniq;
        for (auto& c : cands)
            if (std::none_of(uniq.begin(), uniq.end(), [&](const Cand& u) { return u.line->node_uid == c.line->node_uid && lower(u.lang) == lower(c.lang); }))
                uniq.push_back(c);
        if (uniq.size() > 1) {
            std::vector<std::string> names;
            for (auto& c : uniq) names.push_back(c.line->id + "/" + c.lang);
            rep.ambiguous.push_back({file, names});
            continue;
        }
        rep.matched.push_back({file, uniq[0].line->id, uniq[0].line->node_uid, uniq[0].lang, uniq[0].how});
    }
    // two files for one line and language: none is taken
    std::map<std::string, std::vector<size_t>> groups;
    for (size_t i = 0; i < rep.matched.size(); ++i) groups[rep.matched[i].node_uid + "/" + lower(rep.matched[i].language)].push_back(i);
    std::set<size_t> drop;
    for (auto& [k, ix] : groups) {
        if (ix.size() < 2) continue;
        std::vector<std::string> fs_;
        for (size_t i : ix) {
            fs_.push_back(rep.matched[i].file);
            drop.insert(i);
        }
        rep.duplicates.push_back({rep.matched[ix[0]].line_id + "/" + rep.matched[ix[0]].language, fs_});
    }
    if (!drop.empty()) {
        std::vector<VoiceMatch> keep;
        for (size_t i = 0; i < rep.matched.size(); ++i)
            if (!drop.count(i)) keep.push_back(rep.matched[i]);
        rep.matched = std::move(keep);
    }
    return rep;
}

int apply_voice_import(Project& p, const std::vector<ImportedVoice>& files) {
    int changed = 0;
    for (const ImportedVoice& f : files) {
        GraphNode* n = find_node(p, f.node_uid);
        if (!n) continue;
        std::string rel = f.rel_path;
        std::replace(rel.begin(), rel.end(), '\\', '/');
        const std::string bare = p2m::bare_name(rel);
        bool asset_changed = false;
        if (AssetFile* a = const_cast<AssetFile*>(find_asset(p, bare))) {
            if (a->file != rel) {
                a->file = rel;
                asset_changed = true;
            }
        } else {
            p.assets.push_back({new_uid(), rel, asset_kind(bare)});
            asset_changed = true;
        }
        json s = n->data.value("sound", json::object());
        if (!s.is_object()) s = json::object();
        if (s.value(f.language, std::string()) != bare || asset_changed) ++changed;
        s[f.language] = bare;
        n->data["sound"] = s;
    }
    return changed;
}

}  // namespace mk
