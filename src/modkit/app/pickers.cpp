#include "app/pickers.h"

#include "app/audio.h"
#include "app/dragdrop.h"
#include "app/icons.h"
#include "app/widgets.h"
#include "core/assets.h"
#include "core/catalog.h"
#include "core/i18n.h"
#include "core/image.h"
#include "pack2mod/util.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cstring>
#include <unordered_map>
#include <functional>

namespace mk::ui {

namespace {

void visible_label(const char* label) {
    const char* end = std::strstr(label, "##");
    const size_t n = end ? static_cast<size_t>(end - label) : std::strlen(label);
    if (!n) return;
    ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x);
    ImGui::TextUnformatted(label, label + n);
}

bool icontains(const std::string& hay, const std::string& needle) {
    if (needle.empty()) return true;
    auto it = std::search(hay.begin(), hay.end(), needle.begin(), needle.end(),
                          [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); });
    return it != hay.end();
}

}  // namespace

std::string label_of(const App& app, const std::string& name) {
    for (auto& i : app.project.items)
        if (i.name == name && !i.label.empty()) return i.label.begin()->second;
    for (auto& n : app.project.npc_templates)
        if (n.name == name && !n.label.empty()) return n.label.begin()->second;
    for (auto& r : app.project.recipes)
        if (r.name == name && !r.label.empty()) return r.label.begin()->second;
    return app.game ? app.game->label(name) : std::string();
}

bool template_picker(const char* label, std::string& name, const GameData* game, std::initializer_list<TplKind> kinds, const std::vector<std::string>& extra,
                     float width) {
    bool changed = false;
    ImGui::PushID(label);
    const bool known = name.empty() || !game || game->tpl(name) || std::find(extra.begin(), extra.end(), name) != extra.end();
    if (!known) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.65f, 0.3f, 1));
    const bool items = std::find(kinds.begin(), kinds.end(), TplKind::Item) != kinds.end();
    float icon_w = 0;
    if (items) {  // the picked item's icon in front of the field
        const float s = ImGui::GetFrameHeight();
        item_icon(name, s);
        ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x);
        icon_w = s + ImGui::GetStyle().ItemInnerSpacing.x;
    }
    ImGui::SetNextItemWidth((width > 0 ? width : ImGui::CalcItemWidth() - ImGui::GetFrameHeight() - ImGui::GetStyle().ItemInnerSpacing.x) - icon_w);
    changed |= ImGui::InputText("##name", &name);
    if (!known) {
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered() && !dragging()) ImGui::SetTooltip("%s", tr("No template with this name in the game data or the project"));
    }
    if (const DragTemplate* d = drop_target(kinds, extra)) {
        name = d->name;
        changed = true;
    }
    ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x);
    static std::string filter;
    if (ImGui::ArrowButton("##open", ImGuiDir_Down)) {
        filter.clear();
        ImGui::OpenPopup("##pick");
    }
    visible_label(label);
    if (ImGui::BeginPopup("##pick")) {
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(360);
        ImGui::InputTextWithHint("##filter", tr("filter by name or label"), &filter);
        ImGui::BeginChild("##list", ImVec2(520, 360));
        auto row = [&](const std::string& n, const std::string& lbl, const char* tag) {
            if (!icontains(n, filter) && !icontains(lbl, filter)) return;
            if (items) {
                item_icon(n, 20.0f);
                ImGui::SameLine();
            }
            if (ImGui::Selectable((n + "##" + tag).c_str(), n == name, ImGuiSelectableFlags_None)) {
                name = n;
                changed = true;
                ImGui::CloseCurrentPopup();
            }
            if (!lbl.empty()) {
                ImGui::SameLine(items ? 330.0f : 300.0f);
                ImGui::TextDisabled("%s", lbl.c_str());
            }
        };
        for (auto& e : extra) row(e, "(project)", "p");
        if (game) {
            ImGuiListClipper clip;
            std::vector<const TemplateInfo*> list;
            for (auto& t : game->templates()) {
                if (kinds.size() && std::find(kinds.begin(), kinds.end(), t->kind) == kinds.end()) continue;
                const std::string lbl = game->label(t->name);
                if (!icontains(t->name, filter) && !icontains(lbl, filter)) continue;
                list.push_back(t.get());
            }
            clip.Begin(static_cast<int>(list.size()));
            while (clip.Step())
                for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i) row(list[i]->name, game->label(list[i]->name), "g");
        }
        ImGui::EndChild();
        ImGui::EndPopup();
    }
    ImGui::PopID();
    return changed;
}

