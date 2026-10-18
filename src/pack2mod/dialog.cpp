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


// ---- property edits of an existing info (--classic): the author changed a value of a command, a condition or the info itself, or appended a condition ----------

// the commands and conditions of an info record are runs of `gCInfoCommand<X>.<Prop>` / `gCInfoCondition<X>.<Prop>`; a new run starts when the class changes or a
// property repeats
struct Grp {
    int kind = 0;  // 1 command, 2 condition
    std::string cls;
    std::vector<size_t> idx;
};
std::vector<Grp> groups_of(const Rec& r) {
    std::vector<Grp> out;
    std::set<std::string> seen;
    bool open = false;
    for (size_t i = 0; i < r.size(); ++i) {
        const std::string& key = r[i].first;
        const int kind = starts_with(key, "gCInfoCommand") ? 1 : starts_with(key, "gCInfoCondition") ? 2 : 0;
        if (!kind) {
            open = false;
            continue;
        }
        const size_t dot = key.find('.');
        const std::string cls = key.substr(0, dot), prop = dot == std::string::npos ? std::string() : key.substr(dot + 1);
        if (!open || out.back().cls != cls || seen.count(prop)) {
            out.push_back({kind, cls, {}});
            seen.clear();
            open = true;
        }
        seen.insert(prop);
        out.back().idx.push_back(i);
    }
    return out;
}

// the text the layer's property functions use for a value; nullopt for what they cannot take (an unresolved template reference, vectors, bools)
std::optional<std::string> value_text(const Val& v, const std::unordered_map<std::string, std::string>& guid) {
    if (v.is_str()) {
        if (starts_with(v.s, "@")) {
            const auto g = guid.find(v.s);
            if (g == guid.end()) return std::nullopt;
            return g->second;
        }
        return v.s;
    }
    if (v.is_int()) return std::to_string(v.i);
    return std::nullopt;
}

