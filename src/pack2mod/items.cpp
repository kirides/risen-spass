// Template stages: existing items the mod changed, new item templates (cloned from the closest shipped item), recipes.
// Port of gen_items / gen_new / pick_base of tools/extmod_gen.py (the oracle).
#include <algorithm>
#include <cmath>
#include <set>

#include "pack2mod/internal.h"
#include "pack2mod/text.h"

namespace p2m {

namespace {

struct EnumRow { const char* type; int value; const char* name; };
struct SkillRow { int id; const char* name; };
#include "pack2mod/enums_data.inc"

const char* const kNoise[] = {"eCCollisionShape.", "eCRigidBody_PS.", "gCNavigation_PS.", "gCScriptRoutine_PS.", "gCInventory_PS.",
                              "eCCollisionShape_PS.", "gCNPC_PS.", "gCInteraction."};
const char* const kSkipSets[] = {"eCCollisionShape", "eCCollisionShape_PS", "eCRigidBody_PS", "gCNavigation_PS", "gCScriptRoutine_PS", "gCInventory_PS", "gCNPC_PS"};

bool is_noise(const std::string& k) {
    for (const char* p : kNoise)
        if (starts_with(k, p)) return true;
    return false;
}
std::string set_of(const std::string& k) { return k.substr(0, k.find('.')); }
bool is_skip_set(const std::string& k) {
    const std::string s = set_of(k);
    for (const char* p : kSkipSets)
        if (s == p) return true;
    return false;
}

const char* station_of(int64_t craft) {
    switch (craft) {
        case 0: return "alchemy";
        case 3: return "frying";
        case 4: return "goldsmith";
        case 5: return "scroll";
        case 6: return "forge";
        case 7: return "anvil";
        case 8: return "coolweapon";
        case 9: return "grindstone";
    }
    return nullptr;
}
const char* mode_of(int64_t m) {
    switch (m) {
        case 0: return "add";
        case 1: return "set";
        case 2: return "max";
        case 3: return "percent";
    }
    return nullptr;
}

std::string repr_of(const Val& v) { return v.is_str() ? "'" + v.s + "'" : v.repr(); }

Val norm_val(const Val* v) {
    if (!v) return Val();
    if (v->is_str()) return Val::of_str(norm_mesh(v->s));
    return *v;
}
bool differs(const Val* a, const Val* b) { return !val_eq(a ? *a : Val(), b ? *b : Val()); }

// ---- the item view of a template (item_view of extmod_gen.py)

struct SkillReq {
    std::string skill;
    Val amount;
};
struct SkillMod {
    std::string skill;
    Val amount;
    std::string mode;
};
struct ItemView {
    std::map<std::string, Val> scal;  // value, damage, mana_cost, use_type, category
    Flat props;
    std::vector<SkillReq> requires_;
    std::vector<SkillMod> modifies;
    std::optional<RecipeInfo> recipe;
};

Val get_or(const Elem& e, const char* k, Val dflt) {
    const Val* v = e.get(k);
    return v ? *v : dflt;
}

ItemView item_view(Ctx& ctx, const Flat& P, const std::string& owner) {
    ItemView out;
    static const std::map<std::string, std::string> scalar = {{"gCItem_PS.GoldValue", "value"},
                                                              {"gCDamage_PS.DamageAmount", "damage"},
                                                              {"gCCastInfo_PS.ManaCost", "mana_cost"},
                                                              {"gCItem_PS.UseType", "use_type"},
                                                              {"gCItem_PS.Category", "category"}};
    for (size_t i = 0; i < P.size(); ++i) {
        const std::string& k = P.keys[i];
        const auto sc = scalar.find(k);
        if (sc != scalar.end()) {
            out.scal[sc->second] = P.vals[i];
        } else if (contains(k, "[]") || starts_with(k, "gCRecipe_PS.")) {
            continue;
        } else if (is_noise(k) || is_skip_set(k) || contains(k, "#")) {
            continue;
        } else {
            out.props.put(k, P.vals[i], P.types[i]);
        }
    }
    for (const Elem& e : arrays_of(P, "gCItem_PS.RequiredSkills[].")) {
        const std::string s = ctx.skill(e.get("Skill"));
        if (s.empty()) {
            ctx.bad("skill", owner, "required skill number " + (e.get("Skill") ? repr_of(*e.get("Skill")) : "None") + " has no name", "tool");
            continue;
        }
        out.requires_.push_back({s, get_or(e, "Amount", Val::of_int(0))});
    }
    for (const Elem& e : arrays_of(P, "gCItem_PS.ModifySkills[].")) {
        const std::string s = ctx.skill(e.get("Skill"));
        if (s.empty()) {
            ctx.bad("skill", owner, "modified skill number " + (e.get("Skill") ? repr_of(*e.get("Skill")) : "None") + " has no name", "tool");
            continue;
        }
        const Val m = get_or(e, "Modifier", Val::of_int(0));
        const char* mode = m.is_int() ? mode_of(m.i) : nullptr;
        if (!mode) {
            ctx.bad("skill", owner, "modifier mode " + repr_of(m) + " unknown", "tool");
            mode = "add";
        }
        out.modifies.push_back({s, get_or(e, "Amount", Val::of_int(0)), mode});
    }
    if (P.has("gCRecipe_PS.Craft")) {
        RecipeInfo r;
        for (const Elem& e : arrays_of(P, "gCRecipe_PS.CraftIngredients[].")) {
            const Val* it = e.get("ItemTemplate");
            r.ingredients.emplace_back(it ? *it : Val(), get_or(e, "ItemAmount", Val::of_int(1)));
        }
        for (const Elem& e : arrays_of(P, "gCRecipe_PS.RequiredSkills[].")) {
            const std::string s = ctx.skill(e.get("Skill"));
            if (!s.empty()) r.skills.emplace_back(s, get_or(e, "Amount", Val::of_int(0)));
        }
        r.craft = *P.get("gCRecipe_PS.Craft");
        if (r.craft.is_int()) {
            const char* st = station_of(r.craft.i);
            if (st) r.station = st;
        }
        const Val* res = P.get("gCRecipe_PS.ResultItem");
        r.result = res ? *res : Val();
        r.amount = Val::of_int(1);
        if (const Val* am = P.get("gCRecipe_PS.ResultAmount")) r.amount = *am;
        out.recipe = std::move(r);
    }
    return out;
}

bool same_skills(const std::vector<SkillReq>& a, const std::vector<SkillReq>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i].skill != b[i].skill || !val_eq(a[i].amount, b[i].amount)) return false;
    return true;
}
bool same_mods(const std::vector<SkillMod>& a, const std::vector<SkillMod>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i].skill != b[i].skill || !val_eq(a[i].amount, b[i].amount) || a[i].mode != b[i].mode) return false;
    return true;
}

