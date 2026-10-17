#include "app/app.h"

#include <glad/glad.h>
// glad first: GLFW must not pull in the system GL header
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>
#include <nfd.h>

#include <algorithm>

#include "app/panels/panels.h"
#include "core/export.h"
#include "core/log.h"
#include "core/paths.h"

namespace mk {

namespace {

constexpr const char* kProjectFilter = "rmk.json";

void glfw_error(int code, const char* text) { log_error("glfw " + std::to_string(code) + ": " + text); }

std::string base_name(const std::string& path) { return u8str(u8(path).filename()); }

}  // namespace

// ---- file dialogs ---------------------------------------------------------------------------------------------------------------------------------------
std::string dialog_open_file(const char* filter_name, const char* filter_spec, const std::string& default_dir) {
    nfdu8char_t* out = nullptr;
    nfdu8filteritem_t filter{filter_name, filter_spec};
    const nfdresult_t r = NFD_OpenDialogU8(&out, filter_spec ? &filter : nullptr, filter_spec ? 1 : 0, default_dir.empty() ? nullptr : default_dir.c_str());
    if (r == NFD_ERROR) log_error(std::string("file dialog: ") + NFD_GetError());
    if (r != NFD_OKAY) return {};
    std::string s = out;
    NFD_FreePathU8(out);
    return s;
}

std::string dialog_save_file(const char* filter_name, const char* filter_spec, const std::string& default_dir, const std::string& default_name) {
    nfdu8char_t* out = nullptr;
    nfdu8filteritem_t filter{filter_name, filter_spec};
    const nfdresult_t r = NFD_SaveDialogU8(&out, filter_spec ? &filter : nullptr, filter_spec ? 1 : 0, default_dir.empty() ? nullptr : default_dir.c_str(),
                                           default_name.empty() ? nullptr : default_name.c_str());
    if (r == NFD_ERROR) log_error(std::string("file dialog: ") + NFD_GetError());
    if (r != NFD_OKAY) return {};
    std::string s = out;
    NFD_FreePathU8(out);
    return s;
}

std::string dialog_pick_folder(const std::string& default_dir) {
    nfdu8char_t* out = nullptr;
    const nfdresult_t r = NFD_PickFolderU8(&out, default_dir.empty() ? nullptr : default_dir.c_str());
    if (r == NFD_ERROR) log_error(std::string("folder dialog: ") + NFD_GetError());
    if (r != NFD_OKAY) return {};
    std::string s = out;
    NFD_FreePathU8(out);
    return s;
}

// ---- App ----------------------------------------------------------------------------------------------------------------------------------------------
App::App(AppOptions opts) : options(std::move(opts)) {
    settings = Settings::load();
    if (!options.data_dir.empty()) settings.data_dir = options.data_dir;
    panels_.push_back(make_project_panel());
    panels_.push_back(make_browser_panel());
    panels_.push_back(make_items_panel());
    panels_.push_back(make_recipes_panel());
    panels_.push_back(make_problems_panel());
    panels_.push_back(make_log_panel());
    panels_.push_back(make_settings_panel());
    for (auto& p : panels_) p->attach(*this);
    history.reset(project);
}

App::~App() {
    if (loader_.joinable()) {
        loader_cancel_ = true;
        loader_.join();
    }
}

void App::load_game_data() {
    if (loader_.joinable()) {
        loader_cancel_ = true;
        loader_.join();
    }
    loader_cancel_ = false;
    loader_done_ = false;
    loaded_.reset();
    loader_error_.clear();
    if (settings.data_dir.empty()) return;
    const std::string dir = settings.data_dir;
    loader_ = std::thread([this, dir] {
        try {
            auto gd = GameData::load(
                dir,
                [this](float f, const std::string& what) {
                    std::lock_guard<std::mutex> lock(loader_mu_);
                    loader_fraction_ = f;
                    loader_what_ = what;
                },
                &loader_cancel_);
            std::lock_guard<std::mutex> lock(loader_mu_);
            loaded_ = gd;
        } catch (const std::exception& e) {
            std::lock_guard<std::mutex> lock(loader_mu_);
            loader_error_ = e.what();
        }
        loader_done_ = true;
    });
}

void App::poll_loader() {
    if (!loader_.joinable() || !loader_done_) return;
    loader_.join();
    std::lock_guard<std::mutex> lock(loader_mu_);
    if (loaded_) {
        game = loaded_;
        loaded_.reset();
        status("Game data: " + std::to_string(game->templates().size()) + " templates from " + std::to_string(game->archive_count()) + " archives");
        for (auto& p : panels_) p->on_project_changed(*this);
    } else if (!loader_error_.empty()) {
        status("Cannot read the game data: " + loader_error_);
    }
}

void App::status(const std::string& text) {
    status_ = text;
    status_time_ = glfwGetTime();
    log_info(text);
}

void App::focus_panel(const char* title) { focus_request_ = title; }

void App::select_in(const std::string& panel_title, const std::string& uid_or_name) {
    for (auto& p : panels_)
        if (panel_title == p->title()) {
            p->open = true;
            if (!uid_or_name.empty()) p->select(*this, uid_or_name);
            focus_panel(p->title());
        }
}

void App::refresh_problems() {
    const double now = glfwGetTime();
    if (problems_time_ >= 0 && now - problems_time_ < 0.5) return;
    problems_time_ = now;
    problems_ = validate(project, game.get());
}

void App::draw_export_modal() {
    if (export_open_) {
        ImGui::OpenPopup("Export mod");
        export_open_ = false;
        export_report_.clear();
        if (export_dir_.empty() || u8str(u8(export_dir_).filename()) != project.mod.id)
            export_dir_ = settings.export_dir.empty() ? std::string() : u8str(u8(settings.export_dir) / u8(project.mod.id));
    }
    ImGui::SetNextWindowSize(ImVec2(720, 0), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("Export mod", nullptr)) return;
    ImGui::TextWrapped("The mod is written into this folder (normally <Risen>\\bin\\spass\\mods\\<id>). The export only writes into an empty folder or one it wrote before.");
    ImGui::SetNextItemWidth(-90);
    ImGui::InputText("##dir", &export_dir_);
    ImGui::SameLine();
    if (ImGui::Button("Browse")) {
        const std::string d = dialog_pick_folder(export_dir_.empty() ? settings.export_dir : export_dir_);
        if (!d.empty()) export_dir_ = d;
    }
    problems_time_ = -1;
    refresh_problems();
    const size_t errors = error_count(problems_);
    if (errors) ImGui::TextColored(ImVec4(1, 0.45f, 0.4f, 1), "%zu errors: see the Problems panel. The export is refused until they are fixed.", errors);
    else if (!problems_.empty()) ImGui::TextColored(ImVec4(1, 0.8f, 0.35f, 1), "%zu warnings (see Problems)", problems_.size());
    ImGui::BeginDisabled(errors > 0 || export_dir_.empty());
    if (ImGui::Button("Export", ImVec2(120, 0))) {
        ExportOptions o;
        o.project_dir = project_path.empty() ? std::string() : u8str(u8(project_path).parent_path());
        o.game_data_dir = settings.data_dir;
        const ExportResult r = export_mod(project, export_dir_, o);
        export_report_.clear();
        for (auto& e : r.errors) export_report_.push_back("error: " + e);
        for (auto& w : r.written) export_report_.push_back("written: " + w);
        for (auto& w : r.removed) export_report_.push_back("removed: " + w);
        if (r.ok) {
            export_report_.insert(export_report_.begin(), std::to_string(r.written.size()) + " files written, " + std::to_string(r.unchanged.size()) + " unchanged");
            status("Exported to " + export_dir_);
        } else {
            status("Export failed: " + r.errors.front());
        }
        for (auto& l : export_report_) log_info("export: " + l);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Close", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
    if (!export_report_.empty()) {
        ImGui::BeginChild("##report", ImVec2(0, 200), ImGuiChildFlags_Borders);
        for (auto& l : export_report_) ImGui::TextUnformatted(l.c_str());
        ImGui::EndChild();
    }
    ImGui::EndPopup();
}

void App::project_replaced() {
    for (auto& p : panels_) p->on_project_changed(*this);
    update_title();
}

void App::new_project() {
    auto act = [this] {
        project = Project{};
        project_path.clear();
        history.reset(project);
        project_replaced();
        status("New project");
    };
    if (history.dirty()) {
        after_discard_ = act;
        ask_discard_ = true;
    } else {
        act();
    }
}

void App::open_project(const std::string& path) {
    auto act = [this, path] {
        try {
            std::vector<std::string> warnings;
            project = load_project_file(path, &warnings);
            for (auto& w : warnings) log_warn(base_name(path) + ": " + w);
            project_path = path;
            history.reset(project);
            settings.add_recent(path);
            settings.save();
            project_replaced();
            status("Opened " + path);
        } catch (const std::exception& e) {
            log_error(e.what());
            status(std::string("Cannot open the project: ") + e.what());
        }
    };
    if (history.dirty()) {
        after_discard_ = act;
        ask_discard_ = true;
    } else {
        act();
    }
}

void App::request_open() {
    const std::string dir = project_path.empty() ? std::string() : u8str(u8(project_path).parent_path());
    const std::string path = dialog_open_file("risen-modkit project", kProjectFilter, dir);
    if (!path.empty()) open_project(path);
}

bool App::save_project(bool save_as) {
    std::string path = project_path;
    if (save_as || path.empty()) {
        const std::string dir = project_path.empty() ? std::string() : u8str(u8(project_path).parent_path());
        path = dialog_save_file("risen-modkit project", kProjectFilter, dir, project.mod.id + ".rmk.json");
        if (path.empty()) return false;
        // NFD appends only the last part of a double extension on some platforms
        if (path.size() < 9 || path.compare(path.size() - 9, 9, ".rmk.json") != 0) {
            if (path.size() >= 5 && path.compare(path.size() - 5, 5, ".json") == 0) path.resize(path.size() - 5);
            path += ".rmk.json";
        }
    }
    try {
        history.settle(project);
        save_project_file(project, path);
        project_path = path;
        history.mark_saved();
        settings.add_recent(path);
        settings.save();
        update_title();
        status("Saved " + path);
        return true;
    } catch (const std::exception& e) {
        log_error(e.what());
        status(std::string("Cannot save: ") + e.what());
        return false;
    }
}

void App::update_title() {
    std::string t = "risen-modkit - " + (project_path.empty() ? std::string("(unsaved project)") : base_name(project_path)) + (history.dirty() ? " *" : "");
    if (t != title_shown_ && window_) {
        glfwSetWindowTitle(window_, t.c_str());
        title_shown_ = t;
    }
}

void App::apply_ui_scale() { ImGui::GetStyle().FontScaleMain = settings.ui_scale; }

void App::setup_default_layout(unsigned int dockspace_id) {
    ImGui::DockBuilderRemoveNode(dockspace_id);
    ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspace_id, ImGui::GetMainViewport()->WorkSize);
    ImGuiID center = dockspace_id;
    ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.28f, nullptr, &center);
    ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.24f, nullptr, &center);
    ImGui::DockBuilderDockWindow(kPanelMod, left);
    ImGui::DockBuilderDockWindow(kPanelBrowser, left);
    ImGui::DockBuilderDockWindow(kPanelLog, bottom);
    ImGui::DockBuilderDockWindow("Problems", bottom);
    for (const char* t : {"Items", "Recipes", "NPCs", "Dialogues", "Quests", "World", kPanelSettings}) ImGui::DockBuilderDockWindow(t, center);
    ImGui::DockBuilderFinish(dockspace_id);
}

