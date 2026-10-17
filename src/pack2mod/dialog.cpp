// Dialogue infos (compiled_infos.bin): XP rewards and trader refills the mod added to existing infos.
// Port of infos_stage (tools/extmod_diff.py) and gen_dialog (tools/extmod_gen.py); the unified diff of Python's difflib is replaced by an LCS diff.
#include <algorithm>
#include <regex>

#include "pack2mod/internal.h"

namespace p2m {

namespace {

using Rec = std::vector<std::pair<std::string, Val>>;
struct InfoMap {
    std::vector<std::string> order;
    std::unordered_map<std::string, Rec> recs;
};

InfoMap split_infos(const Template& t) {
    InfoMap m;
    std::string cur;
    bool have = false;
    for (const Prop& p : t.props) {
        if (p.pset == "gCInfo" && p.name == "Name") {
            cur = p.value.repr();
            int n = 1;
            std::string name = cur;
            while (m.recs.count(name)) name = cur + "#" + std::to_string(++n);
            cur = name;
            m.order.push_back(cur);
            m.recs[cur];
            have = true;
        }
        if (have) m.recs[cur].emplace_back(p.key(), p.value);
    }
    return m;
}

bool rec_eq(const Rec& a, const Rec& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i].first != b[i].first || !val_eq(a[i].second, b[i].second)) return false;
    return true;
}
bool el_eq(const std::pair<std::string, Val>& a, const std::pair<std::string, Val>& b) { return a.first == b.first && val_eq(a.second, b.second); }

// removed (from a) and added (to b) elements of a longest common subsequence diff
void lcs_diff(const Rec& a, const Rec& b, std::vector<size_t>& removed, std::vector<size_t>& added) {
    // trim the common head and tail
    size_t head = 0;
    while (head < a.size() && head < b.size() && el_eq(a[head], b[head])) ++head;
    size_t ta = a.size(), tb = b.size();
    while (ta > head && tb > head && el_eq(a[ta - 1], b[tb - 1])) { --ta; --tb; }
    const size_t n = ta - head, m = tb - head;
    std::vector<std::vector<uint16_t>> L(n + 1, std::vector<uint16_t>(m + 1, 0));
    for (size_t i = n; i-- > 0;)
        for (size_t j = m; j-- > 0;)
            L[i][j] = el_eq(a[head + i], b[head + j]) ? static_cast<uint16_t>(L[i + 1][j + 1] + 1) : std::max(L[i + 1][j], L[i][j + 1]);
    size_t i = 0, j = 0;
    while (i < n || j < m) {
        if (i < n && j < m && el_eq(a[head + i], b[head + j])) { ++i; ++j; }
        else if (j < m && (i == n || L[i][j + 1] >= L[i + 1][j])) added.push_back(head + j++);
        else removed.push_back(head + i++);
    }
}

const Val* rec_get(const Rec& r, const std::string& key) {  // dict(rec).get: the last occurrence wins
    const Val* v = nullptr;
    for (const auto& kv : r)
        if (kv.first == key) v = &kv.second;
    return v;
}

}  // namespace

std::optional<std::string> resolve_name(Ctx& ctx, const std::unordered_map<std::string, std::string>& known, const std::string& item, const std::string& where) {
    const std::string low = lower(item);
    auto it = known.find(low);
    if (it != known.end()) return it->second;
    std::set<std::string> cands;
    if (ends_with(low, "_01")) {
        const auto k = known.find(low.substr(0, low.size() - 3));
        if (k != known.end()) cands.insert(k->second);
    }
    if (cands.empty())
        for (const auto& [k, v] : known)
            if (starts_with(k, low)) cands.insert(v);
    if (cands.empty()) {
        auto parts = [](const std::string& s) {
            std::vector<std::string> p;
            size_t a = 0;
            for (size_t i = 0; i <= s.size(); ++i)
                if (i == s.size() || s[i] == '_') { p.push_back(s.substr(a, i - a)); a = i + 1; }
            return p;
        };
        const auto lp = parts(low);
        const std::string tail = lp.back();
        for (const auto& [k, v] : known) {
            const auto kp = parts(k);
            if (kp.size() >= 2 && lp.size() >= 2 && kp[0] == lp[0] && kp[1] == lp[1] && ends_with(k, tail) && tail.size() > 6) cands.insert(v);
        }
    }
    if (cands.size() == 1) {
        ctx.warn.push_back(where + ": '" + item + "' is not a template; assumed to mean '" + *cands.begin() + "' (the mod's own typo)");
        return *cands.begin();
    }
    return std::nullopt;
}