LVal req_lua(const std::vector<SkillReq>& l) {
    LVal out = LVal::list();
    for (const SkillReq& r : l) {
        LVal d = LVal::dict();
        d.set("skill", LVal::str(r.skill));
        d.set("amount", to_lval(r.amount));
        out.push(std::move(d));
    }
    return out;
}
LVal mod_lua(const std::vector<SkillMod>& l) {
    LVal out = LVal::list();
    for (const SkillMod& r : l) {
        LVal d = LVal::dict();
        d.set("skill", LVal::str(r.skill));
        d.set("amount", to_lval(r.amount));
        if (r.mode != "add") d.set("mode", LVal::str(r.mode));
        out.push(std::move(d));
    }
    return out;
}

// ---- property diff

std::optional<LVal> prop_value(Ctx& ctx, const std::string& key, const std::string* typ, const Val& v, const std::string& owner) {
    if (typ && starts_with(*typ, "bTPropertyContainer<enum")) {
        const auto s = ctx.enum_name(*typ, v);
        if (!s) {
            ctx.bad("property", owner + " " + key, "enum value " + repr_of(v) + " of " + *typ + " has no known name (the tool's enum table)", "tool");
            return std::nullopt;
        }
        return LVal::str(*s);
    }
    if (v.k == Val::Vec) return LVal::str(vec_text(v.v));
    if (ends_with(key, "MeshFileName") && v.is_str()) return LVal::str(norm_mesh(v.s));
    if (v.is_str() && starts_with(v.s, "@")) {
        ctx.bad("property", owner + " " + key, "reference to an unknown template (" + v.s + ")", "author");
        return std::nullopt;
    }
    return to_lval(v);
}

