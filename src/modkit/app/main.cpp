// risen-modkit: the mod editor of the Risen modding layer (docs/editor/REQUIREMENTS.md).
//   risen-modkit [project.rmk.json] [--data DIR] [--game-dir DIR [--play]] [--screenshot FILE [--frames N] [--panel TITLE] [--select NAME] [--size WxH]]
//   risen-modkit project.rmk.json --export DIR [--data DIR]     validate and export without a window (exit code 0 = exported)
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "app/app.h"
#include "core/export.h"
#include "core/import.h"
#include "core/log.h"
#include "core/model_import.h"
#include "core/paths.h"
#include "core/validate.h"

namespace {

constexpr const char* kUsage =
    "risen-modkit [project.rmk.json] [--data DIR] [--screenshot FILE [--frames N] [--panel TITLE] [--select NAME] [--size WxH] [--camera x,y,z,yaw,pitch] [--click x,y] [--drag x1,y1,x2,y2]]\n"
    "risen-modkit project.rmk.json --export DIR [--data DIR]\n"
    "risen-modkit --import MODDIR --save project.rmk.json [--from-data]\n"
    "risen-modkit --convert-model model.(obj|gltf|glb) --out My_Mesh._xmsh --material X._xmat [--scale S] [--z-up] [--no-mirror]\n";

int export_headless(const mk::AppOptions& opts, const std::string& out_dir) {
    using namespace mk;
    if (opts.project.empty()) {
        std::fprintf(stderr, "--export needs a project file\n");
        return 2;
    }
    Project p;
    try {
        std::vector<std::string> warnings;
        p = load_project_file(opts.project, &warnings);
        for (auto& w : warnings) log_warn(w);
    } catch (const std::exception& e) {
        log_error(e.what());
        return 1;
    }
    Settings settings = Settings::load();
    const std::string data_dir = !opts.data_dir.empty() ? opts.data_dir : settings.data_dir;
    std::shared_ptr<GameData> game;
    if (!data_dir.empty()) {
        try {
            game = GameData::load(data_dir);
        } catch (const std::exception& e) {
            log_warn(std::string("game data not loaded, validating the project alone: ") + e.what());
        }
    }
    const auto problems = validate(p, game.get());
    for (auto& pr : problems)
        (pr.severity == Severity::Error ? log_error : log_warn)(pr.area + " " + pr.subject + ": " + pr.message);
    if (error_count(problems)) {
        log_error("export refused: " + std::to_string(error_count(problems)) + " errors");
        return 1;
    }
    ExportOptions o;
    o.project_dir = u8str(u8(opts.project).parent_path());
    o.game_data_dir = data_dir;
    o.game = game.get();
    const ExportResult r = export_mod(p, out_dir, o);
    for (auto& e : r.errors) log_error(e);
    for (auto& w : r.written) log_info("written: " + w);
    for (auto& w : r.removed) log_info("removed: " + w);
    log_info(std::to_string(r.written.size()) + " written, " + std::to_string(r.unchanged.size()) + " unchanged, " + std::to_string(r.removed.size()) + " removed");
    return r.ok ? 0 : 1;
}

}  // namespace

// risen-modkit --convert-model model.obj --out My_Mesh._xmsh --material X._xmat [--scale S] [--z-up] [--no-mirror]
int convert_model_headless(const std::string& in, const std::string& out, const mk::ModelOptions& opt) {
    using namespace mk;
    std::string bytes;
    try {
        bytes = read_text(u8(in));
    } catch (const std::exception& e) {
        log_error(e.what());
        return 1;
    }
    const fs::path dir = u8(in).parent_path();
    const ModelSource src = parse_model(bytes, in, [&](const std::string& rel) {
        try {
            return read_text(dir / u8(rel));
        } catch (const std::exception&) {
            return std::string();
        }
    });
    for (auto& w : src.warnings) log_warn(w);
    if (!src.error.empty()) {
        log_error(in + ": " + src.error);
        return 1;
    }
    std::string error;
    const MeshBuild mesh = model_to_mesh(src, opt, error);
    if (!error.empty()) {
        log_error(error);
        return 1;
    }
    const std::string file = build_xmsh(mesh, error);
    if (!error.empty()) {
        log_error(error);
        return 1;
    }
    try {
        write_text(u8(out), file);
    } catch (const std::exception& e) {
        log_error(e.what());
        return 1;
    }
    log_info("wrote " + out + ": " + std::to_string(mesh.parts.size()) + " part(s), " + std::to_string(src.triangles()) + " triangles");
    return 0;
}

