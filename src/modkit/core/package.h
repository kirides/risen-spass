// Packages (docs/editor/REQUIREMENTS.md section 3.14): a mod as one zip file to hand to other players, and the check of a project against the mods that are already installed.
//   * `zip_files` / `unzip_files`: a plain zip (deflate, UTF-8 names, no zip64, fixed time stamps so the same content gives the same file);
//   * `build_package`: validates, exports the mod into a temporary folder and zips it below `<id>/` (so unpacking into `<Risen>\bin\spass\mods` is the whole install), with an
//     INSTALL.txt (version, author, what it requires);
//   * `find_conflicts` / `read_installed_mods`: which things two mods both create or change (a template name, a placed NPC, a quest, a property of a template ...).
#pragma once

#include <string>
#include <utility>
#include <vector>

#include "core/export.h"
#include "core/project.h"

namespace mk {

using ZipFiles = std::vector<std::pair<std::string, std::string>>;  // path inside the zip ('/' separated) -> content

std::string zip_files(const ZipFiles& files);
// throws std::runtime_error for data that is no zip, a compression other than stored / deflate or damaged data; entries whose names climb out ("..", absolute) are refused
ZipFiles unzip_files(const std::string& zip);

struct PackageResult {
    bool ok = false;
    std::vector<std::string> errors, warnings;
    size_t files = 0, bytes = 0;  // the files in the zip, the size of the zip
};
// `zip_path` is written atomically (a temporary file next to it, then renamed). The project is exported with `opts` into a temporary folder first.
PackageResult build_package(const Project& p, const ExportOptions& opts, const std::string& zip_path);
// the text of INSTALL.txt
std::string install_text(const Project& p);
// "id" must be usable as a folder name of the mods folder: letters, digits, '_' and '-', starting with a letter
bool valid_mod_id(const std::string& id);

// ---- conflicts -----------------------------------------------------------------------------------------------------------------------------------

struct Touch {
    std::string kind;  // "item", "recipe", "spell", "NPC template", "placed NPC", "quest", "dialogue info", "template property", "world entity", ...
    std::string name;
    bool creates = false;
};
std::vector<Touch> project_touches(const Project& p);

struct Conflict {
    std::string kind, name, detail;
    bool error = false;  // two mods create the same thing: one of them cannot work; false: both change it, the later one in the load order wins
};
// `a` and `b` are two mods; a conflict is listed once per thing
std::vector<Conflict> find_conflicts(const Project& a, const Project& b);

struct InstalledMod {
    std::string id, dir;
    bool has_project = false;  // the folder has modkit/project.json (a mod made with the editor): only those can be compared
    std::string problem;       // why it could not be read
    Project project;
};
// the subfolders of `mods_dir` (the mod `skip_id` is left out: the one being edited)
std::vector<InstalledMod> read_installed_mods(const std::string& mods_dir, const std::string& skip_id);

}  // namespace mk