namespace {

// the shared look of the lookup fields: a text field that takes free text and an arrow that opens a filterable list; `rows` fills the list for the typed
// filter (called again only when the filter changed), `known` says whether the current value is one of the choices
struct LookupRow {
    std::string value, a, b, c;  // the value a pick sets, then three display columns
};

bool lookup_field(const char* label, std::string& value, const char* hint, float width, bool known, const char* unknown_tip,
                  const std::function<void(const std::string& filter, std::vector<LookupRow>& out)>& rows, float col_a, float col_b, float col_c) {
    bool changed = false;
    ImGui::PushID(label);
    if (!known) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.65f, 0.3f, 1));
    ImGui::SetNextItemWidth(width > 0 ? width : ImGui::CalcItemWidth() - ImGui::GetFrameHeight() - ImGui::GetStyle().ItemInnerSpacing.x);
    changed |= ImGui::InputTextWithHint("##value", hint, &value);
    if (!known) {
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", unknown_tip);
    }
    ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x);
    static std::string filter, cached_for;
    static std::vector<LookupRow> list;
    static bool dirty = true;
    if (ImGui::ArrowButton("##open", ImGuiDir_Down)) {
        filter.clear();
        dirty = true;
        ImGui::OpenPopup("##pick");
    }
    visible_label(label);
    if (ImGui::BeginPopup("##pick")) {
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(420);
        if (ImGui::InputTextWithHint("##filter", tr("filter: any part of the name, owner, quest, text ..."), &filter)) dirty = true;
        if (dirty || cached_for != label) {
            list.clear();
            rows(filter, list);
            cached_for = label;
            dirty = false;
        }
        ImGui::TextDisabled(tr("%zu matches"), list.size());
        ImGui::BeginChild("##list", ImVec2(col_c > 0 ? col_c + 260 : col_b + 240, 380));
        ImGuiListClipper clip;
        clip.Begin(static_cast<int>(list.size()));
        while (clip.Step())
            for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i) {
                const LookupRow& r = list[static_cast<size_t>(i)];
                if (ImGui::Selectable((r.value + "##" + std::to_string(i)).c_str(), r.value == value)) {
                    value = r.value;
                    changed = true;
                    dirty = true;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine(col_a);
                ImGui::TextDisabled("%s", r.a.c_str());
                ImGui::SameLine(col_b);
                ImGui::TextDisabled("%s", r.b.c_str());
                if (col_c > 0) {
                    ImGui::SameLine(col_c);
                    ImGui::TextDisabled("%s", r.c.c_str());
                }
            }
        ImGui::EndChild();
        ImGui::EndPopup();
    }
    ImGui::PopID();
    return changed;
}

std::string clip_text(std::string s, size_t n) {
    for (char& c : s)
        if (c == '\n' || c == '\r') c = ' ';
    if (s.size() > n) s = s.substr(0, n) + "...";
    return s;
}

// distinct NPC entities of the world by name
struct NpcName {
    std::string name, template_name, layer;
    size_t count = 0;
};
std::vector<NpcName> world_npc_names(const World& w) {
    std::vector<NpcName> out;
    std::unordered_map<std::string, size_t> at;
    for (const WorldEntity& e : w.entities) {
        if (!e.npc || e.name.empty()) continue;
        auto it = at.find(e.name);
        if (it == at.end()) {
            at[e.name] = out.size();
            out.push_back({e.name, e.template_name, w.layers[e.layer].name, 1});
        } else {
            ++out[it->second].count;
        }
    }
    return out;
}

}  // namespace

