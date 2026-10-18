// Loose resource files, the compiled combat table and the weather file.
// Ports of gen_assets / gen_combat / gen_weather of tools/extmod_gen.py (the oracle).
#include <algorithm>

#include "pack2mod/cs.h"
#include "pack2mod/internal.h"
#include "pack2mod/library.h"
#include "pack2mod/text.h"

namespace p2m {

AssetsResult gen_assets(Ctx& ctx, Data& d, const std::string& out_dir, bool copy) {
    AssetsResult R;
    std::set<std::string> names;
    for (const char* kind : {"common", "compiled"}) {
        Layered& L = d.layers(kind);
        for (const std::string& key : L.sorted_keys()) {
            const LFile& lf = L.files[key];
            const std::string bare = bare_name(lf.e->path);
            if (!is_asset_name(bare)) continue;
            const Bytes data = lf.read();
            const LFile* bk = d.base(kind).find(key);
            const LFile* other = bk ? bk : (std::string(kind) == "common" ? d.base_compiled : d.base_common).find(key);
            if (bk && bk->read() == data) {
                ++R.identical;
                continue;
            }
            if (d.has_classic) {
                // a resource the mod carries unchanged from the classic game where Enhanced has another one: the classic game's own difference, not the author's
                const LFile* ck = d.classic(kind).find(key);
                ctx.classic["resource files the mod differs from Enhanced in"]++;
                if (ck && ck->read() == data) {
                    ctx.classic["resource files: of them identical to the classic game's (not carried)"]++;
                    continue;
                }
                ctx.classic[ck ? "resource files: of them changed from the classic game (carried)" : "resource files: of them new, the classic game has no such file (carried)"]++;
            }
            if (!names.insert(lower(bare)).second) R.duplicates.push_back(bare);
            (other ? R.replaced : R.added).push_back(bare);
            R.bytes += static_cast<long long>(data.size());
            if (copy) write_file(out_dir + "/assets/" + bare, data);
        }
    }
    std::sort(R.added.begin(), R.added.end());
    std::sort(R.replaced.begin(), R.replaced.end());
    std::sort(R.duplicates.begin(), R.duplicates.end());
    return R;
}

namespace {
std::string label_ext(const std::string& label) {
    const size_t dot = label.rfind('.');
    return dot == std::string::npos ? "" : label.substr(dot + 1);
}
}  // namespace

CombatResult gen_combat(Ctx& ctx, Data& d, const std::string& out_dir) {
    CombatResult R;
    const LFile* bk = d.base_compiled.find("compiled_combatspecies.bin");
    const auto h = d.mod_compiled.history.find("compiled_combatspecies.bin");
    if (!bk || h == d.mod_compiled.history.end() || h->second.empty()) return R;
    // oldest and newest volume that ships the table: "changelog" and "shipped"
    const std::vector<LFile>& hist = h->second;
    const CsDoc base = parse_combat(bk->read());
    CsDoc classic_doc;  // --classic: the table the mod was made against
    std::map<std::string, const CsSpecies*> classic_by;
    if (d.has_classic)
        if (const LFile* ck = d.classic_compiled.find("compiled_combatspecies.bin")) {
            classic_doc = parse_combat(ck->read());
            for (const CsSpecies& sp : classic_doc.species) classic_by[sp.name] = &sp;
        }
    const bool three_way = !classic_by.empty();
    R.present = true;
    for (int variant = 0; variant < 2; ++variant) {
        const LFile& pick = variant == 0 ? hist.front() : hist.back();
        const CsDoc mod = parse_combat(pick.read());
        CsDoc res = base;
        std::map<std::string, const CsSpecies*> mod_by;
        for (const CsSpecies& sp : mod.species) mod_by[sp.name] = &sp;
        CombatVariant cv;
        cv.volume = pick.label;
        cv.file = "variants/combat_" + label_ext(pick.label) + ".bin";
        for (CsSpecies& sp : res.species) {
            const auto m = mod_by.find(sp.name);
            if (m == mod_by.end()) continue;
            std::vector<std::string> what;
            const CsSpecies& ms = *m->second;
            // three-way with --classic: a part of the species is the author's when the mod's differs from the classic game's; the rest stays as Enhanced has it
            const auto cit = classic_by.find(sp.name);
            const CsSpecies* cs = three_way && cit != classic_by.end() ? cit->second : nullptr;
            const bool noise_possible = three_way;
            auto author = [&](bool same_as_classic) { return !noise_possible || !cs || !same_as_classic; };
            if (!cs_equal(sp.styles, ms.styles)) {
                if (author(cs && cs_equal(cs->styles, ms.styles))) { sp.styles = ms.styles; what.push_back("styles (" + std::to_string(ms.styles.size()) + ")"); }
                else if (variant == 0) ctx.classic["combat species parts equal to the classic game's (not applied)"]++;
            }
            if (!cs_equal(sp.moves, ms.moves)) {
                if (author(cs && cs_equal(cs->moves, ms.moves))) { sp.moves = ms.moves; what.push_back("moves (" + std::to_string(ms.moves.size()) + ")"); }
                else if (variant == 0) ctx.classic["combat species parts equal to the classic game's (not applied)"]++;
            }
            if (!cs_equal(sp.ais, ms.ais)) {
                if (author(cs && cs_equal(cs->ais, ms.ais))) { sp.ais = ms.ais; what.push_back("ais (" + std::to_string(ms.ais.size()) + ")"); }
                else if (variant == 0) ctx.classic["combat species parts equal to the classic game's (not applied)"]++;
            }
            for (CsProp& p : sp.obj.props) {
                for (const CsProp& q : ms.obj.props) {
                    if (q.n != p.n) continue;
                    if (!cs_equal(q.v, p.v)) {
                        bool same = false;
                        if (cs)
                            for (const CsProp& c : cs->obj.props)
                                if (c.n == p.n) same = cs_equal(c.v, q.v);
                        if (author(same)) {
                            p.v = q.v;
                            what.push_back("property " + p.n);
                        } else if (variant == 0) {
                            ctx.classic["combat species parts equal to the classic game's (not applied)"]++;
                        }
                    }
                    break;
                }
            }
            if (variant == 0 && !what.empty() && three_way) ctx.classic["combat species with author changes (carried)"]++;
            if (!what.empty()) cv.changed.emplace_back(sp.name, std::move(what));
        }
        cv.data = write_combat(res);
        const CsDoc check = parse_combat(cv.data);
        for (const CsSpecies& sp : check.species) {
            const auto m = mod_by.find(sp.name);
            if (three_way) continue;  // what is equal to the classic game's is left as Enhanced has it on purpose
            if (m != mod_by.end() && (!cs_equal(sp.styles, m->second->styles) || !cs_equal(sp.moves, m->second->moves) || !cs_equal(sp.ais, m->second->ais)))
                ctx.warn.push_back("combat table: species " + sp.name + " differs from the mod's after writing");
        }
        (variant == 0 ? R.changelog : R.shipped) = std::move(cv);
    }
    if (!out_dir.empty()) {
        write_file(out_dir + "/" + R.changelog.file, R.changelog.data);
        if (R.shipped.file != R.changelog.file) write_file(out_dir + "/" + R.shipped.file, R.shipped.data);
    }
    return R;
}

std::vector<WeatherFile> gen_weather(Ctx& ctx, Data& d, const std::string& out_dir) {
    std::vector<WeatherFile> out;
    for (const std::string& key : d.mod_common.sorted_keys()) {
        if (!ends_with(key, ".g3ws")) continue;
        const auto& hist = d.mod_common.history[key];
        const LFile* bk = d.base_common.find(key);
        if (!bk) {
            ctx.warn.push_back("no " + key + " in the baseline: not converted");
            continue;
        }
        const Bytes data = hist.back().read();
        const Bytes base = bk->read();
        if (data == base) continue;
        if (data.size() != base.size()) {
            ctx.warn.push_back(bare_name(hist.back().e->path) + ": the mod's file has another size than the baseline's (" + std::to_string(data.size()) + " / " +
                               std::to_string(base.size()) + "): not converted");
            continue;
        }
        WeatherFile w;
        w.name = bare_name(hist.back().e->path);
        w.volume = hist.back().label;
        w.bytes = data.size();
        for (size_t i = 0; i < data.size(); ++i) w.bytes_differing += data[i] != base[i];
        w.data = data;
        if (!out_dir.empty()) write_file(out_dir + "/variants/" + w.name, data);
        out.push_back(std::move(w));
    }
    return out;
}

}  // namespace p2m
