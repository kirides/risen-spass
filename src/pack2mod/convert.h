// risen-pack2mod: converts a Risen mod that ships .pNN / .pak archives into a mod for the risen-spass layer (docs/PACK2MOD.md).
#pragma once

#include <functional>
#include <iosfwd>
#include <map>
#include <string>
#include <vector>

#include "pack2mod/lua_emit.h"

namespace p2m {

struct Options {
    std::vector<std::string> inputs;  // a mod folder (searched recursively for *.pNN / *.pak) or archive files
    std::string baseline;             // the user's own Risen data directory (data/common/*.pak, data/compiled/*.pak)
    std::string classic;              // --classic DIR: the classic game's data directory (read only): the mod's own changes are what differs from IT, not from the baseline
    std::string out;                  // output mod directory
    std::string id;                   // mod id (default: made from the folder name)
    std::string name;                 // display name (default: "<id> (converted)")
    std::string combat = "changelog"; // changelog | shipped | off: which combat table (oldest / newest volume) becomes the loose file
    std::string report;               // report.md path (default: <out>/report.md)
    std::string profile;              // optional JSON profile with mod specific knowledge (docs/PACK2MOD.md)
    bool list = false;                // list the archives and their files, convert nothing
    bool dry_run = false;             // analyse and report, write nothing
    bool no_assets = false;           // do not copy the loose resource files
    bool strings_all = false;         // apply every differing string row (default: new rows + rows of converted entities)
    bool no_world = false;            // skip dialogue / world extraction
    std::string project;              // --project FILE.rmk.json: also (or only) write the result as a risen-modkit project (needs the modkit build feature)
    bool world_edits = false;         // --world-edits: carry the moved / removed / added entities of the changed world layers into the project (docs/PACK2MOD.md)
    bool verify = false;              // --verify: export that project again and compare it with the converted mod (needs --out and --project)
};

struct Counts {
    int items_changed = 0, new_items = 0, recipes_edited = 0, recipes_new = 0, template_patches = 0, xp_infos = 0, trade_entries = 0;
    int containers = 0, equipment = 0, teacher_limits = 0, assets_new = 0, assets_replaced = 0, assets_identical = 0;
    long long asset_bytes = 0;
    int unconverted = 0, warnings = 0;
};

// ---- the risen-modkit project (docs/PACK2MOD.md "Editor project") ------------------------------------------------------------------------
// The stages' results are handed to a writer that maps them onto the editor's project model. The writer lives in modkit_core (src/modkit/core/pack2project.cpp),
// which depends on this library, so it registers itself here: a build without the modkit feature has no writer and --project says so.

struct Issue {  // something the converter could not convert (Unconverted of the report)
    std::string kind, name, reason, needs;
};

// a world layer (`*.lrent`) the mod changes: the game's file ("" = the game has no such layer) and the mod's newest one
struct WorldLayerFile {
    std::string name;  // bare file name
    std::string base, mod;
    std::string enhanced;  // with --classic: the baseline's (Enhanced's) layer, `base` being the classic game's ("" = none or not given)
};

struct ProjectInput {
    std::string id, name;                       // the mod's identity (as in mod.toml)
    std::string baseline;                       // the game's data directory (the writer loads it to classify templates and to validate the result)
    std::string project_file, project_dir;      // absolute, UTF-8
    std::string mod_dir;                        // the layer mod written by the same run ("" = none): --verify compares with it
    bool verify = false;
    bool has_classic = false;                   // --classic was given: `base` of a world layer is the classic game's, the mod's changes are real ones
    bool world_edits = false;                   // carry the moved / removed / added world entities (they cannot be told from the classic base's own differences)
    // exactly the tables written to data/*.lua of the layer mod
    LVal items, new_items, recipes_edit, recipes_new, patches, dialog, world;
    std::vector<std::string> languages;                                        // English first
    std::map<std::string, std::map<std::string, std::string>> strings;         // language -> string id -> text (the rows of strings/*.csv)
    std::vector<std::string> assets;                                           // bare file names written to <project_dir>/assets
    std::vector<WorldLayerFile> world_layers;                                  // the changed world layers: moved / removed / new entities are read from them
    std::map<std::string, std::string> template_guids;                         // "@" + 32 hex -> template name, the game's and the mod's templates
    std::vector<std::string> template_files;                                   // names of the mod's templates (the creator of a new entity may be one of them)
    std::vector<Issue> unconverted;
    std::vector<std::string> warnings, notes;
};

struct ProjectResult {
    int rc = 0;
    std::string error;
    std::string report_section;  // markdown appended to report.md
    std::string manifest;        // JSON text of <project_dir>/pack2mod-project.json
    std::string summary;         // one line for the console
};
using ProjectWriter = std::function<ProjectResult(const ProjectInput&, std::ostream& log)>;
void set_project_writer(ProjectWriter w);  // called by the program that links the modkit core (main.cpp)
bool has_project_writer();

// Returns 0 on success, 1 on bad arguments or unreadable input. Progress and the summary go to `log`.
int run_convert(const Options& o, std::ostream& log, Counts* counts = nullptr);

// command line front end (main.cpp and the tests call it)
int pack2mod_main(int argc, const char* const* argv, std::ostream& out, std::ostream& err);

}  // namespace p2m