bool info_picker(const char* label, std::string& name, App& app, float width) {
    const GameData* g = app.game.get();
    bool known = name.empty();
    if (g)
        for (auto& i : g->infos())
            if (i.name == name) {
                known = true;
                break;
            }
    auto rows = [g](const std::string& filter, std::vector<LookupRow>& out) {
        if (!g) return;
        for (auto& i : g->infos()) {
            const std::string line = g->info_text(i.name);
            if (!icontains(i.name, filter) && !icontains(i.owner, filter) && !icontains(i.quest, filter) && !icontains(line, filter)) continue;
            out.push_back({i.name, i.owner.empty() ? "-" : i.owner, i.quest.empty() ? "-" : i.quest, clip_text(line, 90)});
        }
    };
    const bool ch = lookup_field(label, name, tr("dialogue info (FILLERX2_00029)"), width, known, tr("No dialogue info of the game has this name"), rows, 190, 300, 480);
    if (g && !name.empty() && ImGui::IsItemHovered() && known) {
        const std::string line = g->info_text(name);
        if (!line.empty()) ImGui::SetTooltip("%s", clip_text(line, 200).c_str());
    }
    return ch;
}

bool npc_name_picker(const char* label, std::string& name, App& app, float width) {
    if (!app.world) app.request_world();
    bool known = name.empty();
    if (app.world)
        for (const WorldEntity& e : app.world->entities)
            if (e.npc && e.name == name) {
                known = true;
                break;
            }
    for (auto& n : app.project.npcs)
        if (n.name == name) known = true;
    const World* w = app.world.get();
    const Project* p = &app.project;
    auto rows = [w, p](const std::string& filter, std::vector<LookupRow>& out) {
        for (auto& n : p->npcs)
            if (!n.name.empty() && (icontains(n.name, filter) || icontains(n.template_name, filter))) out.push_back({n.name, "(project)", n.template_name, ""});
        if (!w) return;
        for (const NpcName& n : world_npc_names(*w)) {
            if (!icontains(n.name, filter) && !icontains(n.template_name, filter) && !icontains(n.layer, filter)) continue;
            out.push_back({n.name, n.count > 1 ? trf("{0} in the world", {std::to_string(n.count)}) : std::string(tr("1 in the world")), n.template_name, n.layer});
        }
    };
    if (!app.world) ImGui::BeginDisabled();
    const bool ch = lookup_field(label, name, app.world ? tr("NPC name") : tr("loading the world..."), width, known || !app.world,
                                 tr("No NPC with this name in the world or the project"), rows, 230, 380, 560);
    if (!app.world) ImGui::EndDisabled();
    return ch;
}

bool entity_id_picker(const char* label, std::string& guid, App& app, const std::string& name_filter, std::string* name, float width) {
    if (!app.world) app.request_world();
    const World* w = app.world.get();
    const bool known = guid.empty() || !w || w->by_guid(guid) != nullptr;
    const std::string wanted = p2m::lower(name_filter);
    auto rows = [w, wanted](const std::string& filter, std::vector<LookupRow>& out) {
        if (!w) return;
        for (const WorldEntity& e : w->entities) {
            if (wanted.empty() ? !e.npc : p2m::lower(e.name) != wanted) continue;
            char pos[64];
            std::snprintf(pos, sizeof pos, "%.0f %.0f %.0f", e.x(), e.y(), e.z());
            const std::string layer = w->layers[e.layer].name;
            if (!icontains(e.guid, filter) && !icontains(e.name, filter) && !icontains(e.template_name, filter) && !icontains(layer, filter)) continue;
            out.push_back({e.guid, e.name, layer, std::string(pos) + (e.template_name.empty() ? "" : "   " + e.template_name)});
        }
    };
    const bool ch = lookup_field(label, guid, w ? tr("entity ID") : tr("loading the world..."), width, known, tr("No entity of the world has this ID"), rows, 300, 460, 640);
    if (ch && name && w)
        if (const WorldEntity* e = w->by_guid(guid))
            if (e->name != *name) *name = e->name;
    return ch;
}