LVal prop_diff(Ctx& ctx, const std::string& name, const Flat& was, const Flat& now) {
    LVal out = LVal::dict(true);
    std::vector<std::string> keys = was.keys;
    for (const std::string& k : now.keys)
        if (!was.has(k)) keys.push_back(k);
    std::sort(keys.begin(), keys.end());
    for (const std::string& k : keys) {
        const Val* a = was.get(k);
        const Val* b = now.get(k);
        if (ends_with(k, "MeshFileName")) {
            if (val_eq(norm_val(a), norm_val(b))) continue;
        } else if (!differs(a, b)) {
            continue;
        }
        const size_t dot = k.find('.');
        const std::string s = k.substr(0, dot), p = dot == std::string::npos ? "" : k.substr(dot + 1);
        if (!was.has(k)) {
            ctx.bad("property", name + " " + k, "the baseline template has no such property (a property set cannot be added at run time)", "engine");
            continue;
        }
        if (!now.has(k)) continue;
        const std::string* typ = now.type_of(k);
        if (!typ || typ->empty()) typ = was.type_of(k);
        auto v = prop_value(ctx, k, typ, *b, name);
        if (!v) continue;
        out.sub(s).set(p, std::move(*v));
    }
    return out;
}

}  // namespace

// ---------------------------------------------------------------------------------------------- shared helpers

void Flat::put(const std::string& k, Val v, const std::string& type) {
    ix[k] = keys.size();
    keys.push_back(k);
    vals.push_back(std::move(v));
    types.push_back(type);
}

Ctx::Ctx() {
    for (const EnumRow& r : kEnumRows) enums[r.type][r.value] = r.name;
    for (const SkillRow& r : kSkillRows) {
        if (static_cast<size_t>(r.id) >= skills.size()) skills.resize(static_cast<size_t>(r.id) + 1);
        skills[static_cast<size_t>(r.id)] = r.name;
    }
}

std::string Ctx::skill(const Val* n) const {
    if (!n || !n->is_int() || n->i < 0 || static_cast<size_t>(n->i) >= skills.size()) return "";
    return skills[static_cast<size_t>(n->i)];
}

std::optional<std::string> Ctx::enum_name(const std::string& typ, const Val& v) const {
    static const std::string pre = "bTPropertyContainer<enum ";
    if (!starts_with(typ, pre)) return std::nullopt;
    size_t e = pre.size();
    while (e < typ.size() && (std::isalnum(static_cast<unsigned char>(typ[e])) || typ[e] == '_')) ++e;
    if (e == pre.size() || e >= typ.size() || typ[e] != '>') return std::nullopt;
    const std::string en = typ.substr(pre.size(), e - pre.size());
    const auto it = enums.find(en);
    if (it == enums.end() || !v.is_int()) return std::nullopt;
    const auto n = it->second.find(static_cast<int>(v.i));
    if (n == it->second.end()) return std::nullopt;
    return en + "_" + n->second;
}

void Ctx::bad(const std::string& kind, const std::string& name, const std::string& reason, const std::string& needs) {
    unconverted.push_back({kind, name, reason, needs});
}