void App::handle_shortcuts() {
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput) return;  // let text fields have their own undo
    const bool ctrl = io.KeyCtrl || io.KeySuper;
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
        if (io.KeyShift) {
            if (history.redo(project)) project_replaced();
        } else if (history.undo(project)) {
            project_replaced();
        }
    }
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Y, false) && history.redo(project)) project_replaced();
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) save_project(io.KeyShift);
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_O, false)) request_open();
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_N, false)) new_project();
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_E, false)) request_export();
}

void App::draw_menu() {
    if (!ImGui::BeginMainMenuBar()) return;
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("New project", "Ctrl+N")) new_project();
        if (ImGui::MenuItem("Open project...", "Ctrl+O")) request_open();
        if (ImGui::BeginMenu("Open recent", !settings.recent.empty())) {
            for (auto& r : settings.recent)
                if (ImGui::MenuItem(r.c_str())) defer([this, r] { open_project(r); });
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Save", "Ctrl+S")) save_project(false);
        if (ImGui::MenuItem("Save as...", "Ctrl+Shift+S")) save_project(true);
        ImGui::Separator();
        if (ImGui::MenuItem("Export mod...", "Ctrl+E")) request_export();
        ImGui::Separator();
        if (ImGui::MenuItem("Quit")) glfwSetWindowShouldClose(window_, 1);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit")) {
        if (ImGui::MenuItem("Undo", "Ctrl+Z", false, history.can_undo()) && history.undo(project)) project_replaced();
        if (ImGui::MenuItem("Redo", "Ctrl+Y", false, history.can_redo()) && history.redo(project)) project_replaced();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Window")) {
        for (auto& p : panels_) ImGui::MenuItem(p->title(), nullptr, &p->open);
        ImGui::Separator();
        if (ImGui::MenuItem("Reset layout")) layout_done_ = false;
        ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
}