bool quest_picker(const char* label, std::string& name, App& app, const std::string& exclude, float width) {
    bool changed = false;
    ImGui::PushID(label);
    const GameData* g = app.game.get();
    bool known = name.empty() || (g && g->quest(name));
    for (auto& q : app.project.quests)
        if (q.name == name) known = true;
    if (!known) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.65f, 0.3f, 1));
    ImGui::SetNextItemWidth(width > 0 ? width : ImGui::CalcItemWidth() - ImGui::GetFrameHeight() - ImGui::GetStyle().ItemInnerSpacing.x);
    changed |= ImGui::InputTextWithHint("##name", tr("quest id"), &name);
    if (!known) {
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("No quest with this id in the project or the game"));
    } else if (g && !name.empty() && ImGui::IsItemHovered()) {
        if (const GameQuest* gq = g->quest(name)) {
            const std::string t = gq->title_id.empty() ? std::string() : g->text(gq->title_id);
            ImGui::SetTooltip(tr("a quest of the game%s%s"), t.empty() ? "" : ": ", t.c_str());
        }
    }
    ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x);
    static std::string filter;
    if (ImGui::ArrowButton("##open", ImGuiDir_Down)) {
        filter.clear();
        ImGui::OpenPopup("##pick");
    }
    visible_label(label);
    if (ImGui::BeginPopup("##pick")) {
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(360);
        ImGui::InputTextWithHint("##filter", tr("filter by id or title"), &filter);
        ImGui::BeginChild("##list", ImVec2(620, 380));
        struct Row {
            std::string id, title;
            bool mod, silent;
        };
        std::vector<Row> rows;
        for (auto& q : app.project.quests) {
            if (q.name.empty() || q.name == exclude) continue;
            const std::string t = q.title.empty() ? std::string() : q.title.begin()->second;
            if (icontains(q.name, filter) || icontains(t, filter)) rows.push_back({q.name, t, true, q.silent});
        }
        if (g)
            for (auto& q : g->quests()) {
                const std::string t = q.title_id.empty() ? std::string() : g->text(q.title_id);
                if (icontains(q.name, filter) || icontains(t, filter)) rows.push_back({q.name, t, false, q.silent});
            }
        ImGuiListClipper clip;
        clip.Begin(static_cast<int>(rows.size()));
        while (clip.Step())
            for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i) {
                const Row& r = rows[i];
                ImGui::PushID(i);
                if (r.mod) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.8f, 1.0f, 1.0f));
                if (ImGui::Selectable(r.id.c_str(), r.id == name)) {
                    name = r.id;
                    changed = true;
                    ImGui::CloseCurrentPopup();
                }
                if (r.mod) ImGui::PopStyleColor();
                ImGui::SameLine(300);
                ImGui::TextDisabled("%s%s%s", r.mod ? tr("(mod) ") : "", r.title.c_str(), r.silent ? tr("  [silent]") : "");
                ImGui::PopID();
            }
        ImGui::EndChild();
        ImGui::TextDisabled(tr("%zu quests%s"), rows.size(), g ? "" : tr(" (load the game data to see the game's quests)"));
        ImGui::EndPopup();
    }
    ImGui::PopID();
    return changed;
}