LVal to_lval(const Val& v) {
    switch (v.k) {
        case Val::None: return LVal::nil();
        case Val::Int: return LVal::integer(v.i);
        case Val::Bool: return LVal::boolean(v.i != 0);
        case Val::Float: return LVal::real(v.d);
        case Val::Str: return LVal::str(v.s);
        case Val::Vec: {
            LVal l = LVal::list();
            for (double x : v.v) l.push(LVal::real(x));
            return l;
        }
    }
    return LVal::nil();
}

Flat flat_typed(const Template& t, const std::unordered_map<std::string, std::string>& guid) {
    Flat f;
    std::unordered_map<std::string, int> count;
    for (const Prop& p : t.props) {
        const std::string key = p.key();
        const int c = ++count[key];
        Val v = p.value;
        if (v.is_str() && starts_with(v.s, "@")) {
            const auto g = guid.find(v.s);
            if (g != guid.end()) v = Val::of_str(g->second);
        }
        f.put(c == 1 ? key : key + "#" + std::to_string(c), std::move(v), p.type);
    }
    return f;
}

std::vector<Elem> arrays_of(const Flat& P, const std::string& prefix) {
    std::vector<Elem> out;
    Elem* cur = nullptr;
    for (size_t i = 0; i < P.size(); ++i) {
        const std::string& k = P.keys[i];
        if (!starts_with(k, prefix)) continue;
        std::string f = k.substr(prefix.size());
        f = f.substr(0, f.find('#'));
        if (!cur || cur->has(f)) {
            out.emplace_back();
            cur = &out.back();
        }
        cur->f.emplace_back(f, P.vals[i]);
    }
    return out;
}

LVal recipe_lua(const RecipeInfo& r) {
    LVal out = LVal::dict(true);
    out.set("station", LVal::str(r.station.value_or("")));
    LVal ing = LVal::list();
    for (const auto& i : r.ingredients) {
        LVal d = LVal::dict();
        d.set("item", to_lval(i.first));
        d.set("count", to_lval(i.second));
        ing.push(std::move(d));
    }
    out.set("ingredients", std::move(ing));
    // skills_map: the highest amount per skill
    std::vector<std::pair<std::string, Val>> m;
    for (const auto& s : r.skills) {
        auto it = std::find_if(m.begin(), m.end(), [&](const auto& x) { return x.first == s.first; });
        if (it == m.end()) m.push_back(s);
        else if (s.second.num() > it->second.num()) it->second = s.second;
    }
    LVal sk = LVal::dict();
    for (const auto& s : m) sk.set(s.first, to_lval(s.second));
    out.set("skills", std::move(sk));
    out.set("result", to_lval(r.result));
    return out;
}

// ---------------------------------------------------------------------------------------------- existing items

