// The risen-modkit application: window, main loop, menus, docking layout, project file handling. Panels draw into it and edit `project`.
#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <mutex>
#include <thread>
#include <string>
#include <vector>

#include "core/assets.h"
#include "core/gamedata.h"
#include "core/gamelog.h"
#include "core/history.h"
#include "core/project.h"
#include "core/settings.h"
#include "core/validate.h"
#include "core/world.h"

struct GLFWwindow;

namespace mk {

namespace gfx {
class ResourceCache;
}
class AudioPlayer;
class CommandPalette;
class WizardUi;

enum class WizardKind { None, FetchQuest, Weapon, Trader };  // app/wizards.h

struct AppOptions {
    std::string project;     // open this project at start
    std::string data_dir;    // overrides the setting
    std::string game_dir;    // overrides the Play setting (the Risen folder)
    bool play = false;       // --play: Play once the game data is loaded (a test of the Play button)
    std::string palette;     // --palette TEXT: open the command palette with this query (screenshots)
    std::string wizard;      // --wizard fetch|weapon|trader: open that wizard (screenshots)
    std::string screenshot;  // render `frames` frames, save the window as PNG, exit
    int frames = 10;
    std::string panel;       // focus this panel (screenshots)
    std::string language;    // --lang CODE: the UI language for this run, overrides the setting without saving it
    int width = 1600, height = 900;
    std::string select;      // select an object by name in its panel (screenshots)
    std::string camera;      // the World view's camera: "x,y,z,yaw,pitch" in game coordinates and degrees (screenshots)
    bool collapse_side = false;  // --collapse-side: start with the left column folded into vertical tabs (screenshots)
    // left clicks and drags at window coordinates, one after the other once the app is idle (UI tests)
    struct Input {
        float x, y, x2 = 0, y2 = 0;
        bool drag = false;  // press at x,y, move to x2,y2, release
    };
    std::vector<Input> clicks;
};

class App;

// an action the Browser offers for a game template ("New item based on this", "Edit this item"); registered by the editor panels
struct TemplateAction {
    std::string label;
    std::vector<TplKind> kinds;  // empty = every kind
    std::function<void(App&, const TemplateInfo&)> run;
};

class Panel {
public:
    virtual ~Panel() = default;
    virtual const char* title() const = 0;  // also the ImGui window name
    virtual void draw(App& app) = 0;
    // called when another project was loaded (drop cached selections)
    virtual void on_project_changed(App&) {}
    // select an object by name or uid (command line --select, Problems panel jumps)
    virtual bool select(App&, const std::string&) { return false; }
    // called once after all panels exist (register template actions, ...)
    virtual void attach(App&) {}
    // still loading something (screenshots wait for it)
    virtual bool busy() const { return false; }
    bool open = true;
};

class App {
public:
    explicit App(AppOptions opts);
    ~App();
    int run();

    // ---- state the panels use ----
    Project project;
    std::string project_path;  // empty: not saved yet
    History history;
    Settings settings;
    AppOptions options;

    // the game's data (nullptr until loaded; loading runs on a worker thread, started at start-up and when the data directory changes)
    std::shared_ptr<const GameData> game;
    void load_game_data();
    bool game_loading() const { return loader_.joinable(); }

    std::vector<TemplateAction> template_actions;
    // the loaded game world (the World panel loads it; placement and the pickers of NPC names use it)
    std::shared_ptr<const World> world;
    // starts loading the world in the background when nothing has (the pickers of NPC names and entity IDs need it without the World panel having been opened);
    // `world` is set when it is done. No effect while a load runs or the world is there.
    void request_world();
    bool world_loading() const { return world_future_.valid(); }
    // meshes and textures of the game for the 3D views (made with the game data; nullptr without)
    gfx::ResourceCache* resources();
    // tells the 3D views about the project's asset files (every frame; `reload` reads files that changed on disk again)
    void refresh_assets(bool reload = false);
    // an asset file of the project on disk (relative paths are relative to the project file)
    std::string asset_path(const AssetFile& a) const;
    // any file of the project (behaviour scripts, ...) on disk: relative paths are relative to the project file
    std::string project_file(const std::string& file) const;
    // a sound reference (core/assets.h: a project audio file or a game sound) read and unpacked; nullopt + `error` when it cannot be
    std::optional<SoundData> load_sound(const std::string& ref, std::string& error) const;
    // the sound preview: play / stop a reference (errors go to the status bar)
    AudioPlayer& audio();
    void toggle_sound(const std::string& ref);

