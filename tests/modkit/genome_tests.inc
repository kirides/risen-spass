// Tests of the strict entity-file reader (core/genome.h, docs/RE.md section 56). Included by modkit_tests.cpp.

#include "core/genome.h"

namespace {
std::string val_text(const p2m::Val& v) { return v.repr(); }
}  // namespace

TEST(genome_reads_every_template_and_layer) {
    auto g = game_data();
    if (!g) {
        std::printf("  (skipped: no --data)\n");
        return;
    }
    size_t tpl = 0, tpl_ok = 0, entities = 0, differ = 0, extra = 0, missing = 0;
    std::string failures, diffs;
    std::map<std::string, size_t> only_scanner, only_strict;
    auto base_key = [](const std::string& k) { return k.substr(0, k.find('=')); };
    for (const std::string& path : g->paths_with_prefix("templates/")) {
        if (!p2m::ends_with(p2m::lower(path), ".tple")) continue;
        ++tpl;
        const p2m::Bytes raw = g->read_path(path);
        mk::GFile f;
        try {
            f = mk::parse_genome(raw);
        } catch (const std::exception& e) {
            if (failures.size() < 600) failures += "\n    " + path + ": " + e.what();
            continue;
        }
        ++tpl_ok;
        entities += f.entities.size();
        // against the scanner: the same properties in the same order
        const p2m::Template a = p2m::parse_template(raw), b = mk::genome_template(f);
        std::map<std::string, int> ca, cb;
        for (auto& p : a.props) ++ca[p.pset + "." + p.name + "=" + val_text(p.value)];
        for (auto& p : b.props) ++cb[p.pset + "." + p.name + "=" + val_text(p.value)];
        if (ca != cb) {
            ++differ;
            for (auto& [k, n] : ca)
                if (cb[k] < n) {
                    ++missing;
                    only_scanner[base_key(k)] += n - cb[k];
                    if (diffs.size() < 0) diffs += "\n    " + path + ": scanner has, strict lacks: " + k.substr(0, 120);
                }
            for (auto& [k, n] : cb)
                if (ca[k] < n) {
                    ++extra;
                    only_strict[base_key(k)] += n - ca[k];
                    if (diffs.size() < 0) diffs += "\n    " + path + ": strict has, scanner lacks: " + k.substr(0, 120);
                }
        }
    }
    std::printf("  %zu templates: %zu read by the strict reader (%zu entities), %zu differ from the scanner (%zu values only in the scanner, %zu only in the strict reader)%s%s\n", tpl,
                tpl_ok, entities, differ, missing, extra, failures.c_str(), diffs.c_str());
    CHECK_EQ(tpl_ok, tpl);
    auto top = [](const char* what, const std::map<std::string, size_t>& m) {
        std::vector<std::pair<size_t, std::string>> v;
        for (auto& [k, n] : m) v.push_back({n, k});
        std::sort(v.rbegin(), v.rend());
        for (size_t i = 0; i < v.size() && i < 25; ++i) std::printf("  %s %zux %s\n", what, v[i].first, v[i].second.c_str());
    };
    top("only in the scanner:", only_scanner);
    top("only in the strict reader:", only_strict);
    // everything the scanner found is found by the strict reader, except: two values the scanner left as hex (a char, a bool container: the strict reader
    // decodes them), and four phantom properties it made out of the file names in the skills' string table
    for (auto& [k, n] : only_scanner) {
        const bool known = k == "eCParticle_PS.AlphaReference" || k == "gCScriptRoutine_PS.RoutineChanged" || p2m::starts_with(k, "gCSkills_PS.") && p2m::ends_with(k, ".tple");
        if (!known) std::printf("  UNEXPLAINED scanner value %s\n", k.c_str());
        CHECK(known);
    }

    size_t layers = 0, layers_ok = 0, ents = 0;
    std::string lfail;
    for (const std::string& path : g->paths_with_prefix("projects/")) {
        if (!p2m::ends_with(p2m::lower(path), ".lrent")) continue;
        ++layers;
        try {
            const mk::GFile f = mk::parse_genome(g->read_path(path));
            ents += f.entities.size();
            ++layers_ok;
        } catch (const std::exception& e) {
            if (lfail.size() < 600) lfail += "\n    " + path + ": " + e.what();
        }
    }
    std::printf("  %zu layers: %zu read (%zu entities)%s\n", layers, layers_ok, ents, lfail.c_str());
    CHECK_EQ(layers_ok, layers);
}

TEST(genome_world_entities_without_template) {
    auto g = game_data();
    if (!g) return;
    size_t none = 0, no_creator = 0, unresolved = 0;
    std::map<std::string, size_t> by_layer, guids;
    for (const std::string& path : g->paths_with_prefix("projects/World/")) {
        if (!p2m::ends_with(p2m::lower(path), ".lrent")) continue;
        const mk::GFile f = mk::parse_genome(g->read_path(path));
        for (auto& e : f.entities) {
            if (e.name == "DRE") continue;
            if (!e.creator.empty() && !g->template_by_guid(e.creator).empty()) continue;
            ++none;
            if (e.creator.empty()) {
                ++no_creator;
                std::string sets;
                for (auto& st : e.sets) sets += st.cls + " ";
                if (by_layer.size() < 12) by_layer[p2m::bare_name(path) + ": " + e.name + " [" + sets.substr(0, 90) + "]"]++;
            } else {
                ++unresolved;
                ++guids[e.creator];
            }
        }
    }
    std::printf("  %zu world entities without a template: %zu have no creator (level objects with their own property sets, vegetation), %zu name a creator GUID no template has (%zu distinct)\n",
                none, no_creator, unresolved, guids.size());
    CHECK(no_creator > 700 && no_creator < 800);
    CHECK_EQ(unresolved, size_t(19));
}
