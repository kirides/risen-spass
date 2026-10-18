// Command line front end of risen-pack2mod.
#include <iostream>

#include "pack2mod/convert.h"
#include "pack2mod/util.h"

namespace p2m {

namespace {
const char* kUsage =
    "risen-pack2mod " P2M_VERSION " - converts a Risen mod's .pNN / .pak archives into a mod for the risen-spass layer\n"
    "\n"
    "usage: risen-pack2mod <mod folder | archive files...> --baseline <Risen data dir> (--out <mods/name> | --project <file.rmk.json>) [options]\n"
    "\n"
    "  --baseline DIR     the user's own Risen data directory (holds common/*.pak and compiled/*.pak); read only\n"
    "  --out DIR          the mod folder to write (replaced on a rerun; refuses a non-empty folder this tool did not create)\n"
    "  --id ID            mod id (default: made from the folder name)         --name TEXT   display name\n"
    "  --combat MODE      changelog (oldest volume with a combat table, default) | shipped (newest) | off\n"
    "  --report FILE      where to write report.md (default: <out>/report.md)\n"
    "  --profile FILE     JSON profile with knowledge about one specific mod (docs/PACK2MOD.md)\n"
    "  --strings all      apply every differing string row, not only new rows and rows of converted items\n"
    "  --no-assets        do not copy the loose resource files    --no-world   skip dialogue / world extraction\n"
    "  --project FILE     also (or instead of --out) write the result as a risen-modkit project (FILE.rmk.json in its own folder; needs a build with the modkit feature)\n"
    "  --world-edits      with --project: also carry moved / removed / added world entities (the mod's layers differ from the game's in more than the mod's own edits)\n"
    "  --verify           with --out and --project: export the project again and compare it with the converted mod\n"
    "  --list             list the archives and every file (new / changed / same) and stop\n"
    "  --dry-run          analyse and print the summary, write nothing\n"
    "\n"
    "The output is a derived work of the mod's author: private use, never commit or redistribute it.\n";
}

int pack2mod_main(int argc, const char* const* argv, std::ostream& out, std::ostream& err) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto value = [&](std::string& dst) -> bool {
            const size_t eq = a.find('=');
            if (eq != std::string::npos && a.compare(0, 2, "--") == 0) { dst = a.substr(eq + 1); return true; }
            if (i + 1 >= argc) { err << "error: " << a << " needs a value\n"; return false; }
            dst = argv[++i];
            return true;
        };
        const std::string key = a.substr(0, a.find('='));
        if (key == "-h" || key == "--help" || key == "/?") { out << kUsage; return 0; }
        else if (key == "--baseline") { if (!value(o.baseline)) return 1; }
        else if (key == "--out") { if (!value(o.out)) return 1; }
        else if (key == "--id") { if (!value(o.id)) return 1; }
        else if (key == "--name") { if (!value(o.name)) return 1; }
        else if (key == "--combat") { if (!value(o.combat)) return 1; }
        else if (key == "--report") { if (!value(o.report)) return 1; }
        else if (key == "--profile") { if (!value(o.profile)) return 1; }
        else if (key == "--strings") { std::string v; if (!value(v)) return 1; o.strings_all = v == "all"; }
        else if (key == "--no-assets") o.no_assets = true;
        else if (key == "--no-world") o.no_world = true;
        else if (key == "--project") { if (!value(o.project)) return 1; }
        else if (key == "--verify") o.verify = true;
        else if (key == "--world-edits") o.world_edits = true;
        else if (key == "--list") o.list = true;
        else if (key == "--dry-run") o.dry_run = true;
        else if (a.compare(0, 2, "--") == 0) { err << "error: unknown option " << a << "\n\n" << kUsage; return 1; }
        else o.inputs.push_back(a);
    }
    if (o.inputs.empty()) { err << kUsage; return 1; }
    try {
        return run_convert(o, out);
    } catch (const Error& e) {
        err << "error: " << e.what() << "\n";
        return 1;
    } catch (const std::exception& e) {
        err << "error: " << e.what() << "\n";
        return 1;
    }
}

}  // namespace p2m
