// Live panel: the running game seen through the live link (a named pipe, core/link_client.h): the player and the NPCs around, any entity's properties and
// inventory, the state of the project's quests, game variables, the mods (with a reload button) and the game's log. Everything is read-only except "Reload".
#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <map>
#include <memory>
#include <set>

#include "app/panels/panels.h"
#include "app/widgets.h"
#include "core/gamelog.h"
#include "core/i18n.h"
#include "core/link_worker.h"

namespace mk {

namespace {

using nlohmann::json;

const char* kLogLevels[] = {"off", "error", "warn", "info", "debug"};

std::string str(const json& j, const char* key, const char* fallback = "") {
    if (!j.is_object() || !j.contains(key)) return fallback;
    const json& v = j[key];
    if (v.is_string()) return v.get<std::string>();
    if (v.is_null()) return fallback;
    return v.dump();
}

double num(const json& j, const char* key, double fallback = 0) {
    if (!j.is_object() || !j.contains(key) || !j[key].is_number()) return fallback;
    return j[key].get<double>();
}

class LivePanel : public Panel {
public:
    const char* title() const override { return kPanelLive; }

    void on_project_changed(App&) override { index_.reset(); }

    // --select NAME: inspect the entity with this name as soon as it is among the nearby ones
    bool select(App&, const std::string& name) override {
        pending_select_ = name;
        return true;
    }