bool asset_picker(const char* label, std::string& name, const GameData* game, const char* ext, bool strip_compiled, float width) {
    bool changed = false;
    ImGui::PushID(label);
    ImGui::SetNextItemWidth(width > 0 ? width : ImGui::CalcItemWidth() - ImGui::GetFrameHeight() - ImGui::GetStyle().ItemInnerSpacing.x);
    changed |= ImGui::InputText("##name", &name);
    ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x);
    static std::string filter;
    if (ImGui::ArrowButton("##open", ImGuiDir_Down)) {
        filter.clear();
        ImGui::OpenPopup("##pick");
    }
    visible_label(label);
    if (ImGui::BeginPopup("##pick")) {
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(360);
        ImGui::InputTextWithHint("##filter", tr("filter"), &filter);
        ImGui::BeginChild("##list", ImVec2(520, 360));
        if (game) {
            std::vector<std::string> list;
            for (auto& a : game->assets(ext)) {
                std::string n = a.name;
                if (strip_compiled) {
                    // "X._xmsh" -> "X.xmsh"
                    const auto dot = n.rfind("._");
                    if (dot != std::string::npos) n.erase(dot + 1, 1);
                }
                if (icontains(n, filter)) list.push_back(n);
            }
            ImGuiListClipper clip;
            clip.Begin(static_cast<int>(list.size()));
            while (clip.Step())
                for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i)
                    if (ImGui::Selectable(list[i].c_str(), list[i] == name)) {
                        name = list[i];
                        changed = true;
                        ImGui::CloseCurrentPopup();
                    }
        } else {
            ImGui::TextDisabled("%s", tr("load the game data first (Settings)"));
        }
        ImGui::EndChild();
        ImGui::EndPopup();
    }
    ImGui::PopID();
    return changed;
}

bool sound_button(App& app, const std::string& ref, const char* id) {
    const bool on = !ref.empty() && app.audio().current() == ref;
    ImGui::BeginDisabled(ref.empty());
    const bool clicked = ImGui::SmallButton(((on ? std::string("Stop") : std::string("Play")) + id).c_str());
    ImGui::EndDisabled();
    if (clicked) app.toggle_sound(ref);
    if (on && ImGui::IsItemHovered()) ImGui::SetTooltip("%.1f / %.1f s", app.audio().position(), app.audio().length());
    return clicked;
}

