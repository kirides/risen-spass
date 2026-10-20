#include "app/palette.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cctype>

#include "app/panels/panels.h"
#include "core/i18n.h"
#include "core/search.h"
#include "core/spells.h"

namespace mk {


int CommandPalette::score(const std::string& query, const std::string& text) { return search_score(query, text); }

std::vector<const CommandPalette::Entry*> CommandPalette::search(const std::vector<Entry>& all, const std::string& query, size_t limit) {
    std::vector<std::pair<int, const Entry*>> hits;
    bool empty = true;
    for (char c : query)
        if (!std::isspace(static_cast<unsigned char>(c))) empty = false;
    for (auto& e : all) {
        if (empty) {
            if (e.kind == "Command" || e.kind == "Panel") hits.push_back({1000 - static_cast<int>(hits.size()), &e});
            continue;
        }
        int s = score(query, e.label + " " + e.detail + " " + e.kind);
        if (s == 0) continue;
        // the name itself beats a hit in the label text
        if (const int n = score(query, e.label)) s += n / 2;
        if (e.kind == "Command" || e.kind == "Panel") s += 15;
        else if (e.kind.rfind("Shipped", 0) == 0) s -= 25;  // the project's own things come first
        hits.push_back({s, &e});
    }
    std::stable_sort(hits.begin(), hits.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    std::vector<const Entry*> out;
    for (auto& h : hits) {
        if (out.size() >= limit) break;
        out.push_back(h.second);
    }
    return out;
}

// the kind column: "Command", "Panel", a panel title or "Shipped <kind>" (the English text is what the ranking looks at)
static std::string kind_text(const std::string& k) {
    if (k.rfind("Shipped ", 0) == 0) return trf("Shipped {0}", {tr(k.c_str() + 8)});
    return tr(k.c_str());
}

std::vector<CommandPalette::Entry> CommandPalette::collect(App& app) {
    std::vector<Entry> v;
    auto add = [&](const std::string& kind, const std::string& label, const std::string& detail, std::function<void(App&)> run) {
        v.push_back({kind, label, detail, std::move(run)});
    };
    // commands
    add(N_("Command"), tr("Play"), tr("export and start the game (F5)"), [](App& a) { a.play(); });
    add(N_("Command"), tr("Export mod..."), tr("write the mod into a folder (Ctrl+E)"), [](App& a) { a.request_export(); });
    add(N_("Command"), tr("Save project"), "(Ctrl+S)", [](App& a) { a.save_project(false); });
    add(N_("Command"), tr("New project"), "(Ctrl+N)", [](App& a) { a.new_project(); });
    add(N_("Command"), tr("Open project..."), "(Ctrl+O)", [](App& a) { a.request_open(); });
    add(N_("Command"), tr("Wizard: fetch quest"), tr("an NPC asks for items; quest and dialogue"), [](App& a) { a.open_wizard(WizardKind::FetchQuest); });
    add(N_("Command"), tr("Wizard: new weapon with a recipe"), tr("an item and the recipe that crafts it"), [](App& a) { a.open_wizard(WizardKind::Weapon); });
    add(N_("Command"), tr("Wizard: trader with stock"), tr("an NPC who sells things"), [](App& a) { a.open_wizard(WizardKind::Trader); });
    // panels
    for (const std::string& t : app.panel_titles()) add(N_("Panel"), tr(t.c_str()), tr("open the panel"), [t](App& a) { a.select_in(t, ""); });
    // the project
    const Project& p = app.project;
    auto jump = [](const char* panel, const std::string& uid) { return [panel, uid](App& a) { a.select_in(panel, uid); }; };
    auto text_of = [&](const Text& t) {
        for (auto& l : p.mod.languages)
            if (auto it = t.find(l); it != t.end() && !it->second.empty()) return it->second;
        return t.empty() ? std::string() : t.begin()->second;
    };
    for (auto& i : p.items) add("Items", i.name, text_of(i.label) + (i.mode == Mode::Edit ? tr(" (change)") : tr(" (new)")), jump("Items", i.uid));
    for (auto& r : p.recipes) add("Recipes", r.name, text_of(r.label) + (r.result ? " -> " + *r.result : std::string()), jump("Recipes", r.uid));
    for (auto& n : p.npc_templates) add("NPCs", n.name, text_of(n.label) + (n.mode == Mode::Edit ? tr(" (change)") : tr(" (template)")), jump("NPCs", n.uid));
    for (auto& n : p.npcs) add("NPCs", n.name, text_of(n.label) + trf(" (placed, {0})", {n.template_name}), jump("NPCs", n.uid));
    for (auto& d : p.dialogs) add("Dialogues", d.name, trf("owner {0}", {d.owner}), jump("Dialogues", d.uid));
    for (auto& q : p.quests) add("Quests", q.name, text_of(q.title), jump("Quests", q.uid));
    for (auto& s : p.spells) add("Spells", s.name, s.mode == Mode::New ? trf("new, made of {0}", {s.base}) : std::string(tr("change")), jump("Spells", s.uid));
    for (size_t i = 0; i < p.npc_groups.size(); ++i) {
        auto& g = p.npc_groups[i];
        add("NPC groups", !g.group.empty() ? g.group : !g.match.empty() ? g.match : trf("group {0}", {std::to_string(i + 1)}), "", jump("NPC groups", g.uid));
    }
    for (auto& b : p.behaviors) add("Behaviours", b.name, "", jump("Behaviours", b.uid));
    for (auto& a : p.assets) add("Assets", a.file, a.kind, jump("Assets", a.uid));
    // the game's templates (the Browser)
    if (app.game) {
        for (auto& t : app.game->templates()) {
            const std::string name = t->name;
            add(std::string("Shipped ") + kind_name(t->kind), name, app.game->label(name), [name](App& a) { a.select_in("Browser", name); });
        }
    }
    return v;
}

void CommandPalette::draw(App& app) {
    if (want_open_) {
        want_open_ = false;
        open_ = true;
        refocus_ = true;
        query_ = initial_query_;
        initial_query_.clear();
        last_query_ = "\x01";
        selected_ = 0;
        all_ = collect(app);
    }
    if (!open_) return;
    if (last_query_ != query_) {
        last_query_ = query_;
        shown_ = search(all_, query_);
        selected_ = 0;
    }
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->GetCenter().x, vp->WorkPos.y + 70), ImGuiCond_Always, ImVec2(0.5f, 0));
    ImGui::SetNextWindowSize(ImVec2(std::min(760.0f, vp->WorkSize.x - 40), 0));
    ImGuiWindowFlags f = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize |
                         ImGuiWindowFlags_NoDocking;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 10));
    if (ImGui::Begin("##palette", nullptr, f)) {
        if (refocus_) {
            ImGui::SetKeyboardFocusHere();
            refocus_ = false;
        }
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##q", tr("Search objects, shipped templates, panels, commands..."), &query_);
        const int n = static_cast<int>(shown_.size());
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow) && n) selected_ = (selected_ + 1) % n;
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow) && n) selected_ = (selected_ + n - 1) % n;
        bool run = ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter);
        ImGui::BeginChild("##results", ImVec2(0, 360));
        int clicked = -1;
        for (int i = 0; i < n; ++i) {
            const Entry& e = *shown_[static_cast<size_t>(i)];
            ImGui::PushID(i);
            if (ImGui::Selectable("##row", i == selected_, ImGuiSelectableFlags_AllowOverlap)) clicked = i;
            if (i == selected_ && (ImGui::IsKeyPressed(ImGuiKey_DownArrow) || ImGui::IsKeyPressed(ImGuiKey_UpArrow))) ImGui::SetScrollHereY();
            ImGui::SameLine(0, 0);
            ImGui::TextDisabled("%-14s", kind_text(e.kind).c_str());
            ImGui::SameLine(130);
            ImGui::TextUnformatted(e.label.c_str());
            if (!e.detail.empty()) {
                ImGui::SameLine();
                ImGui::TextDisabled("  %s", e.detail.c_str());
            }
            ImGui::PopID();
        }
        if (!n) ImGui::TextDisabled("%s", query_.empty() ? tr("Type to search.") : tr("Nothing matches."));
        ImGui::EndChild();
        ImGui::TextDisabled("%s", tr("Up / Down choose, Enter opens, Esc closes"));
        if (clicked >= 0) {
            selected_ = clicked;
            run = true;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) open_ = false;
        else if (run && n) {
            const Entry* e = shown_[static_cast<size_t>(selected_)];
            auto fn = e->run;
            open_ = false;
            if (fn) app.defer([&app, fn] { fn(app); });
        }
        // a click elsewhere closes it
        if (open_ && ImGui::IsMouseClicked(0) && !ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows)) open_ = false;
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

}  // namespace mk