    void draw(App& app) override {
        LinkWorker& link = app.link();
        link.set_game_dir(app.settings.game_dir);
        const LinkWorker::Status st = link.status();
        if (!ImGui::Begin(ui::panel_label(title()).c_str(), &open)) {
            ImGui::End();
            return;
        }
        // the connection
        const ImVec4 col = st.connected ? ImVec4(0.45f, 0.9f, 0.5f, 1) : ImVec4(0.6f, 0.6f, 0.6f, 1);
        ImGui::TextColored(col, "%s", st.connected ? "\xE2\x97\x8F" : "\xE2\x97\x8B");  // filled / empty circle
        ImGui::SameLine();
        if (st.connected) {
            ImGui::Text(tr("Connected to the game (process %d, layer %s)"), st.pid, st.layer.c_str());
        } else {
            ImGui::TextUnformatted(tr("Not connected"));
            ImGui::TextDisabled("%s", st.note.empty() ? tr("looking for a running game ...") : st.note.c_str());
            ImGui::Spacing();
            ImGui::TextWrapped("%s", tr("The game opens a named pipe for the editor when its config.toml has [link] enabled = true. Play starts the game that way; for a game you started yourself add the two lines and restart it. Only your Windows user can use the pipe, and no network socket is opened."));
        }
        if (st.connects != seen_connects_) {
            seen_connects_ = st.connects;
            reset_data();
            if (st.connected) link.set_log_level(kLogLevels[log_level_]);
        }
        pump(app);
        if (!st.connected) {
            ImGui::End();
            return;
        }
        if (ImGui::BeginTabBar("##livetabs")) {
            if (ImGui::BeginTabItem(tr("Game"))) {
                draw_game(app);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem(tr("Quests and variables"))) {
                draw_quests(app);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem(tr("Mods"))) {
                draw_mods(app);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem(tr("Game log"))) {
                draw_log(app);
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        ImGui::End();
    }

private:
    // ---- data ----
    json state_, nearby_, entity_, inventory_, quests_, vars_, mods_;
    std::map<std::string, json> props_;  // property set -> reply
    std::string selected_;               // entity id
    std::string reload_message_;
    std::set<std::string> watched_;      // game variables the user asked for
    std::vector<LinkWorker::LogLine> log_;
    std::map<std::string, double> last_ask_;
    unsigned long long seen_connects_ = 0;
    float radius_ = 3000;
    int log_level_ = 2;
    ImGuiTextFilter log_filter_, prop_filter_;
    std::unique_ptr<ProjectNameIndex> index_;
    char var_input_[96] = "";
    std::string pending_select_;

    void reset_data() {
        state_ = nearby_ = entity_ = inventory_ = quests_ = vars_ = mods_ = json();
        props_.clear();
        last_ask_.clear();
        reload_message_.clear();
    }

    void ask(App& app, const std::string& tag, const std::string& cmd, const json& args, double period) {
        const double now = ImGui::GetTime();
        auto it = last_ask_.find(tag);
        if (it != last_ask_.end() && now - it->second < period) return;
        last_ask_[tag] = now;
        app.link().request(tag, cmd, args);
    }

    void pump(App& app) {
        for (LinkWorker::Result& r : app.link().take_results()) {
            const json& res = r.reply.ok ? r.reply.result : json();
            if (r.tag == "state") state_ = res;
            else if (r.tag == "nearby") nearby_ = res;
            else if (r.tag == "entity") entity_ = r.reply.ok ? res : json{{"error", r.reply.error}};
            else if (r.tag == "inventory") inventory_ = res;
            else if (r.tag == "quests") quests_ = res;
            else if (r.tag == "vars") vars_ = res;
            else if (r.tag == "mods") mods_ = res;
            else if (r.tag.rfind("props:", 0) == 0) props_[r.tag.substr(6)] = r.reply.ok ? res : json{{"error", r.reply.error}};
            else if (r.tag == "reload") {
                reload_message_ = r.reply.ok ? tr("Reloaded.") : (r.reply.error.empty() ? tr("The game did not answer.") : r.reply.error);
                const json& reports = r.reply.result.is_object() ? r.reply.result["reports"] : json();
                if (reports.is_array() && !reports.empty() && reports[0].contains("actions") && !reports[0]["actions"].empty()) {
                    const json& a = reports[0]["actions"][0];
                    const std::string err = str(a, "error");
                    reload_message_ = str(a, "id") + ": " + str(a, "action") + (err.empty() ? "" : " - " + err);
                }
                last_ask_.erase("mods");
            }
        }
        for (LinkWorker::LogLine& l : app.link().take_log()) {
            log_.push_back(std::move(l));
        }
        if (log_.size() > 20000) log_.erase(log_.begin(), log_.begin() + 10000);
    }

    const ProjectNameIndex& names(App& app) {
        if (!index_) index_ = std::make_unique<ProjectNameIndex>(app.project);
        return *index_;
    }

    // ---- the Game tab ----
    void draw_game(App& app) {
        ask(app, "state", "inspect.state", json::object(), 1.0);
        ask(app, "nearby", "inspect.nearby", {{"radius", radius_}, {"limit", 120}}, 1.5);
        if (!state_.is_object()) {
            ImGui::TextDisabled("%s", tr("waiting for the game ..."));
            return;
        }
        if (!state_.value("running", false)) {
            ImGui::TextWrapped("%s", tr("No game is running yet (the main menu or loading). Start a new game or load a savegame."));
            return;
        }
        const json& p = state_["player"];
        const double hour = num(state_, "hour"), minute = num(state_, "minute");
        ImGui::Text(tr("World %s, chapter %d, time %02d:%02d"), str(state_, "world").c_str(), static_cast<int>(num(state_, "chapter")), static_cast<int>(hour), static_cast<int>(minute));
        if (p.is_object()) {
            ImGui::Text(tr("Player: level %d, position %.0f %.0f %.0f"), static_cast<int>(num(p, "level")), num(p, "x"), num(p, "y"), num(p, "z"));
            ImGui::SameLine();
            if (ImGui::SmallButton(tr("Copy position"))) {
                char buf[96];
                std::snprintf(buf, sizeof buf, "%.0f, %.0f, %.0f", num(p, "x"), num(p, "y"), num(p, "z"));
                ImGui::SetClipboardText(buf);
            }
            bars(p);
        }
        ImGui::Separator();
        ImGui::SetNextItemWidth(220);
        ImGui::SliderFloat(tr("Radius"), &radius_, 500, 20000, "%.0f", ImGuiSliderFlags_Logarithmic);
        ImGui::SameLine();
        const json& ents = nearby_.is_object() ? nearby_["entities"] : json();
        ImGui::TextDisabled(tr("%zu around you"), ents.is_array() ? ents.size() : size_t(0));

        const float total = ImGui::GetContentRegionAvail().y;
        if (ImGui::BeginTable("##nearby", 7, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable, ImVec2(0, std::max(120.0f, total * 0.4f)))) {
            ImGui::TableSetupColumn(tr("Name"), ImGuiTableColumnFlags_WidthStretch, 2.0f);
            ImGui::TableSetupColumn(tr("Template"), ImGuiTableColumnFlags_WidthStretch, 2.0f);
            ImGui::TableSetupColumn(tr("Lv"), ImGuiTableColumnFlags_WidthFixed, 34.0f);
            ImGui::TableSetupColumn(tr("HP"), ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableSetupColumn(tr("Distance"), ImGuiTableColumnFlags_WidthFixed, 70.0f);
            ImGui::TableSetupColumn(tr("State"), ImGuiTableColumnFlags_WidthStretch, 1.5f);
            ImGui::TableSetupColumn(tr("Mod"), ImGuiTableColumnFlags_WidthFixed, 50.0f);
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableHeadersRow();
            if (ents.is_array()) {
                for (const json& e : ents) {
                    if (!pending_select_.empty() && str(e, "name") == pending_select_) {
                        select_entity(app, str(e, "id"));
                        pending_select_.clear();
                    }
                    const std::string id = str(e, "id");
                    ImGui::PushID(id.c_str());
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    const bool dead = e.value("dead", false);
                    if (dead) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1));
                    if (ImGui::Selectable(str(e, "name").c_str(), selected_ == id, ImGuiSelectableFlags_SpanAllColumns)) select_entity(app, id);
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(str(e, "template").c_str());
                    ImGui::TableNextColumn();
                    ImGui::Text("%d", static_cast<int>(num(e, "level")));
                    ImGui::TableNextColumn();
                    ImGui::Text("%d/%d", static_cast<int>(num(e, "hp")), static_cast<int>(num(e, "hp_max")));
                    ImGui::TableNextColumn();
                    ImGui::Text("%.0f", num(e, "distance"));
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(str(e, "state").c_str());
                    ImGui::TableNextColumn();
                    if (names(app).find(str(e, "name"))) ImGui::TextColored(ImVec4(1, 0.4f, 0.9f, 1), "\xE2\x97\x8F");  // an NPC of this project
                    if (dead) ImGui::PopStyleColor();
                    ImGui::PopID();
                }
            }
            ImGui::EndTable();
        }
        if (!selected_.empty()) draw_inspector(app);
    }

    static void bars(const json& p) {
        const double hp = num(p, "hp"), hpm = std::max(1.0, num(p, "hp_max")), mana = num(p, "mana"), manam = std::max(1.0, num(p, "mana_max"));
        char t[48];
        std::snprintf(t, sizeof t, "%.0f / %.0f", hp, hpm);
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.18f, 0.62f, 0.25f, 1));  // health: green
        ImGui::ProgressBar(static_cast<float>(hp / hpm), ImVec2(160, 0), t);
        ImGui::PopStyleColor();
        ImGui::SameLine();
        std::snprintf(t, sizeof t, "%.0f / %.0f", mana, manam);
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.2f, 0.4f, 0.85f, 1));  // mana: blue
        ImGui::ProgressBar(static_cast<float>(mana / manam), ImVec2(160, 0), t);
        ImGui::PopStyleColor();
    }

    void select_entity(App& app, const std::string& id) {
        selected_ = id;
        entity_ = inventory_ = json();
        props_.clear();
        last_ask_.erase("entity");
        last_ask_.erase("inventory");
        (void)app;
    }

    void draw_inspector(App& app) {
        ImGui::Separator();
        ask(app, "entity", "inspect.entity", {{"id", selected_}}, 1.0);
        if (!entity_.is_object()) {
            ImGui::TextDisabled("%s", tr("waiting for the game ..."));
            return;
        }
        if (entity_.contains("error")) {
            ImGui::TextColored(ImVec4(1, 0.6f, 0.4f, 1), "%s", str(entity_, "error").c_str());
            return;
        }
        ImGui::BeginChild("##inspector", ImVec2(0, 0), ImGuiChildFlags_None);
        ImGui::Text("%s", str(entity_, "name").c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("(%s) %s", str(entity_, "template").c_str(), str(entity_, "id").c_str());
        if (const GameLogRef* ref = names(app).find(str(entity_, "name"))) {
            ImGui::SameLine();
            if (ImGui::SmallButton(tr("Open in the project"))) {
                const std::string area = ref->area, uid = ref->uid;
                app.defer([&app, area, uid] { app.select_in(area, uid); });
            }
        }
        ImGui::Text(tr("Level %d, guild %d, group '%s', state %s%s"), static_cast<int>(num(entity_, "level")), static_cast<int>(num(entity_, "guild")), str(entity_, "group").c_str(),
                    str(entity_, "state").c_str(), entity_.value("dead", false) ? tr(" (dead)") : "");
        ImGui::Text(tr("Position %.0f %.0f %.0f, distance %.0f"), num(entity_, "x"), num(entity_, "y"), num(entity_, "z"), num(entity_, "distance"));
        if (ImGui::TreeNode(tr("Inventory"))) {
            ask(app, "inventory", "inspect.inventory", {{"id", selected_}}, 2.0);
            if (inventory_.is_object()) {
                ImGui::Text(tr("%d stacks, gold %d"), static_cast<int>(num(inventory_, "count")), static_cast<int>(num(inventory_, "gold")));
                for (const json& s : inventory_["stacks"]) ImGui::BulletText("%s x%d", str(s, "template").c_str(), static_cast<int>(num(s, "amount")));
            }
            ImGui::TreePop();
        }
        prop_filter_.Draw(tr("Filter properties"), 200);
        for (const json& set : entity_["sets"]) {
            const std::string name = str(set, "name");
            char label[96];
            std::snprintf(label, sizeof label, "%s (%d)###%s", name.c_str(), static_cast<int>(num(set, "count")), name.c_str());
            if (name == "gCNPC_PS") ImGui::SetNextItemOpen(true, ImGuiCond_Once);  // the interesting one first
            if (ImGui::TreeNode(label)) {
                ask(app, "props:" + name, "inspect.props", {{"id", selected_}, {"set", name}}, 2.0);
                const auto it = props_.find(name);
                if (it != props_.end() && it->second.contains("properties")) {
                    if (ImGui::BeginTable("##props", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable)) {
                        ImGui::TableSetupColumn(tr("Property"), ImGuiTableColumnFlags_WidthStretch, 2.0f);
                        ImGui::TableSetupColumn(tr("Type"), ImGuiTableColumnFlags_WidthStretch, 2.0f);
                        ImGui::TableSetupColumn(tr("Value"), ImGuiTableColumnFlags_WidthStretch, 3.0f);
                        for (const json& pr : it->second["properties"]) {
                            const std::string pn = str(pr, "name");
                            if (!prop_filter_.PassFilter(pn.c_str())) continue;
                            ImGui::TableNextRow();
                            ImGui::TableNextColumn();
                            ImGui::TextUnformatted(pn.c_str());
                            ImGui::TableNextColumn();
                            ImGui::TextDisabled("%s", str(pr, "type").c_str());
                            ImGui::TableNextColumn();
                            ImGui::TextUnformatted(str(pr, "value").c_str());
                        }
                        ImGui::EndTable();
                    }
                } else if (it != props_.end()) {
                    ImGui::TextDisabled("%s", str(it->second, "error").c_str());
                }
                ImGui::TreePop();
            }
        }
        ImGui::EndChild();
    }

    // ---- quests and variables ----
    void draw_quests(App& app) {
        std::vector<std::string> names_list;
        for (const auto& q : app.project.quests) names_list.push_back(q.name);
        ImGui::SeparatorText(tr("Quests of this project"));
        if (names_list.empty()) {
            ImGui::TextDisabled("%s", tr("The project has no quests."));
        } else {
            ask(app, "quests", "inspect.quests", {{"names", names_list}}, 2.0);
            if (ImGui::BeginTable("##quests", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable)) {
                ImGui::TableSetupColumn(tr("Quest"), ImGuiTableColumnFlags_WidthStretch, 3.0f);
                ImGui::TableSetupColumn(tr("Known to the game"), ImGuiTableColumnFlags_WidthFixed, 130.0f);
                ImGui::TableSetupColumn(tr("Running"), ImGuiTableColumnFlags_WidthFixed, 70.0f);
                ImGui::TableSetupColumn(tr("Status"), ImGuiTableColumnFlags_WidthStretch, 2.0f);
                ImGui::TableHeadersRow();
                if (quests_.is_object()) {
                    for (const json& q : quests_["quests"]) {
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        const std::string qn = str(q, "name");
                        if (ImGui::Selectable(qn.c_str(), false, ImGuiSelectableFlags_SpanAllColumns)) {
                            std::string uid;
                            for (const auto& pq : app.project.quests) {
                                if (pq.name == qn) uid = pq.uid;
                            }
                            app.defer([&app, uid] { app.select_in(kPanelQuests, uid); });
                        }
                        ImGui::TableNextColumn();
                        ImGui::TextUnformatted(q.value("exists", false) ? tr("yes") : tr("no"));
                        ImGui::TableNextColumn();
                        ImGui::TextColored(q.value("running", false) ? ImVec4(0.45f, 0.9f, 0.5f, 1) : ImVec4(0.6f, 0.6f, 0.6f, 1), "%s", q.value("running", false) ? tr("running") : "-");
                        ImGui::TableNextColumn();
                        ImGui::TextUnformatted(str(q, "status").c_str());
                    }
                }
                ImGui::EndTable();
            }
        }
        ImGui::SeparatorText(tr("Game variables"));
        ImGui::SetNextItemWidth(260);
        const bool add = ImGui::InputTextWithHint("##var", tr("variable name, Enter adds it"), var_input_, sizeof var_input_, ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        if ((ImGui::Button(tr("Watch")) || add) && var_input_[0] != 0) {
            watched_.insert(var_input_);
            var_input_[0] = 0;
            last_ask_.erase("vars");
        }
        if (!watched_.empty()) {
            ask(app, "vars", "inspect.gamevars", {{"names", json(watched_)}}, 1.0);
            if (ImGui::BeginTable("##vars", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable)) {
                ImGui::TableSetupColumn(tr("Variable"), ImGuiTableColumnFlags_WidthStretch, 3.0f);
                ImGui::TableSetupColumn(tr("Value"), ImGuiTableColumnFlags_WidthFixed, 100.0f);
                ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 30.0f);
                std::string remove;
                if (vars_.is_object()) {
                    for (const json& v : vars_["vars"]) {
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        const std::string vn = str(v, "name");
                        ImGui::TextUnformatted(vn.c_str());
                        ImGui::TableNextColumn();
                        if (v.value("exists", false)) ImGui::Text("%d", static_cast<int>(num(v, "value")));
                        else ImGui::TextDisabled("%s", tr("not set"));
                        ImGui::TableNextColumn();
                        ImGui::PushID(vn.c_str());
                        if (ImGui::SmallButton("x")) remove = vn;
                        ImGui::PopID();
                    }
                }
                ImGui::EndTable();
                if (!remove.empty()) {
                    watched_.erase(remove);
                    last_ask_.erase("vars");
                }
            }
        } else {
            ImGui::TextDisabled("%s", tr("Add the names of the variables your dialogues and events set (SetGameEvent, game variable conditions)."));
        }
    }

    // ---- mods ----
    void draw_mods(App& app) {
        ask(app, "mods", "mods", json::object(), 3.0);
        if (ImGui::Button(tr("Reload all"))) app.link().request("reload", "reload", {{"mod", "all"}}, 20000);
        if (!reload_message_.empty()) {
            ImGui::SameLine();
            ImGui::TextUnformatted(reload_message_.c_str());
        }
        if (!mods_.is_object()) return;
        if (ImGui::BeginTable("##mods", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable)) {
            ImGui::TableSetupColumn(tr("Mod"), ImGuiTableColumnFlags_WidthStretch, 3.0f);
            ImGui::TableSetupColumn(tr("Version"), ImGuiTableColumnFlags_WidthFixed, 70.0f);
            ImGui::TableSetupColumn(tr("State"), ImGuiTableColumnFlags_WidthStretch, 3.0f);
            ImGui::TableSetupColumn(tr("Network"), ImGuiTableColumnFlags_WidthFixed, 70.0f);
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 70.0f);
            ImGui::TableHeadersRow();
            for (const json& m : mods_["mods"]) {
                const std::string id = str(m, "id"), state = str(m, "state");
                ImGui::PushID(id.c_str());
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                const bool mine = id == app.project.mod.id;
                ImGui::TextColored(mine ? ImVec4(1, 0.4f, 0.9f, 1) : ImVec4(0.9f, 0.9f, 0.9f, 1), "%s", id.c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(str(m, "version").c_str());
                ImGui::TableNextColumn();
                const bool bad = state == "failed" || state == "skipped";
                ImGui::TextColored(bad ? ImVec4(1, 0.45f, 0.4f, 1) : ImVec4(0.9f, 0.9f, 0.9f, 1), "%s%s%s", state.c_str(), str(m, "reason").empty() ? "" : ": ", str(m, "reason").c_str());
                ImGui::TableNextColumn();
                const json& net = m["network"];
                ImGui::TextUnformatted(net.is_object() && net.value("declared", false) ? tr("yes") : "-");
                ImGui::TableNextColumn();
                if ((state == "loaded" || state == "failed") && m.value("reloadable", true) && ImGui::SmallButton(tr("Reload"))) app.link().request("reload", "reload", {{"mod", id}}, 20000);
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }

    // ---- the game's log ----
    void draw_log(App& app) {
        ImGui::SetNextItemWidth(110);
        if (ImGui::Combo(tr("Level"), &log_level_, "off\0error\0warn\0info\0debug\0")) app.link().set_log_level(kLogLevels[log_level_]);
        ImGui::SameLine();
        if (ImGui::Button(tr("Clear"))) log_.clear();
        ImGui::SameLine();
        log_filter_.Draw(tr("Filter"), 220);
        ImGui::Separator();
        ImGui::BeginChild("##gamelog", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
        const bool at_bottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 2;
        int i = 0;
        for (const LinkWorker::LogLine& l : log_) {
            if (!log_filter_.PassFilter(l.text.c_str())) continue;
            const bool err = l.level == "error" || l.level == "critical", warn = l.level == "warning" || l.level == "warn";
            const ImVec4 c = err ? ImVec4(1, 0.45f, 0.4f, 1) : warn ? ImVec4(1, 0.8f, 0.35f, 1) : l.level == "debug" || l.level == "trace" ? ImVec4(0.6f, 0.6f, 0.6f, 1) : ImVec4(0.9f, 0.9f, 0.9f, 1);
            ImGui::PushID(i++);
            ImGui::PushStyleColor(ImGuiCol_Text, c);
            ImGui::TextUnformatted(l.text.c_str());
            ImGui::PopStyleColor();
            if (err || warn) {
                if (const GameLogRef* ref = names(app).find(l.text)) {
                    ImGui::SameLine();
                    if (ImGui::SmallButton(tr("show"))) {
                        const std::string area = ref->area, uid = ref->uid;
                        app.defer([&app, area, uid] { app.select_in(area, uid); });
                    }
                }
            }
            ImGui::PopID();
        }
        if (at_bottom) ImGui::SetScrollHereY(1.0f);
        ImGui::EndChild();
    }
};

}  // namespace

std::unique_ptr<Panel> make_live_panel() { return std::make_unique<LivePanel>(); }

}  // namespace mk