void App::draw_status_bar() {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float h = ImGui::GetFrameHeight();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x, vp->WorkPos.y + vp->WorkSize.y - h));
    ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x, h));
    ImGuiWindowFlags f = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking |
                         ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 2));
    if (ImGui::Begin("##status", nullptr, f)) {
        if (game_loading()) {
            std::lock_guard<std::mutex> lock(loader_mu_);
            ImGui::ProgressBar(loader_fraction_, ImVec2(160, 0), ("loading " + loader_what_).c_str());
        } else if (game) {
            ImGui::TextDisabled("%s (%zu templates)", game->data_dir().c_str(), game->templates().size());
        } else {
            ImGui::TextDisabled("%s", settings.data_dir.empty() ? "No game data directory (Settings)" : "Game data not loaded");
        }
        ImGui::SameLine(ImGui::GetWindowWidth() * 0.45f);
        ImGui::TextUnformatted(status_.c_str());
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

void App::draw_modals() {
    if (ask_discard_) {
        ImGui::OpenPopup("Unsaved changes");
        ask_discard_ = false;
    }
    if (ImGui::BeginPopupModal("Unsaved changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("The project has unsaved changes.");
        if (ImGui::Button("Save")) {
            ImGui::CloseCurrentPopup();
            if (save_project(false) && after_discard_) defer(after_discard_);
            after_discard_ = nullptr;
        }
        ImGui::SameLine();
        if (ImGui::Button("Discard")) {
            ImGui::CloseCurrentPopup();
            history.reset(project);  // nothing is dirty any more
            if (after_discard_) defer(after_discard_);
            after_discard_ = nullptr;
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            ImGui::CloseCurrentPopup();
            after_discard_ = nullptr;
            want_quit_ = false;
        }
        ImGui::EndPopup();
    }
}

void App::draw_frame() {
    refresh_problems();
    handle_shortcuts();
    draw_menu();
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    // the dock space leaves room for the status bar
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x, vp->WorkSize.y - ImGui::GetFrameHeight()));
    ImGui::SetNextWindowViewport(vp->ID);
    ImGuiWindowFlags host = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoBackground;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
    ImGui::Begin("##dockhost", nullptr, host);
    ImGui::PopStyleVar(2);
    const ImGuiID dock = ImGui::GetID("MainDock");
    if (!layout_done_) {
        if (ImGui::DockBuilderGetNode(dock) == nullptr || frame_ == 0 || !ImGui::GetIO().IniFilename) setup_default_layout(dock);
        layout_done_ = true;
    }
    ImGui::DockSpace(dock, ImVec2(0, 0), ImGuiDockNodeFlags_None);
    ImGui::End();

    for (auto& p : panels_) {
        if (!p->open) continue;
        if (!focus_request_.empty() && focus_request_ == p->title()) {
            ImGui::SetNextWindowFocus();
            focus_request_.clear();
        }
        p->draw(*this);
    }
    draw_status_bar();
    draw_modals();
    draw_export_modal();
}

