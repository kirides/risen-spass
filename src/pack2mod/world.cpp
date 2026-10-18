// World layers (*.lrent, "GENOMEDL" dynamic layers): teacher limits, items placed into containers, equipment of placed NPCs.
// Entity-aware scanner after the research helper of the Extended Mod analysis (RESEARCH_teachers.md): an entity record is `d6 00 <GUID16> 00 00 00 00
// <u16 name string index> <16 floats of its matrix>`; the property stream of the layer is attributed to the entity whose byte range contains it.
#include <algorithm>
#include <cmath>

#include "pack2mod/internal.h"

namespace p2m {

namespace {

struct Ent {
    std::string name, guid;  // guid as "@hex"
    size_t start = 0, end = 0;
    std::vector<size_t> props;  // indices into Lrent::t.props
    bool is_npc = false;
};

struct Lrent {
    Template t;
    std::vector<Ent> ents;
};

bool parse_lrent(const Bytes& b, Lrent& out) {
    out.t = parse_template(b);
    const auto& S = out.t.strings;
    const size_t tbl = rd32(b, 10);
    for (size_t p = 0; p + 2 <= tbl; ++p) {
        if (b[p] != '\xD6' || b[p + 1] != '\0') continue;
        if (p + 24 > tbl) continue;
        if (b.compare(p + 18, 4, std::string(4, '\0')) != 0 || (b[p + 4] == '\x1e' && b[p + 5] == '\0')) continue;
        const size_t idx = rd16(b, p + 22);
        if (idx >= S.size()) continue;
        const std::string& nm = S[idx];
        if (nm.empty() || starts_with(nm, "gC") || starts_with(nm, "eC") || starts_with(nm, "bC") || starts_with(nm, "bT")) continue;
        if (p + 24 + 64 > b.size()) continue;
        bool ok = true;
        for (size_t k = 0; k < 16; ++k) ok = ok && std::fabs(rdf(b, p + 24 + 4 * k)) < 1e7;
        if (!ok) continue;
        Ent e;
        e.name = nm;
        e.guid = "@" + hex_of(b.substr(p + 2, 16));
        e.start = p;
        out.ents.push_back(std::move(e));
        ++p;  // the match consumed two bytes
    }
    for (size_t i = 0; i < out.ents.size(); ++i) out.ents[i].end = i + 1 < out.ents.size() ? out.ents[i + 1].start : tbl;
    for (size_t pi = 0; pi < out.t.props.size(); ++pi) {
        const size_t off = out.t.props[pi].off;
        for (size_t i = out.ents.size(); i-- > 0;) {
            Ent& e = out.ents[i];
            if (e.start <= off && off < e.end) {
                e.props.push_back(pi);
                if (out.t.props[pi].pset == "gCNPC_PS") e.is_npc = true;
                break;
            }
        }
    }
    return true;
}

struct InvKey {
    std::string item;
    Val type, amount, slot;
    bool operator==(const InvKey& o) const { return item == o.item && val_eq(type, o.type) && val_eq(amount, o.amount) && val_eq(slot, o.slot); }
};
struct InvEntry {
    InvKey key;
    int n = 0;
};
using Inv = std::vector<InvEntry>;

Inv inventory_of(const Lrent& L, const Ent& e, const std::unordered_map<std::string, std::string>& guid) {
    // records of gCInventory_PS: a new record starts at "Amount"
    struct Rec { std::optional<std::string> tpl; Val type, amount, slot; };
    std::vector<Rec> recs;
    bool have = false;
    for (size_t pi : e.props) {
        const Prop& p = L.t.props[pi];
        if (p.pset != "gCInventory_PS") continue;
        if (p.name == "Amount" || !have) { recs.emplace_back(); have = true; }
        Rec& r = recs.back();
        if (p.name == "Template") {
            std::string v = p.value.is_str() ? p.value.s : p.value.repr();
            const auto g = guid.find(v);
            r.tpl = g == guid.end() ? v : g->second;
        } else if (p.name == "Type") r.type = p.value;
        else if (p.name == "Amount") r.amount = p.value;
        else if (p.name == "EquipSlot") r.slot = p.value;
    }
    Inv inv;
    for (const Rec& r : recs) {
        if (!r.tpl) continue;
        InvKey k{*r.tpl, r.type, r.amount, r.slot};
        auto it = std::find_if(inv.begin(), inv.end(), [&](const InvEntry& x) { return x.key == k; });
        if (it == inv.end()) inv.push_back({k, 1});
        else ++it->n;
    }
    return inv;
}

// Counter subtraction a - b (positive counts only, in a's order)
Inv inv_sub(const Inv& a, const Inv& b) {
    Inv out;
    for (const InvEntry& x : a) {
        int n = x.n;
        for (const InvEntry& y : b)
            if (y.key == x.key) n -= y.n;
        if (n > 0) out.push_back({x.key, n});
    }
    return out;
}

struct Teach { Val skill, min, max; };
std::vector<Teach> teach_of(const Lrent& L, const Ent& e) {
    std::vector<Teach> out;
    std::vector<std::pair<std::string, Val>> cur;
    bool have = false;
    auto flush = [&] {
        if (!have) return;
        Teach t;
        for (auto& kv : cur) {
            if (kv.first == "Skill") t.skill = kv.second;
            else if (kv.first == "MinValue") t.min = kv.second;
            else if (kv.first == "MaxValue") t.max = kv.second;
        }
        out.push_back(t);
    };
    for (size_t pi : e.props) {
        const Prop& p = L.t.props[pi];
        if (p.pset != "gCDialog_PS" || !contains(p.name, "TeachSkills[].")) continue;
        const std::string f = p.name.substr(p.name.rfind('.') + 1);
        bool dup = !have;
        for (auto& kv : cur) dup = dup || kv.first == f;
        if (dup) {
            flush();
            cur.clear();
            have = true;
        }
        cur.emplace_back(f, p.value);
    }
    flush();
    return out;
}

std::string guid_text(const std::string& at_hex) {
    // bCGuid bytes -> the text form risen.entity.by_id takes (Windows GUID layout: three little-endian fields)
    const std::string h = at_hex.substr(1);
    if (h.size() != 32) return at_hex;
    auto byte = [&](size_t i) { return h.substr(2 * i, 2); };
    return byte(3) + byte(2) + byte(1) + byte(0) + "-" + byte(5) + byte(4) + "-" + byte(7) + byte(6) + "-" + byte(8) + byte(9) + "-" + byte(10) + byte(11) + byte(12) +
           byte(13) + byte(14) + byte(15);
}

std::optional<std::string> resolve_known(const std::unordered_map<std::string, std::string>& known, const std::string& item) {
    const auto it = known.find(lower(item));
    if (it == known.end()) return std::nullopt;
    return it->second;
}

}  // namespace

WorldResult gen_world(Ctx& ctx, Data& d) {
    WorldResult R;
    LVal containers = LVal::list(), equipment = LVal::list(), teachers = LVal::list();
    const auto known = known_names(d);
    // GUID -> template name for the references inside world layers: the game's own template wins when a mod template copies its GUID
    std::unordered_map<std::string, std::string> wguid;
    for (const std::string& n : d.van_order)
        for (const std::string& g : d.van[n].guids) wguid.emplace(g, n);
    for (const std::string& n : d.mod_order)
        for (const std::string& g : d.mod[n].guids) wguid.emplace(g, n);
    for (const auto& [n, t] : d.cla)
        for (const std::string& g : t.guids) wguid.emplace(g, n);
    struct Work { std::string key, base_path; const LFile* mod; const LFile* base; };
    std::vector<Work> work;
    for (const std::string& key : d.mod_common.sorted_keys()) {
        if (!ends_with(key, ".lrent")) continue;
        const LFile& m = d.mod_common.files[key];
        const LFile* b = d.reference("common").find(key);  // the classic game's layer when it is given
        if (!b) {
            ctx.bad("world", m.e->path, "world layer of the mod that the game does not have: new placements are not translated", "tool");
            continue;
        }
        work.push_back({key, b->e->path, &m, b});
    }
    int unreadable = 0;
    struct Cont { std::string name, id, layer; LVal items; };
    std::vector<Cont> conts;
    std::vector<std::pair<std::string, LVal>> teach_rows;  // sorted later by the game's path
    std::vector<std::tuple<std::string, size_t, LVal>> teach_sorted;
    int other_changes = 0;
    for (const Work& w : work) {
        Lrent a, b;
        try {
            if (!parse_lrent(w.base->read(), a) || !parse_lrent(w.mod->read(), b)) continue;
        } catch (const Error&) {
            ++unreadable;
            continue;
        }
        std::unordered_map<std::string, const Ent*> av;
        for (const Ent& e : a.ents) av.emplace(e.guid, &e);
        size_t order = 0;
        for (const Ent& e : b.ents) {
            ++order;
            const auto o = av.find(e.guid);
            if (o == av.end()) continue;
            const Ent& old = *o->second;
            // ---- teacher limits
            const std::vector<Teach> ta = teach_of(a, old), tb = teach_of(b, e);
            for (size_t i = 0; i < ta.size() && i < tb.size(); ++i) {
                if (val_eq(ta[i].max, tb[i].max)) continue;
                LVal t = LVal::dict();
                t.set("id", LVal::str(guid_text(e.guid)));
                t.set("name", LVal::str(e.name));
                const std::string sk = ctx.skill(&ta[i].skill);
                t.set("skill", LVal::str(sk.empty() ? ta[i].skill.repr() : sk));
                t.set("max", to_lval(tb[i].max));
                t.set("old", to_lval(ta[i].max));
                teach_sorted.emplace_back(lower(w.base_path), order, std::move(t));
            }
            // ---- inventory: containers and equipment
            const Inv ia = inventory_of(a, old, wguid), ib = inventory_of(b, e, wguid);
            const Inv add = inv_sub(ib, ia), rem = inv_sub(ia, ib);
            if (add.empty() && rem.empty()) continue;
            // containers and objects take every added item; an NPC only the items that exist in the mod but not in the game (placing shipped
            // items in NPC inventories is bookkeeping noise of a re-saved layer): equipped ones are equipment, the rest is put into the inventory
            LVal items = LVal::list();
            for (const InvEntry& x : add) {
                if (e.is_npc && (d.van.count(x.key.item) || !d.mod.count(x.key.item))) continue;
                const bool equipped = e.is_npc && x.key.slot.is_num() && x.key.slot.num() != 0;
                const auto real = resolve_name(ctx, known, x.key.item, "container " + e.name);
                if (!real) {
                    ctx.bad("container", e.name, "item " + x.key.item + " unknown", "author");
                    continue;
                }
                if (equipped) {
                    LVal q = LVal::dict();
                    q.set("id", LVal::str(guid_text(e.guid)));
                    q.set("name", LVal::str(e.name));
                    q.set("item", LVal::str(*real));
                    equipment.push(std::move(q));
                    ++R.equipment;
                    continue;
                }
                int64_t n = x.key.amount.is_num() ? static_cast<int64_t>(x.key.amount.num()) : 0;
                for (const InvEntry& r : rem)
                    if (r.key.item == x.key.item) n -= r.key.amount.is_num() ? static_cast<int64_t>(r.key.amount.num()) : 0;
                if (n > 0) {
                    LVal it = LVal::dict();
                    it.set("item", LVal::str(*real));
                    it.set("count", LVal::integer(n));
                    items.push(std::move(it));
                }
            }
            if (!items.items.empty()) conts.push_back({e.name, guid_text(e.guid), bare_name(w.mod->e->path), std::move(items)});
        }
        (void)other_changes;
    }
    if (unreadable) ctx.warn.push_back(std::to_string(unreadable) + " world layer(s) could not be read");
    std::stable_sort(teach_sorted.begin(), teach_sorted.end(), [](const auto& x, const auto& y) { return std::tie(std::get<0>(x), std::get<1>(x)) < std::tie(std::get<0>(y), std::get<1>(y)); });
    for (auto& t : teach_sorted) {
        teachers.push(std::move(std::get<2>(t)));
        ++R.teachers;
    }
    for (Cont& c : conts) {
        LVal q = LVal::dict();
        q.set("id", LVal::str(c.id));
        q.set("name", LVal::str(c.name));
        q.set("layer", LVal::str(c.layer));
        q.set("items", std::move(c.items));
        containers.push(std::move(q));
        ++R.containers;
    }
    R.data = LVal::dict();
    R.data.set("containers", std::move(containers));
    R.data.set("equipment", std::move(equipment));
    R.data.set("teachers", std::move(teachers));
    return R;
}

}  // namespace p2m