ItemsResult gen_items(Ctx& ctx, Data& d) {
    ItemsResult R;
    for (const std::string& name : d.mod_sorted()) {
        const auto vit = d.van.find(name);
        if (vit == d.van.end()) continue;
        const Flat a = flat_typed(vit->second, d.guid);
        const Flat b = flat_typed(d.mod.at(name), d.guid);
        const std::string& path = d.vdir[name];
        std::vector<std::string> diffkeys;
        {
            std::vector<std::string> keys = a.keys;
            for (const std::string& k : b.keys)
                if (!a.has(k)) keys.push_back(k);
            for (const std::string& k : keys) {
                const Val* va = a.get(k);
                const Val* vb = b.get(k);
                if (!differs(va, vb)) continue;
                if (val_eq(norm_val(va), norm_val(vb))) continue;
                if (is_noise(k) || is_skip_set(k)) continue;
                diffkeys.push_back(k);
            }
        }
        if (diffkeys.empty()) {
            ctx.stats["identical (re-saved only)"]++;
            continue;
        }
        if (!starts_with(path, "Items/") || !a.has("gCItem_PS.UseType")) {
            if (starts_with(path, "Items/")) {
                Flat fa, fb;
                for (size_t i = 0; i < a.size(); ++i)
                    if (!contains(a.keys[i], "[]")) fa.put(a.keys[i], a.vals[i], a.types[i]);
                for (size_t i = 0; i < b.size(); ++i)
                    if (!contains(b.keys[i], "[]")) fb.put(b.keys[i], b.vals[i], b.types[i]);
                LVal sets = prop_diff(ctx, name, fa, fb);
                if (!sets.empty()) {
                    R.patches.set(name, std::move(sets));
                    ctx.stats["projectile-like templates patched"]++;
                }
                continue;
            }
            const std::string want = "eCAnimation_PS.ResourceFilePath";
            const bool has_want = std::find(diffkeys.begin(), diffkeys.end(), want) != diffkeys.end() && b.has(want);
            std::vector<std::string> other;
            for (const std::string& k : diffkeys)
                if (k != want) other.push_back(k);
            if (has_want) {
                LVal inner = LVal::dict();
                inner.set("ResourceFilePath", LVal::str(norm_mesh(b.get(want)->s)));
                LVal outer = LVal::dict();
                outer.set("eCAnimation_PS", std::move(inner));
                R.patches.set(name, std::move(outer));
                ctx.stats["monster actor swaps"]++;
            }
            if (!other.empty()) {
                std::sort(other.begin(), other.end());
                std::string j;
                for (size_t i = 0; i < other.size(); ++i) j += (i ? ", " : "") + other[i];
                ctx.warn.push_back(name + ": ignored changes of runtime state: " + j);
            }
            continue;
        }
        const ItemView va = item_view(ctx, a, name), vb = item_view(ctx, b, name);
        LVal entry = LVal::dict(true);
        if (vb.recipe && !va.recipe) {
            // an item that becomes a recipe (pearls, gold, ore ...): a property set cannot be added to a template at run time
            R.recipes_new.emplace_back(name, *vb.recipe);
            ctx.stats["items that became recipes"]++;
        }
        for (const char* k : {"value", "damage", "mana_cost"}) {
            const auto it = vb.scal.find(k);
            if (it == vb.scal.end()) continue;
            const auto ia = va.scal.find(k);
            const Val* old = ia == va.scal.end() ? nullptr : &ia->second;
            if (differs(&it->second, old)) {
                if (!old) ctx.bad("property", name + " " + k, "the baseline template has no such property", "engine");
                else entry.set(k, to_lval(it->second));
            }
        }
        for (const auto& kv : {std::pair<const char*, const char*>{"use_type", "gEItemUseType"}, {"category", "gEItemCategory"}}) {
            const auto it = vb.scal.find(kv.first);
            if (it == vb.scal.end()) continue;
            const auto ia = va.scal.find(kv.first);
            if (!differs(&it->second, ia == va.scal.end() ? nullptr : &ia->second)) continue;
            const auto nm = ctx.enum_name(std::string("bTPropertyContainer<enum ") + kv.second + ">", it->second);
            if (!nm) ctx.bad("property", name + " " + kv.first, "enum value " + repr_of(it->second) + " of " + kv.second + " unknown", "tool");
            else entry.set(kv.first, LVal::str(*nm));
        }
        if (!same_skills(vb.requires_, va.requires_)) entry.set("requires", req_lua(vb.requires_));
        if (!same_mods(vb.modifies, va.modifies)) entry.set("modifies", mod_lua(vb.modifies));
        LVal pd = prop_diff(ctx, name, va.props, vb.props);
        if (!pd.empty()) entry.set("properties", std::move(pd));
        if (!entry.empty()) {
            R.items.set(name, std::move(entry));
            ctx.stats["items changed"]++;
        } else {
            bool becomes = false;
            for (const auto& rn : R.recipes_new) becomes = becomes || rn.first == name;
            if (!becomes) ctx.stats["items: nothing convertible"]++;
        }
    }
    return R;
}

// ---------------------------------------------------------------------------------------------- new templates