// The author's edits as a list of operations, or nullopt when the difference is more than that (a command inserted or removed, a condition in the middle, ...).
// `rem` / `add` are the indices (into the classic and the mod's record) of the lines not explained otherwise.
std::optional<LVal> property_ops(const Rec& A, const Rec& B, const std::set<size_t>& rem, const std::set<size_t>& add, const std::unordered_map<std::string, std::string>& guid) {
    const std::vector<Grp> GA = groups_of(A), GB = groups_of(B);
    // the conditions of the sequence all have to hold, so where a new one stands does not matter: the mod's list without the conditions that are only added is the
    // one that corresponds to the classic game's (the layer appends them)
    auto all_added = [&](const Grp& g) {
        for (size_t k : g.idx)
            if (!add.count(k)) return false;
        return true;
    };
    auto of_kind = [&](const std::vector<Grp>& g, int kind, bool mod_side = false) {
        std::vector<const Grp*> v;
        for (const Grp& x : g)
            if (x.kind == kind && !(mod_side && kind == 2 && all_added(x))) v.push_back(&x);
        return v;
    };
    auto group_of = [](const std::vector<Grp>& g, size_t i) -> const Grp* {
        for (const Grp& x : g)
            for (size_t k : x.idx)
                if (k == i) return &x;
        return nullptr;
    };
    std::set<size_t> used;
    LVal ops = LVal::list();
    auto prop_of = [](const std::string& key) { return key.substr(key.find('.') + 1); };
    for (size_t i : rem) {
        const std::string& key = A[i].first;
        const Grp* ga = group_of(GA, i);
        size_t match = B.size();
        if (!ga) {  // an info property
            for (size_t j : add)
                if (!used.count(j) && B[j].first == key) { match = j; break; }
        } else {
            const auto la = of_kind(GA, ga->kind), lb = of_kind(GB, ga->kind, true);
            size_t ord = 0;
            while (ord < la.size() && la[ord] != ga) ++ord;
            if (ord >= lb.size() || lb[ord]->cls != ga->cls) return std::nullopt;
            for (size_t j : add)
                if (!used.count(j) && B[j].first == key && group_of(GB, j) == lb[ord]) { match = j; break; }
            if (match == B.size()) return std::nullopt;
            const auto o = value_text(A[i].second, guid), n = value_text(B[match].second, guid);
            if (!o || !n) return std::nullopt;
            LVal e = LVal::dict(true);
            e.set("kind", LVal::str(ga->kind == 1 ? "command" : "condition"));
            e.set("index", LVal::integer(static_cast<int64_t>(ord + 1)));
            e.set("name", LVal::str(prop_of(key)));
            e.set("old", LVal::str(*o));
            e.set("new", LVal::str(*n));
            ops.push(std::move(e));
            used.insert(match);
            continue;
        }
        if (match == B.size()) return std::nullopt;
        const auto o = value_text(A[i].second, guid), n = value_text(B[match].second, guid);
        if (!o || !n) return std::nullopt;
        LVal e = LVal::dict(true);
        e.set("kind", LVal::str("info"));
        e.set("index", LVal::integer(0));
        e.set("name", LVal::str(prop_of(key)));
        e.set("old", LVal::str(*o));
        e.set("new", LVal::str(*n));
        ops.push(std::move(e));
        used.insert(match);
    }
    // what is only added: a condition appended to the sequence (all its lines new, classes of the earlier ones unchanged)
    const auto ca = of_kind(GA, 2), cb = of_kind(GB, 2, true), call = of_kind(GB, 2);
    if (ca.size() != cb.size()) return std::nullopt;
    for (size_t k = 0; k < ca.size(); ++k)
        if (cb[k]->cls != ca[k]->cls) return std::nullopt;
    int group_no = 0;
    for (size_t j : add) {
        if (used.count(j)) continue;
        const Grp* gb = group_of(GB, j);
        if (!gb || gb->kind != 2 || !all_added(*gb)) return std::nullopt;
        size_t ord = 0;
        while (ord < call.size() && call[ord] != gb) ++ord;
        ++group_no;
        LVal e = LVal::dict(true);
        e.set("kind", LVal::str("add_condition"));
        e.set("index", LVal::integer(static_cast<int64_t>(ord + 1)));
        e.set("class", LVal::str(gb->cls.substr(std::string("gCInfoCondition").size())));
        e.set("name", LVal::str(prop_of(B[j].first)));
        const auto n = value_text(B[j].second, guid);
        if (!n) return std::nullopt;
        e.set("new", LVal::str(*n));
        ops.push(std::move(e));
        used.insert(j);
    }
    (void)group_no;
    if (used.size() != add.size()) return std::nullopt;
    return ops;
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
    // the mod's own changes are measured against the classic game's infos when it is given, else against the baseline's
    const LFile* bk = (d.has_classic ? d.classic_compiled : d.base_compiled).find("compiled_infos.bin");
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
    if (d.has_classic) {
        // what the mod's infos are against Enhanced's: the classic game's own differences are not the author's
        if (const LFile* eb = d.base_compiled.find("compiled_infos.bin")) {
            try {
                const InfoMap E = split_infos(parse_template(eb->read()));
                for (const std::string& k : B.order) {
                    const bool in_e = E.recs.count(k) != 0, in_c = A.recs.count(k) != 0;
                    if (!in_e) {
                        ctx.classic["dialogue infos the mod has that Enhanced lacks"]++;
                        ctx.classic[in_c ? "dialogue infos: of them classic infos (not carried)" : "dialogue infos: of them new in the mod"]++;
                    } else if (!rec_eq(B.recs[k], E.recs.at(k))) {
                        ctx.classic["dialogue infos the mod differs from Enhanced in"]++;
                        ctx.classic[in_c && rec_eq(B.recs[k], A.recs.at(k)) ? "dialogue infos: of them equal to the classic info (classic-vs-Enhanced, not applied)" : "dialogue infos: of them changed by the author"]++;
                    }
                }
            } catch (const Error&) {
            }
        }
    }
    const auto known = known_names(d);
    std::map<std::string, std::vector<std::tuple<std::string, std::string, Val>>> refills;  // info -> (trader, item, count)
    int new_infos = 0;
    std::vector<std::string> other_names;
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
        std::set<size_t> rest_rem, rest_add;
        auto has = [](const std::pair<std::string, Val>& e, const char* w) { return contains(e.first, w) || contains(e.second.repr(), w); };
        for (size_t i : added) {
            const auto& e = rec[i];
            if (has(e, "GiveXP.Amount")) { xp_lines = true; if (e.first == "gCInfoCommandGiveXP.Amount" && e.second.is_int() && e.second.i >= 0) plus.push_back(e.second.i); }
            else if (!has(e, "GiveXP") && !has(e, "RunScript")) { rest = true; rest_add.insert(i); }
        }
        for (size_t i : removed) {
            const auto& e = ai->second[i];
            if (has(e, "GiveXP.Amount")) { xp_lines = true; if (e.first == "gCInfoCommandGiveXP.Amount" && e.second.is_int() && e.second.i >= 0) minus.push_back(e.second.i); }
            else if (!has(e, "GiveXP") && !has(e, "RunScript")) { rest = true; rest_rem.insert(i); }
        }
        if (rest && d.has_classic) {
            // against the classic game these are the author's: a changed value or an appended condition is carried as an operation on the shipped info
            if (auto ops = property_ops(ai->second, rec, rest_rem, rest_add, d.guid)) {
                LVal ch = LVal::dict(true);
                ch.set("info", LVal::str(k));
                ch.set("owner", ownerv ? LVal::str(ownerv->repr()) : LVal::nil());
                ch.set("ops", std::move(*ops));
                R.changes.push(std::move(ch));
                ++R.property_infos;
                rest = false;
            }
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
        if (rest) {
            ++R.infos_other;
            other_names.push_back(k + (ownerv ? " (" + ownerv->repr() + ")" : std::string()));
        }
    }
    if (R.infos_other)
    {
        std::string names;
        if (d.has_classic)
            for (size_t i = 0; i < other_names.size() && i < 12; ++i) names += (i ? ", " : "") + other_names[i];
        R.notes.push_back(std::to_string(R.infos_other) + " existing dialogue infos differ in other ways (commands or conditions added/removed): not converted; the converter only extracts what the mod adds (XP rewards, trader refills)" +
                          (d.has_classic ? " [against the classic game, so these are the author's: " + names + "]" : std::string()));
    }
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
    if (!R.changes.items.empty()) {
        R.data.set("changes", R.changes);
        ctx.classic["dialogue infos with property edits the author made (carried)"] = R.property_infos;
    }
    R.data.set("layer", LVal::str(newest->label));
    R.data.set("trade", std::move(trade));
    R.data.set("xp", std::move(xp));
    return R;
}

}  // namespace p2m
