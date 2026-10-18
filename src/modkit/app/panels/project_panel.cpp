// "Mod" panel: the mod's identity (mod.toml) and an overview of the project's content.
#include <imgui.h>
#include <imgui_stdlib.h>

#include "app/panels/panels.h"
#include "app/widgets.h"

namespace mk {

namespace {

bool valid_id(const std::string& id) {
    if (id.empty()) return false;
    for (unsigned char c : id)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
    return true;
}

class ProjectPanel : public Panel {
public:
    const char* title() const override { return kPanelMod; }
    void draw(App& app) override {
        if (!ImGui::Begin(title(), &open)) {
            ImGui::End();
            return;
        }
        ModInfo& m = app.project.mod;
        bool ch = false;
        ui::section("Mod");
        ch |= ui::input_text("Id", m.id);
        ui::help("The mod's folder name and id: a-z, 0-9 and _. It names the save data and the log lines of the mod; keep it stable once released.");
        if (!valid_id(m.id)) ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), "only a-z, 0-9 and _ are allowed");
        ch |= ui::input_text("Name", m.name);
        ch |= ui::input_text("Version", m.version);
        ch |= ui::input_text("Author", m.author);
        ImGui::TextUnformatted("Description");
        ch |= ui::input_multiline("##desc", m.description, 3);
        ui::section("Languages");
        ImGui::TextWrapped("Texts are entered per language; the first language is used for every language without its own text.");
        ch |= ui::string_list("##langs", m.languages);
        if (m.languages.empty()) {
            m.languages = {"English"};
            ch = true;
        }
        ui::section("Dependencies");
        ImGui::TextUnformatted("Requires (one mod id per line)");
        ch |= ui::string_list("##requires", m.requires_mods);
        ImGui::TextUnformatted("Load after (one mod id per line)");
        ch |= ui::string_list("##load_after", m.load_after);
        if (ch) app.touch();

        ui::section("Content");
        const Project& p = app.project;
        ImGui::BulletText("%zu items, %zu item groups", p.items.size(), p.item_groups.size());
        ImGui::BulletText("%zu recipes", p.recipes.size());
        ImGui::BulletText("%zu spells, %zu NPC groups", p.spells.size(), p.npc_groups.size());
        ImGui::BulletText("%zu NPC templates, %zu placed NPCs", p.npc_templates.size(), p.npcs.size());
        ImGui::BulletText("%zu dialogues, %zu quests", p.dialogs.size(), p.quests.size());
        ImGui::BulletText("%zu template patches, %zu string rows, %zu shipped dialogue edits, %zu teacher limits", p.patches.size(), p.strings.size(), p.dialog_edits.size(),
                          p.teachers.size());
        ImGui::BulletText("%zu world spawns, %zu containers, %zu removals, %zu moves", p.world.spawns.size(), p.world.containers.size(), p.world.removals.size(),
                          p.world.moves.size());
        ImGui::BulletText("%zu asset files", p.assets.size());
        ImGui::End();
    }
};

}  // namespace

std::unique_ptr<Panel> make_project_panel() { return std::make_unique<ProjectPanel>(); }

}  // namespace mk
