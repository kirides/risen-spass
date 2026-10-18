// String tables -> strings/<Language>.csv (the layer's localisation overlay): only rows that differ from the user's own tables.
// Port of gen_strings of tools/extmod_gen.py (the oracle), with the mod specific row choices moved into the profile.
#include <algorithm>

#include "pack2mod/internal.h"
#include "pack2mod/tab.h"
#include "pack2mod/text.h"

namespace p2m {

namespace {
std::string write_text_crlf(const std::string& s) { return replace_all(s, "\n", "\r\n"); }

// a text without what the mod's spreadsheet export loses or changes: quote characters (straight, curly, backtick, acute, the replacement character), case and
// white space. Two texts equal in this form differ only by such an artefact, not by what the author wrote.
std::string loose_text(const std::string& in) {
    std::string s = in;
    for (const char* q : {"\"", "'", "`", "\xC2\xB4", "\xEF\xBF\xBD", "\xE2\x80\x98", "\xE2\x80\x99", "\xE2\x80\x9C", "\xE2\x80\x9D"}) s = replace_all(s, q, "");
    std::string out;
    bool space = false;
    for (unsigned char c : s) {
        if (c == ' ' || c == '\n' || c == '\r' || c == '\t') {
            space = !out.empty();
            continue;
        }
        if (space) out += ' ';
        space = false;
        out += static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
    }
    return out;
}
}  // namespace

StringsResult gen_strings(Ctx& ctx, Data& d, const std::set<std::string>& wanted, bool with_combat, bool all_rows) {
    StringsResult R;
    std::unordered_map<std::string, TabTable> vt;
    for (const std::string& k : d.base_compiled.order)
        if (ends_with(k, ".tab")) vt[k] = read_tab(d.base_compiled.files[k].read());
    std::unordered_map<std::string, TabTable> ct;  // the classic game's tables (--classic)
    if (d.has_classic)
        for (const std::string& k : d.classic_compiled.order)
            if (ends_with(k, ".tab")) {
                try {
                    ct[k] = read_tab(d.classic_compiled.files[k].read());
                } catch (const Error&) {
                }
            }
    std::set<std::string> lang_set;
    std::vector<std::string> lang_order;
    for (const std::string& k : d.mod_compiled.sorted_keys()) {
        if (!ends_with(k, ".tab")) continue;
        const LFile& lf = d.mod_compiled.files[k];
        TabTable mt;
        try {
            mt = read_tab(lf.read());
        } catch (const Error& e) {
            ctx.bad("strings", k, std::string("unreadable string table: ") + e.what(), "author");
            continue;
        }
        for (size_t c = 1; c < mt.columns.size(); ++c) {
            const std::string& col = mt.columns[c];
            if (!ends_with(col, "_Text")) continue;
            const std::string lang = col.substr(0, col.size() - 5);
            if (lang_set.insert(lang).second) lang_order.push_back(lang);
        }
        static const TabTable empty;
        const auto vit = vt.find(k);
        const TabTable& vr = vit == vt.end() ? empty : vit->second;
        const auto only = ctx.profile.string_only_rows.find(k);
        const bool changed_all = all_rows || ctx.profile.string_changed_all.count(k) || (with_combat && ctx.profile.string_changed_with_combat.count(k));
        int skipped_changed = 0, applied_new = 0, applied_changed = 0;
        for (const std::string& rid : mt.ids) {
            const bool isnew = !vr.has(rid);
            if (d.has_classic) {
                // three-way: a text the mod has and Enhanced has not (or has otherwise) is the author's when it differs from the classic game's text too
                static const TabTable none;
                const auto cit = ct.find(k);
                const TabTable& cr = cit == ct.end() ? none : cit->second;
                bool differs_enh = false, author = false, any = false, artefact = false;
                for (size_t c = 1; c < mt.columns.size(); ++c) {
                    const std::string& col = mt.columns[c];
                    if (!ends_with(col, "_Text")) continue;
                    const std::string lang = col.substr(0, col.size() - 5);
                    const auto t = norm_cell(mt.rows.at(rid)[c - 1]);
                    if (!t) continue;
                    const std::string* eold = vr.cell(rid, col);
                    if (!isnew && norm_cell(eold ? *eold : std::string()) == t) continue;
                    differs_enh = true;
                    const std::string* cold = cr.cell(rid, col);
                    if (cr.has(rid)) {
                        const auto cn = norm_cell(cold ? *cold : std::string());
                        if (cn == t) continue;
                        if (cn && loose_text(*cn) == loose_text(*t)) {
                            artefact = true;
                            continue;
                        }
                    }
                    if (loose_text(*t).empty() || *t == "-0") continue;  // a spreadsheet's placeholder
                    author = true;
                    R.rows[lang][rid] = *t;
                    any = true;
                    if (lang == "English") R.counts[k + (isnew ? " new rows" : " changed rows")]++;
                }
                (void)any;
                if (differs_enh) {
                    ctx.classic["string rows the mod differs from Enhanced in"]++;
                    ctx.classic[author ? "string rows: of them author changes (carried)" : artefact ? "string rows: of them only typographic artefacts of the mod's table (not applied)" : "string rows: of them only classic-vs-Enhanced differences (not applied)"]++;
                    if (isnew) ctx.classic["string rows: of them rows Enhanced does not have"]++;
                }
                continue;
            }
            if (only != ctx.profile.string_only_rows.end()) {
                if (!only->second.count(rid)) continue;
            } else if (!isnew && !changed_all) {
                bool related = false;
                if (starts_with(rid, "FO_") || starts_with(rid, "ITEMDESC_")) related = wanted.count(rid.substr(rid.find('_') + 1)) != 0;
                if (!related) {
                    // only count rows whose text really differs
                    bool diff = false;
                    for (size_t c = 1; c < mt.columns.size() && !diff; ++c) {
                        if (!ends_with(mt.columns[c], "_Text")) continue;
                        const std::string* old = vr.cell(rid, mt.columns[c]);
                        const auto a = norm_cell(old ? *old : std::string());
                        const auto b = norm_cell(mt.rows.at(rid)[c - 1]);
                        diff = a != b;
                    }
                    if (diff) ++skipped_changed;
                    continue;
                }
            }
            bool any = false;
            for (size_t c = 1; c < mt.columns.size(); ++c) {
                const std::string& col = mt.columns[c];
                if (!ends_with(col, "_Text")) continue;
                const std::string lang = col.substr(0, col.size() - 5);
                const auto t = norm_cell(mt.rows.at(rid)[c - 1]);
                if (!t) continue;
                if (!isnew) {
                    const std::string* old = vr.cell(rid, col);
                    if (norm_cell(old ? *old : std::string()) == t) continue;
                }
                R.rows[lang][rid] = *t;
                any = true;
                if (lang == "English") R.counts[k + (isnew ? " new rows" : " changed rows")]++;
            }
            if (any) (isnew ? applied_new : applied_changed)++;
        }
        if (skipped_changed)
            R.not_applied.push_back(k + ": " + std::to_string(skipped_changed) +
                                    " changed rows are not applied (they do not belong to a converted item; --strings all applies them)");
        (void)applied_new;
        (void)applied_changed;
    }
    // English first (it becomes the all.csv fallback), the others in table order
    std::sort(lang_order.begin(), lang_order.end(), [](const std::string& a, const std::string& b) { return (a == "English") > (b == "English"); });
    R.languages = lang_order;
    for (const std::string& l : R.languages) R.counts["strings " + l] = static_cast<int>(R.rows[l].size());
    return R;
}

void write_strings(const StringsResult& s, const std::string& out_dir) {
    const std::string dir = out_dir + "/strings";
    for (const std::string& lang : s.languages) {
        const auto it = s.rows.find(lang);
        std::string text;
        if (lang == "English") text = "# generated from the mod's string tables (English); other languages override\n";
        text += "id;text\n";
        if (it != s.rows.end())
            for (const auto& [id, t] : it->second) text += id + ";" + csv_field(t) + "\n";
        write_file(dir + "/" + (lang == "English" ? std::string("all") : lang) + ".csv", write_text_crlf(text));
    }
}

}  // namespace p2m