int App::run() {
    glfwSetErrorCallback(glfw_error);
    if (!glfwInit()) {
        log_error("glfw: cannot initialise");
        return 1;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
    window_ = glfwCreateWindow(options.width, options.height, "risen-modkit", nullptr, nullptr);
    if (!window_) {
        log_error("glfw: cannot create an OpenGL 3.3 window");
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window_);
    glfwSwapInterval(1);
    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) {
        log_error("glad: cannot load OpenGL");
        return 1;
    }
    log_info(std::string("OpenGL ") + reinterpret_cast<const char*>(glGetString(GL_VERSION)) + ", " + reinterpret_cast<const char*>(glGetString(GL_RENDERER)));
    if (NFD_Init() != NFD_OKAY) log_warn(std::string("file dialogs unavailable: ") + NFD_GetError());

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_NavEnableKeyboard;
    static std::string ini_path = u8str(config_dir() / "imgui.ini");
    // screenshots use the default layout, never the user's
    io.IniFilename = options.screenshot.empty() ? ini_path.c_str() : nullptr;
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 4;
    style.FrameRounding = 3;
    style.GrabRounding = 3;
    style.TabRounding = 3;
    load_fonts();
    apply_ui_scale();
    ImGui_ImplGlfw_InitForOpenGL(window_, true);
    ImGui_ImplOpenGL3_Init("#version 330 core");

    load_game_data();
    if (!options.project.empty()) open_project(options.project);
    else project_replaced();
    if (!options.panel.empty()) focus_request_ = options.panel;
    pending_select_ = options.select;

    int rc = 0;
    while (!glfwWindowShouldClose(window_)) {
        glfwPollEvents();
        poll_loader();
        if (glfwGetWindowAttrib(window_, GLFW_ICONIFIED)) {
            glfwWaitEventsTimeout(0.1);
            continue;
        }
        apply_ui_scale();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        draw_frame();
        ImGui::Render();
        int w, h;
        glfwGetFramebufferSize(window_, &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(0.11f, 0.11f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        ++frame_;
        // screenshots wait for the game data (a minute at most), then count their frames
        if (!game_loading() || glfwGetTime() > 60) ++shot_frames_;
        if (!options.screenshot.empty() && shot_frames_ >= options.frames) {
            rc = save_screenshot(options.screenshot, w, h) ? 0 : 1;
            glfwSetWindowShouldClose(window_, 1);
        }
        glfwSwapBuffers(window_);

        // structural edits and file actions run between frames
        // --select: once the game data is there (or there is none to wait for)
        if (!pending_select_.empty() && !game_loading()) {
            for (auto& p : panels_)
                if (p->select(*this, pending_select_)) break;
            pending_select_.clear();
        }
        auto todo = std::move(deferred_);
        deferred_.clear();
        for (auto& fn : todo) fn();
        if (!ImGui::IsAnyItemActive()) history.settle(project);
        update_title();

        if (glfwWindowShouldClose(window_) && options.screenshot.empty() && history.dirty() && !want_quit_) {
            // ask once; "Discard" or a successful "Save" closes the window
            glfwSetWindowShouldClose(window_, 0);
            want_quit_ = true;
            after_discard_ = [this] { glfwSetWindowShouldClose(window_, 1); };
            ask_discard_ = true;
        }
    }
    settings.save();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    NFD_Quit();
    glfwDestroyWindow(window_);
    glfwTerminate();
    return rc;
}

}  // namespace mk
