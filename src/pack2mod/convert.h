// risen-pack2mod: converts a Risen mod that ships .pNN / .pak archives into a mod for the risen-spass layer (docs/PACK2MOD.md).
#pragma once

#include <iosfwd>
#include <map>
#include <string>
#include <vector>

namespace p2m {

struct Options {
    std::vector<std::string> inputs;  // a mod folder (searched recursively for *.pNN / *.pak) or archive files
    std::string baseline;             // the user's own Risen data directory (data/common/*.pak, data/compiled/*.pak)
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
};

struct Counts {
    int items_changed = 0, new_items = 0, recipes_edited = 0, recipes_new = 0, template_patches = 0, xp_infos = 0, trade_entries = 0;
    int containers = 0, equipment = 0, teacher_limits = 0, assets_new = 0, assets_replaced = 0, assets_identical = 0;
    long long asset_bytes = 0;
    int unconverted = 0, warnings = 0;
};

// Returns 0 on success, 1 on bad arguments or unreadable input. Progress and the summary go to `log`.
int run_convert(const Options& o, std::ostream& log, Counts* counts = nullptr);

// command line front end (main.cpp and the tests call it)
int pack2mod_main(int argc, const char* const* argv, std::ostream& out, std::ostream& err);

}  // namespace p2m
