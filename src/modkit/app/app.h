// The risen-modkit application: window, main loop, menus, docking layout, project file handling. Panels draw into it and edit `project`.
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "core/history.h"
#include "core/project.h"
#include "core/settings.h"

struct GLFWwindow;

namespace mk {

struct AppOptions {
    std::string project;     // open this project at start
    std::string data_dir;    // overrides the setting
    std::string screenshot;  // render `frames` frames, save the window as PNG, exit
    int frames = 10;
    std::string panel;       // focus this panel (screenshots)
    int width = 1600, height = 900;
    std::string select;      // select an object by name in its panel (screenshots)
};

class App;

class Panel {
public:
    virtual ~Panel() = default;
    virtual const char* title() const = 0;  // also the ImGui window name
    virtual void draw(App& app) = 0;
    // called when another project was loaded (drop cached selections)
    virtual void on_project_changed(App&) {}
    // select an object by name (command line --select, Problems panel jumps)
    virtual bool select(App&, const std::string&) { return false; }
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

    void touch() { history.touch(); }  // a panel changed `project`
    void status(const std::string& text);  // the status bar line
    template <class T>
    T* panel() {
        for (auto& p : panels_)
            if (auto* x = dynamic_cast<T*>(p.get())) return x;
        return nullptr;
    }
    void focus_panel(const char* title);
    void defer(std::function<void()> fn) { deferred_.push_back(std::move(fn)); }  // run after the frame (structural edits)

    // project file actions (with the unsaved-changes prompt where needed)
    void new_project();
    void open_project(const std::string& path);
    bool save_project(bool save_as);
    void request_open();  // file dialog
    void project_replaced();  // after the model was replaced (open, new, undo across structure, import)

private:
    void draw_frame();
    void draw_menu();
    void draw_status_bar();
    void draw_modals();
    void setup_default_layout(unsigned int dockspace_id);
    void apply_ui_scale();
    void handle_shortcuts();
    void update_title();

    GLFWwindow* window_ = nullptr;
    std::vector<std::unique_ptr<Panel>> panels_;
    std::vector<std::function<void()>> deferred_;
    std::string status_;
    double status_time_ = 0;
    std::function<void()> after_discard_;  // the action waiting for the "unsaved changes" answer
    bool ask_discard_ = false, want_quit_ = false, layout_done_ = false;
    std::string title_shown_;
    int frame_ = 0;
    std::string focus_request_;
};

// helpers implemented in other files
void load_fonts();
bool save_screenshot(const std::string& path, int width, int height);
// native file dialogs; empty string = cancelled
std::string dialog_open_file(const char* filter_name, const char* filter_spec, const std::string& default_dir);
std::string dialog_save_file(const char* filter_name, const char* filter_spec, const std::string& default_dir, const std::string& default_name);
std::string dialog_pick_folder(const std::string& default_dir);

}  // namespace mk