int import_headless(const std::string& dir, const std::string& save, bool prefer_data) {
    using namespace mk;
    ImportResult r = import_mod(dir, prefer_data);
    for (auto& w : r.warnings) log_warn(w);
    if (!r.ok) {
        log_error("import: " + r.error);
        return 1;
    }
    try {
        save_project_file(r.project, save);
    } catch (const std::exception& e) {
        log_error(e.what());
        return 1;
    }
    log_info(std::string("imported ") + (r.from_project_file ? "from modkit/project.json" : "from the data files") + " into " + save);
    return 0;
}

int main(int argc, char** argv) {
    mk::AppOptions opts;
    std::string export_dir, import_dir, save_path, convert_in, convert_out;
    mk::ModelOptions model_opt;
    bool from_data = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&](const char* what) -> std::string {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "%s needs %s\n", a.c_str(), what);
                std::exit(2);
            }
            return argv[++i];
        };
        if (a == "--data") opts.data_dir = next("a directory");
        else if (a == "--game-dir") opts.game_dir = next("a directory");
        else if (a == "--play") opts.play = true;
        else if (a == "--palette") opts.palette = next("a query");
        else if (a == "--wizard") opts.wizard = next("fetch, weapon or trader");
        else if (a == "--screenshot") opts.screenshot = next("a file name");
        else if (a == "--frames") opts.frames = std::max(2, std::atoi(next("a number").c_str()));
        else if (a == "--panel") opts.panel = next("a panel title");
        else if (a == "--select") opts.select = next("a name");
        else if (a == "--camera") opts.camera = next("x,y,z,yaw,pitch");
        else if (a == "--click") {
            float x = 0, y = 0;
            if (std::sscanf(next("x,y").c_str(), "%f,%f", &x, &y) != 2) {
                std::fprintf(stderr, "--click wants x,y\n");
                return 2;
            }
            opts.clicks.push_back({x, y});
        } else if (a == "--drag") {
            mk::AppOptions::Input in{};
            if (std::sscanf(next("x1,y1,x2,y2").c_str(), "%f,%f,%f,%f", &in.x, &in.y, &in.x2, &in.y2) != 4) {
                std::fprintf(stderr, "--drag wants x1,y1,x2,y2\n");
                return 2;
            }
            in.drag = true;
            opts.clicks.push_back(in);
        }
        else if (a == "--export") export_dir = next("a directory");
        else if (a == "--convert-model") convert_in = next("a .obj, .gltf or .glb file");
        else if (a == "--out") convert_out = next("a file name");
        else if (a == "--material") model_opt.material = next("a game material (X._xmat)");
        else if (a == "--scale") model_opt.scale = std::atof(next("a number").c_str());
        else if (a == "--z-up") model_opt.z_up = true;
        else if (a == "--no-mirror") model_opt.mirror_z = false;
        else if (a == "--import") import_dir = next("a mod folder");
        else if (a == "--save") save_path = next("a project file");
        else if (a == "--from-data") from_data = true;
        else if (a == "--verbose") mk::Log::get().set_echo_level(mk::Level::Debug);
        else if (a == "--size") {
            const std::string s = next("WxH");
            if (std::sscanf(s.c_str(), "%dx%d", &opts.width, &opts.height) != 2) {
                std::fprintf(stderr, "--size wants WxH\n");
                return 2;
            }
        } else if (a == "--help" || a == "-h") {
            std::printf("%s", kUsage);
            return 0;
        } else if (!a.empty() && a[0] == '-') {
            std::fprintf(stderr, "unknown option %s\n%s", a.c_str(), kUsage);
            return 2;
        } else {
            opts.project = a;
        }
    }
    mk::log_info("risen-modkit " MK_VERSION);
    if (!convert_in.empty()) {
        if (convert_out.empty() || model_opt.material.empty()) {
            std::fprintf(stderr, "--convert-model needs --out FILE and --material X._xmat\n");
            return 2;
        }
        return convert_model_headless(convert_in, convert_out, model_opt);
    }
    if (!export_dir.empty()) return export_headless(opts, export_dir);
    if (!import_dir.empty()) {
        if (save_path.empty()) {
            std::fprintf(stderr, "--import needs --save FILE\n");
            return 2;
        }
        return import_headless(import_dir, save_path, from_data);
    }
    mk::App app(std::move(opts));
    return app.run();
}