namespace {

std::set<std::string> sets_of(const Flat& P) {
    std::set<std::string> s;
    for (const std::string& k : P.keys)
        if (!is_skip_set(k)) s.insert(set_of(k));
    return s;
}

bool unresolved(const Val& v) { return !v.is_str() || starts_with(v.s, "@"); }

std::string pick_base(const Data& d, const Flat& m, const std::vector<std::pair<std::string, Flat>>& vanitems) {
    const std::set<std::string> ms = sets_of(m);
    bool have = false;
    double best_s = 0;
    std::string best_n;
    static const char* const script_keys[] = {"gCItem_PS.CanEquipScript", "gCItem_PS.EquipScript", "gCItem_PS.UnEquipScript",
                                             "gCDamage_PS.DamageScript", "gCItem_PS.EffectMaterial", "gCDamage_PS.DamageType",
                                             "gCItem_PS.IsDangerousWeapon", "gCInteraction_PS.UseType"};
    static const std::set<std::string> skip_keys = {"gCItem_PS.GoldValue", "gCItem_PS.SortValue", "gCItem_PS.IconImage", "eCMesh_PS.MeshFileName",
                                                    "gCDamage_PS.DamageAmount"};
    for (const auto& [vn, P] : vanitems) {
        long long s = 0;
        s += 100 * (P.has("gCRecipe_PS.Craft") != m.has("gCRecipe_PS.Craft"));
        s += 50 * differs(P.get("gCItem_PS.UseType"), m.get("gCItem_PS.UseType"));
        s += 30 * differs(P.get("gCItem_PS.HoldType"), m.get("gCItem_PS.HoldType"));
        s += 20 * differs(P.get("gCItem_PS.Category"), m.get("gCItem_PS.Category"));
        const std::set<std::string> ps = sets_of(P);
        size_t sym = 0;
        for (const auto& x : ms) sym += !ps.count(x);
        for (const auto& x : ps) sym += !ms.count(x);
        s += 12 * static_cast<long long>(sym);
        for (const char* k : script_keys) s += 4 * differs(P.get(k), m.get(k));
        std::set<std::string> keys(P.keys.begin(), P.keys.end());
        keys.insert(m.keys.begin(), m.keys.end());
        for (const std::string& k : keys) {
            if (contains(k, "[]") || is_noise(k) || starts_with(k, "gCRecipe_PS.") || is_skip_set(k)) continue;
            if (skip_keys.count(k)) continue;
            s += differs(P.get(k), m.get(k));
        }
        const double sc = static_cast<double>(s) + 0.001 * static_cast<double>(vn.size());
        if (!have || sc < best_s || (sc == best_s && vn < best_n)) {
            have = true;
            best_s = sc;
            best_n = vn;
        }
    }
    (void)d;
    return best_n;
}

}  // namespace