bool sound_picker(const char* label, std::string& ref, App& app, bool raw_name, float width) {
    // a raw name is played as the compiled file of the same stem
    auto compiled_of = [&](const std::string& v) -> std::string {
        if (!raw_name || is_compiled_sound(v)) return v;
        const auto dot = v.rfind('.');
        return (dot == std::string::npos ? v : v.substr(0, dot)) + "._xsnd";
    };
    bool changed = false;
    ImGui::PushID(label);
    const float play_w = ImGui::CalcTextSize("Stop").x + ImGui::GetStyle().FramePadding.x * 2 + ImGui::GetStyle().ItemInnerSpacing.x;
    ImGui::SetNextItemWidth(width > 0 ? width : ImGui::CalcItemWidth() - ImGui::GetFrameHeight() - ImGui::GetStyle().ItemInnerSpacing.x - play_w);
    changed |= ImGui::InputTextWithHint("##name", raw_name ? "X.wav" : tr("a sound"), &ref);
    ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x);
    static std::string filter;
    static int group = 0;  // 0 all, 1.. a voice language, last = effects (no language)
    if (ImGui::ArrowButton("##open", ImGuiDir_Down)) {
        filter.clear();
        ImGui::OpenPopup("##pick");
    }
    ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x);
    sound_button(app, compiled_of(ref));
    visible_label(label);
    if (ImGui::BeginPopup("##pick")) {
        static const char* kGroups[] = {N_("all"), N_("English"), N_("German"), N_("French"), N_("Russian"), N_("effects and other")};
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(360);
        ImGui::InputTextWithHint("##filter", tr("filter"), &filter);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(160);
        ui::combo_items("##group", &group, kGroups, IM_ARRAYSIZE(kGroups));
        ImGui::BeginChild("##list", ImVec2(640, 380));
        struct Row {
            std::string value, play;  // what is stored, what is played
            bool project;
        };
        std::vector<Row> list;
        for (auto& a : app.project.assets) {
            const std::string n = p2m::bare_name(a.file);
            if (asset_kind(n) != "sound" || (raw_name && !is_compiled_sound(n)) || !icontains(n, filter)) continue;
            list.push_back({raw_name ? sound_raw_name(n) : n, n, true});
        }
        if (app.game) {
            for (auto& a : app.game->assets("_xsnd")) {
                if (!icontains(a.name, filter)) continue;
                if (group > 0) {
                    const std::string lang = sound_language(a.name);
                    if (group == IM_ARRAYSIZE(kGroups) - 1 ? !lang.empty() : lang != kGroups[group]) continue;
                }
                list.push_back({raw_name ? sound_raw_name(a.name) : a.name, a.name, false});
            }
        } else {
            ImGui::TextDisabled("%s", tr("load the game data (Settings) to see the game's sounds"));
        }
        ImGuiListClipper clip;
        clip.Begin(static_cast<int>(list.size()));
        while (clip.Step())
            for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i) {
                ImGui::PushID(i);
                sound_button(app, list[i].play);
                ImGui::SameLine();
                if (list[i].project) {
                    ImGui::TextColored(ImVec4(0.55f, 0.8f, 1.0f, 1.0f), "%s", tr("project"));
                    ImGui::SameLine();
                }
                if (ImGui::Selectable(list[i].value.c_str(), list[i].value == ref)) {
                    ref = list[i].value;
                    changed = true;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::PopID();
            }
        ImGui::EndChild();
        ImGui::TextDisabled(tr("%zu sounds"), list.size());
        ImGui::EndPopup();
    }
    ImGui::PopID();
    return changed;
}

bool sound_rows(const char* label, json& sound, App& app) {
    if (!sound.is_object()) sound = json::object();
    bool changed = false;
    ImGui::PushID(label);
    ImGui::TextUnformatted(label);
    ImGui::SameLine();
    help(tr("Spoken audio for the line: an audio file of the project (Assets panel: Ogg Vorbis, WAV, MP3) or a sound of the game. 'every' plays in "
         "every voice language; a language row replaces it for that voice language. The export writes the files into sounds/ of the mod."));
    std::vector<std::string> rows = {"default"};
    for (auto& l : app.project.mod.languages) rows.push_back(l);
    for (auto& r : rows) {
        ImGui::PushID(r.c_str());
        std::string v = sound.value(r, std::string());
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%-8s", r == "default" ? "every" : r.c_str());
        ImGui::SameLine(90);
        ImGui::PushItemWidth(-FLT_MIN);
        if (sound_picker("##snd", v, app)) {
            if (v.empty()) sound.erase(r);
            else sound[r] = v;
            changed = true;
        }
        ImGui::PopItemWidth();
        ImGui::PopID();
    }
    ImGui::PopID();
    return changed;
}