DialogResult gen_dialog(Ctx& ctx, Data& d) {
    DialogResult R;
    R.data = LVal::dict();
    LVal xp = LVal::dict();
    xp.set("add", LVal::list());
    xp.set("change", LVal::list());
    const LFile* bk = d.base_compiled.find("compiled_infos.bin");
    const auto h = d.mod_compiled.history.find("compiled_infos.bin");
    if (!bk || h == d.mod_compiled.history.end() || h->second.empty()) {
        R.data.set("trade", LVal::list());
        R.data.set("xp", std::move(xp));
        return R;
    }
    const LFile* newest = &h->second.front();
    for (const LFile& f : h->second)
        if (f.label > newest->label) newest = &f;
    InfoMap A, B;
    try {
        A = split_infos(parse_template(bk->read()));
        B = split_infos(parse_template(newest->read()));
    } catch (const Error& e) {
        ctx.bad("dialogue", "compiled_infos.bin", std::string("unreadable: ") + e.what(), "tool");
        R.data.set("trade", LVal::list());
        R.data.set("xp", std::move(xp));
        return R;
    }
    const auto known = known_names(d);
    std::map<std::string, std::vector<std::tuple<std::string, std::string, Val>>> refills;  // info -> (trader, item, count)
    int new_infos = 0;
    std::vector<std::pair<std::string, LVal>> xp_add, xp_change;
    for (const std::string& k : B.order) {
        const Rec& rec = B.recs[k];
        const auto ai = A.recs.find(k);
        if (ai == A.recs.end()) { ++new_infos; continue; }
        if (rec_eq(rec, ai->second)) continue;
        const Val* ownerv = rec_get(rec, "gCInfo.Owner");
        std::vector<size_t> removed, added;
        lcs_diff(ai->second, rec, removed, added);
        std::vector<int64_t> plus, minus;
        bool xp_lines = false, rest = false;
        auto has = [](const std::pair<std::string, Val>& e, const char* w) { return contains(e.first, w) || contains(e.second.repr(), w); };
        for (size_t i : added) {
            const auto& e = rec[i];
            if (has(e, "GiveXP.Amount")) { xp_lines = true; if (e.first == "gCInfoCommandGiveXP.Amount" && e.second.is_int() && e.second.i >= 0) plus.push_back(e.second.i); }
            else if (!has(e, "GiveXP") && !has(e, "RunScript")) rest = true;
        }
        for (size_t i : removed) {
            const auto& e = ai->second[i];
            if (has(e, "GiveXP.Amount")) { xp_lines = true; if (e.first == "gCInfoCommandGiveXP.Amount" && e.second.is_int() && e.second.i >= 0) minus.push_back(e.second.i); }
            else if (!has(e, "GiveXP") && !has(e, "RunScript")) rest = true;
        }
        if (xp_lines) {
            LVal e = LVal::dict();
            e.set("info", LVal::str(k));
            e.set("owner", ownerv ? LVal::str(ownerv->repr()) : LVal::nil());
            if (plus.size() == 1 && minus.empty()) {
                e.set("amount", LVal::integer(plus[0]));
                xp_add.emplace_back(k, std::move(e));
            } else if (plus.size() == 1 && minus.size() == 1) {
                e.set("old", LVal::integer(minus[0]));
                e.set("new", LVal::integer(plus[0]));
                xp_change.emplace_back(k, std::move(e));
            } else {
                ctx.bad("dialogue", k, "XP change not understood (" + std::to_string(plus.size()) + " added, " + std::to_string(minus.size()) + " removed amounts)", "tool");
            }
        }
        for (size_t i = 0; i < rec.size(); ++i) {
            if (rec[i].first == "gCInfoCommandRunScript.Script" && rec[i].second.is_str() && rec[i].second.s == "OnTradeInfo_RefillTradeItems") {
                const Rec window(rec.begin() + static_cast<long>(i), rec.begin() + static_cast<long>(std::min(rec.size(), i + 5)));
                const Val* self = rec_get(window, "gCInfoCommandRunScript.Self");
                const Val* other = rec_get(window, "gCInfoCommandRunScript.Other");
                const Val* param = rec_get(window, "gCInfoCommandRunScript.Param");
                if (self && other && param) {
                    auto t = std::make_tuple(self->repr(), other->repr(), *param);
                    auto& lst = refills[k];
                    bool dup = false;
                    for (const auto& x : lst) dup = dup || (std::get<0>(x) == std::get<0>(t) && std::get<1>(x) == std::get<1>(t) && val_eq(std::get<2>(x), std::get<2>(t)));
                    if (!dup) lst.push_back(std::move(t));
                }
                i += 4;
            }
        }
        if (rest) ++R.infos_other;
    }
    if (R.infos_other)
        R.notes.push_back(std::to_string(R.infos_other) + " existing dialogue infos differ in other ways (commands or conditions added/removed): not converted; the converter only extracts what the mod adds (XP rewards, trader refills)");
    if (new_infos)
        R.notes.push_back(std::to_string(new_infos) + " new dialogue infos in " + newest->label + ": not converted (new conversations need their voice lines and quest logic)");
    LVal trade = LVal::list();
    static const std::regex chapter_re("Special_Chapter(\\d+)", std::regex::icase);
    for (const auto& [info, lst] : refills) {
        std::set<std::pair<std::string, std::string>> seen;
        LVal entries = LVal::list();
        for (const auto& [trader, item, count] : lst) {
            const auto real = resolve_name(ctx, known, item, "trade " + trader);
            if (!real) {
                ctx.bad("trade", trader + " " + item, "no template of that name in the mod or the game (trader entry skipped)", "author");
                continue;
            }
            if (!seen.insert({trader, *real}).second) continue;
            LVal en = LVal::dict();
            en.set("trader", LVal::str(trader));
            en.set("item", LVal::str(*real));
            en.set("count", LVal::integer(count.is_num() ? static_cast<int64_t>(count.num()) : 0));
            entries.push(std::move(en));
            ++R.trade_entries;
        }
        LVal t = LVal::dict();
        t.set("info", LVal::str(info));
        t.set("entries", std::move(entries));
        // the chapter title infos show a "Special_Chapter<N>" picture
        LVal chapter = LVal::nil();
        for (const auto& kv : B.recs[info]) {
            std::smatch m;
            if (kv.first == "gCInfoCommandShowPicture.FileName" && kv.second.is_str() && std::regex_search(kv.second.s, m, chapter_re)) {
                chapter = LVal::integer(std::stoi(m[1].str()));
                break;
            }
        }
        t.set("chapter", std::move(chapter));
        trade.push(std::move(t));
    }
    auto by_info = [](const auto& a, const auto& b) { return a.first < b.first; };
    std::sort(xp_add.begin(), xp_add.end(), by_info);
    std::sort(xp_change.begin(), xp_change.end(), by_info);
    for (auto& e : xp_add) xp.get("add")->push(std::move(e.second));
    for (auto& e : xp_change) xp.get("change")->push(std::move(e.second));
    R.xp = static_cast<int>(xp.get("add")->items.size() + xp.get("change")->items.size());
    R.data.set("layer", LVal::str(newest->label));
    R.data.set("trade", std::move(trade));
    R.data.set("xp", std::move(xp));
    return R;
}

}  // namespace p2m