NewResult gen_new(Ctx& ctx, Data& d) {
    NewResult R;
    std::vector<std::pair<std::string, Flat>> vanitems;
    for (const std::string& vn : d.van_order) {
        if (!starts_with(d.vdir[vn], "Items/")) continue;
        Flat P = flat_typed(d.van[vn], d.guid);
        if (P.has("gCItem_PS.UseType")) vanitems.emplace_back(vn, std::move(P));
    }
    const auto known = [&](const std::string& n) { return d.mod.count(n) || d.van.count(n); };
    const std::map<std::string, std::pair<std::string, std::string>>& result_fix = ctx.profile.recipe_result_fix;
    for (const std::string& name : d.mod_sorted()) {
        if (d.van.count(name)) continue;
        const Flat m = flat_typed(d.mod.at(name), d.guid);
        if (!m.has("gCItem_PS.UseType")) {
            ctx.bad("template", name, "not an item template (new NPCs / other templates are not converted by the tool)", "engine");
            continue;
        }
        const std::string base = pick_base(d, m, vanitems);
        if (base.empty()) {
            ctx.bad("template", name, "no shipped item template to clone", "engine");
            continue;
        }
        const Flat bP = flat_typed(d.van[base], d.guid);
        const ItemView mv = item_view(ctx, m, name), bv = item_view(ctx, bP, base);
        LVal e = LVal::dict(true);
        e.set("base", LVal::str(base));
        for (const char* k : {"value", "damage", "mana_cost"}) {
            const auto it = mv.scal.find(k);
            if (it == mv.scal.end()) continue;
            if (bv.scal.count(k)) e.set(k, to_lval(it->second));
            else ctx.bad("property", name + " " + k, "the best base " + base + " has no such property", "engine");
        }
        for (const auto& kv : {std::pair<const char*, const char*>{"use_type", "gEItemUseType"}, {"category", "gEItemCategory"}}) {
            const auto it = mv.scal.find(kv.first);
            if (it == mv.scal.end()) continue;
            const auto ib = bv.scal.find(kv.first);
            if (!differs(&it->second, ib == bv.scal.end() ? nullptr : &ib->second)) continue;
            const auto nm = ctx.enum_name(std::string("bTPropertyContainer<enum ") + kv.second + ">", it->second);
            if (!nm) ctx.bad("property", name + " " + kv.first, "enum value " + repr_of(it->second) + " of " + kv.second + " unknown", "tool");
            else e.set(kv.first, LVal::str(*nm));
        }
        if (!mv.requires_.empty() || !bv.requires_.empty()) e.set("requires", req_lua(mv.requires_));
        if (!mv.modifies.empty() || !bv.modifies.empty()) e.set("modifies", mod_lua(mv.modifies));
        LVal props = LVal::dict(true);
        const LVal pd = prop_diff(ctx, name, bv.props, mv.props);
        for (const auto& [s, p] : pd.fields) {
            for (const auto& [pn, v] : p.fields) {
                if (s == "eCMesh_PS" && pn == "MeshFileName") e.set("mesh", v);
                else if (s == "gCItem_PS" && pn == "IconImage") e.set("icon", v);
                else if (s == "eCMesh_PS" && pn == "MaterialSwitch") e.set("material", v);
                else props.sub(s).set(pn, v);
            }
        }
        if (!props.empty()) e.set("properties", std::move(props));
        R.items.set(name, std::move(e));
        ctx.stats["new items"]++;
        if (mv.recipe) {
            RecipeInfo r = *mv.recipe;
            if (!r.station) {
                ctx.bad("recipe", name, "recipe station number " + repr_of(r.craft) + " unknown", "tool");
                continue;
            }
            if (!(r.amount.is_num() && r.amount.num() == 1))
                ctx.warn.push_back(name + ": ResultAmount " + r.amount.repr() + " (the engine's craft makes one result)");
            const auto fix = result_fix.find(name);
            if (fix != result_fix.end() && unresolved(r.result) && known(fix->second.first)) {
                ctx.warn.push_back(name + ": the mod's recipe produces a template that is in neither archive (its own error); chained to " + fix->second.first +
                                   " instead (" + fix->second.second + ")");
                r.result = Val::of_str(fix->second.first);
            }
            const auto ifix = ctx.profile.recipe_ingredient_fix.find(name);
            if (ifix != ctx.profile.recipe_ingredient_fix.end()) {
                bool any = false;
                for (const auto& i : r.ingredients) any = any || unresolved(i.first);
                if (any) {
                    ctx.warn.push_back(name + ": the mod's recipe consumes a template that is in neither archive (its own error); it consumes " + ifix->second +
                                       " instead (the stage itself, like every other stage)");
                    for (auto& i : r.ingredients)
                        if (unresolved(i.first)) i.first = Val::of_str(ifix->second);
                }
            }
            std::string un;
            for (const auto& i : r.ingredients)
                if (unresolved(i.first)) un += (un.empty() ? "" : ", ") + repr_of(i.first);
            if (unresolved(r.result)) un += (un.empty() ? "" : ", ") + repr_of(r.result);
            if (!un.empty()) {
                ctx.bad("recipe", name, "ingredient or result unresolved: [" + un + "]", "author");
                continue;
            }
            R.recipes.set(name, recipe_lua(r));
            ctx.stats["new recipe items"]++;
        }
    }
    return R;
}

}  // namespace p2m