namespace {

// a name field with an arrow that opens a filtered list: `rows` are (name, note, from the mod), `draw_pre` draws in front of a row (an icon)
bool list_picker(const char* label, std::string& name, bool known, const char* hint, const char* unknown_tip, const char* start_filter,
                 const std::function<void(const std::string& filter, std::vector<std::pair<std::string, bool>>& rows)>& collect,
                 const std::function<void(const std::string&)>& draw_pre, float row_height, const char* count_fmt, float width) {
    bool changed = false;
    ImGui::PushID(label);
    if (!known) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.65f, 0.3f, 1));
    ImGui::SetNextItemWidth(width > 0 ? width : ImGui::CalcItemWidth() - ImGui::GetFrameHeight() - ImGui::GetStyle().ItemInnerSpacing.x);
    changed |= ImGui::InputTextWithHint("##name", hint, &name);
    if (!known) {
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", unknown_tip);
    }
    ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x);
    static std::string filter;
    if (ImGui::ArrowButton("##open", ImGuiDir_Down)) {
        filter = start_filter ? start_filter : "";
        ImGui::OpenPopup("##pick");
    }
    visible_label(label);
    if (ImGui::BeginPopup("##pick")) {
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(360);
        ImGui::InputTextWithHint("##filter", tr("filter"), &filter);
        ImGui::BeginChild("##list", ImVec2(520, 380));
        std::vector<std::pair<std::string, bool>> rows;
        collect(filter, rows);
        ImGuiListClipper clip;
        clip.Begin(static_cast<int>(rows.size()), row_height > 0 ? row_height : -1.0f);
        while (clip.Step())
            for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i) {
                const auto& [n, mod] = rows[i];
                ImGui::PushID(i);
                if (draw_pre) {
                    draw_pre(n);
                    ImGui::SameLine();
                }
                if (mod) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.8f, 1.0f, 1.0f));
                if (ImGui::Selectable(mod ? (n + "  " + tr("(picture of the project)")).c_str() : n.c_str(), n == name)) {
                    name = n;
                    changed = true;
                    ImGui::CloseCurrentPopup();
                }
                if (mod) ImGui::PopStyleColor();
                ImGui::PopID();
            }
        ImGui::EndChild();
        ImGui::TextDisabled(count_fmt, rows.size());
        ImGui::EndPopup();
    }
    ImGui::PopID();
    return changed;
}

}  // namespace

bool icon_picker(const char* label, std::string& icon, App& app, float width) {
    const GameData* g = app.game.get();
    static const GameData* cached_for = nullptr;
    static std::vector<std::string> game_icons;
    if (g != cached_for) {
        game_icons = g ? known_icons(*g) : std::vector<std::string>{};
        cached_for = g;
    }
    std::vector<std::string> pictures;
    for (auto& a : app.project.assets) {
        const std::string n = p2m::bare_name(a.file);
        if (is_picture(n)) pictures.push_back(n);
    }
    auto same = [&](const std::string& n) { return p2m::lower(n) == p2m::lower(icon); };
    const bool known = icon.empty() || !g || std::any_of(pictures.begin(), pictures.end(), same) || std::any_of(game_icons.begin(), game_icons.end(), same);
    ui::icon(icon, 32.0f);
    ImGui::SameLine();
    return list_picker(label, icon, known, tr("icon"), tr("No picture of the project and no icon of the game has this name"), "",
                       [&](const std::string& f, std::vector<std::pair<std::string, bool>>& rows) {
                           for (auto& n : pictures)
                               if (icontains(n, f)) rows.emplace_back(n, true);
                           for (auto& n : game_icons)
                               if (icontains(n, f)) rows.emplace_back(n, false);
                       },
                       [](const std::string& n) { ui::icon(n, 20.0f); }, 0, g ? tr("%zu icons") : tr("%zu icons (load the game data to see the game's icons)"), width);
}

bool effect_picker(const char* label, std::string& name, App& app, const char* start_filter, float width) {
    const GameData* g = app.game.get();
    const bool known = name.empty() || !g || std::any_of(g->effects().begin(), g->effects().end(), [&](const std::string& e) { return p2m::lower(e) == p2m::lower(name); });
    return list_picker(label, name, known, tr("effect name"), tr("The game has no effect of this name"), start_filter,
                       [&](const std::string& f, std::vector<std::pair<std::string, bool>>& rows) {
                           if (g)
                               for (auto& e : g->effects())
                                   if (icontains(e, f)) rows.emplace_back(e, false);
                       },
                       nullptr, ImGui::GetTextLineHeightWithSpacing(), g ? tr("%zu effects") : tr("%zu effects (load the game data to see the game's effects)"), width);
}

}  // namespace mk::ui