    void touch() { history.touch(); }  // a panel changed `project`
    void save_settings() const;         // writes `settings` (not in a screenshot / UI test run)
    void status(const std::string& text);  // the status bar line
    template <class T>
    T* panel() {
        for (auto& p : panels_)
            if (auto* x = dynamic_cast<T*>(p.get())) return x;
        return nullptr;
    }
    void focus_panel(const char* title);
    // select an object in the panel with this title (Problems panel)
    void select_in(const std::string& panel_title, const std::string& uid_or_name);
    // the validation result of the current project (refreshed twice a second)
    const std::vector<Problem>& problems() const { return problems_; }
    // problems the running game reported for this mod (spass.log, tailed while a game folder is set), linked to project objects; not part of
    // problems(): they never block the export
    const std::vector<Problem>& game_problems() const { return game_problems_; }
    // Play: export into <game folder>'s bin\spass\mods, then start the game windowed or let a running one hot-reload the mod
    void play();
    void request_export() { export_open_ = true; }
    void open_palette();                  // Ctrl+P
    void open_wizard(WizardKind kind);    // the Wizards menu
    std::vector<std::string> panel_titles() const;  // every panel, for the command palette
    void defer(std::function<void()> fn) { deferred_.push_back(std::move(fn)); }  // run after the frame (structural edits)

    // project file actions (with the unsaved-changes prompt where needed)
    void new_project();
    void open_project(const std::string& path);
    bool save_project(bool save_as);
    void request_open();  // file dialog
    void set_language(const std::string& code);  // Settings > Interface: saves the choice, switches after the frame
    void import_mod_folder(const std::string& dir, bool prefer_data);  // File > Import mod
    void project_replaced();  // after the model was replaced (open, new, undo across structure, import)

private:
    void draw_frame();
    void draw_menu();
    void draw_status_bar();
    void draw_modals();
    void draw_export_modal();
    void refresh_problems();
    void poll_game_log();
    bool any_busy() const;
    void setup_default_layout(unsigned int dockspace_id);
    void apply_ui_scale();
    void apply_language(const std::string& code);  // activates a language file; call between frames (defer)
    void handle_shortcuts();
    void update_title();
    // the side bar: the panels docked in the left column fold into vertical tabs at the window's left edge (Ctrl+B)
    void set_side_collapsed(bool collapsed);
    std::vector<std::string> left_column_panels() const;
    float draw_side_bar();  // the strip's width (0 = expanded)

    GLFWwindow* window_ = nullptr;
    std::vector<std::unique_ptr<Panel>> panels_;
    std::vector<std::function<void()>> deferred_;
    std::string status_;
    double status_time_ = 0;
    std::function<void()> after_discard_;  // the action waiting for the "unsaved changes" answer
    bool ask_discard_ = false, want_quit_ = false, layout_done_ = false;
    std::string title_shown_;
    int frame_ = 0, shot_frames_ = 0;
    std::string focus_request_, pending_select_;
    unsigned int dock_id_ = 0;
    bool side_collapsed_ = false, side_pending_ = false;  // pending: collapse once the panels are docked (start-up)
    std::vector<std::string> side_panels_;                // the titles folded into the side bar
    std::vector<Problem> problems_;
    std::unique_ptr<gfx::ResourceCache> resources_;
    std::unique_ptr<AudioPlayer> audio_;
    std::string assets_sig_;
    const gfx::ResourceCache* assets_res_ = nullptr;
    const GameData* resources_game_ = nullptr;
    double problems_time_ = -1;
    std::unique_ptr<CommandPalette> palette_;
    std::unique_ptr<WizardUi> wizards_;
    LogTail game_log_;
    std::vector<GameLogLine> game_log_lines_;
    std::vector<Problem> game_problems_;
    double game_log_time_ = -1;
    bool game_problems_dirty_ = false;
    bool export_open_ = false;
    std::string export_dir_;
    std::vector<std::string> export_report_;
    std::vector<std::string> import_report_;
    bool import_open_ = false;
    // game data loader
    void poll_loader();
    std::future<std::shared_ptr<World>> world_future_;
    std::thread loader_;
    std::atomic<bool> loader_done_{false}, loader_cancel_{false};
    std::mutex loader_mu_;
    std::shared_ptr<GameData> loaded_;
    std::string loader_error_, loader_what_;
    float loader_fraction_ = 0;
};

// helpers implemented in other files
void load_fonts();
bool save_screenshot(const std::string& path, int width, int height);
// native file dialogs; empty string = cancelled
std::string dialog_open_file(const char* filter_name, const char* filter_spec, const std::string& default_dir);
std::string dialog_save_file(const char* filter_name, const char* filter_spec, const std::string& default_dir, const std::string& default_name);
std::string dialog_pick_folder(const std::string& default_dir);
std::vector<std::string> dialog_open_files(const char* filter_name, const char* filter_spec, const std::string& default_dir);
// opens a file with the program the system uses for it (the "edit" action first: a script opens in the editor, not the interpreter); false on failure
bool open_in_os(const std::string& path);

}  // namespace mk
