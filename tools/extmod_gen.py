#!/usr/bin/env python3
"""Generator for the Lua mod "risen_extended": converts the data of a Risen mod that ships .pNN patch archives
(the German "Risen Extended Mod", docs/EXTENDED_MOD_ANALYSIS.md) into a mod for this layer.

    python -I tools/extmod_gen.py --mod <folder with the .pNN files> --baseline <Risen data dir> --out mods/risen_extended

* The mod is read as DATA only (nothing in it is executed); the baseline is the game's own data directory (common/*.pak, compiled/*.pak), read only.
* Every number, name and text of the output comes from the archives (diff against the baseline); nothing is typed in by hand. The generic Lua code
  (main.lua, check.lua) is copied from tools/extmod_templates/.
* The output is a derived work of the mod's author: it is for local play only and must never be committed or redistributed
  (the output folder gets a .gitignore and a NOTICE.txt; mods/risen_extended/ is git-ignored in this repository).
* Deterministic: the same inputs give byte-identical files (sorted keys, no time stamps).

Output (<out>):
  mod.toml, main.lua, check.lua, config.lua      the mod (generic code from tools/extmod_templates)
  data/items.lua         existing items the mod changed: name -> { value, damage, mana_cost, requires, modifies, use_type, category, properties }
  data/new_items.lua     templates the mod adds: name -> { base, value, damage, requires, modifies, mesh, icon, material, properties, ... }
  data/recipes.lua       recipe edits of the new recipe items + new recipes for items that become recipes (pearls, gold, ...)
  data/patches.lua       edits of other templates (monster actors, light range)
  strings/*.csv          item / focus / document texts (the languages the mod has; all.csv = English fallback)
  assets/                loose resource files (meshes, materials, textures, actors) with their compiled names
  manifest.json          what was generated, skipped, and what could not be converted (with the reason and the milestone that would)
"""
import argparse
import collections
import hashlib
import json
import os
import re
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import extmod_diff as X  # noqa: E402
import g3pak  # noqa: E402
import g3tab  # noqa: E402
import g3tple  # noqa: E402

NOISE = X.NOISE + ("eCCollisionShape_PS.", "gCNPC_PS.", "gCInteraction.")
SKIP_SETS = ("eCCollisionShape", "eCCollisionShape_PS", "eCRigidBody_PS", "gCNavigation_PS", "gCScriptRoutine_PS", "gCInventory_PS", "gCNPC_PS")

# numeric enum values (docs/RE.md sections 23 and 43; the rest comes from tools/extmod_enums.json when present)
ENUMS = {
    "gEItemUseType": {0: "None", 1: "1H", 2: "2H", 3: "BS", 4: "Arrow", 5: "Bow", 6: "CrossBow", 7: "Bolt", 9: "Shield", 10: "Armor", 11: "Helmet",
                      12: "Staff", 13: "Amulet", 14: "Ring", 18: "Axe", 19: "Cast"},
    "gEDamageType": {0: "None", 1: "Edge", 2: "Blunt", 3: "Point", 4: "Fire", 5: "Ice", 6: "Magic"},
    "gEItemCategory": {0: "None", 1: "Weapon", 2: "Armor", 3: "Consumable", 5: "Magic", 6: "Misc", 7: "Written"},
    "gEInteractionUseType": {0: "None", 1: "Item"},
}
STATIONS = {0: "alchemy", 3: "frying", 4: "goldsmith", 5: "scroll", 6: "forge", 7: "anvil", 8: "coolweapon", 9: "grindstone"}
MODES = {0: "add", 1: "set", 2: "max", 3: "percent"}
LANG_OF = {"German_Text": "German", "English_Text": "English", "French_Text": "French", "Italian_Text": "Italian", "Spanish_Text": "Spanish"}
PLACEHOLDERS = {"", "-", "\u2026", "\ufffd", "\x85", "...", "?"}
ASSET_EXT = re.compile(r"\._x[a-z]+$", re.I)
SKILL_NAMES = X.SKILLS

# ---------------------------------------------------------------------------------------------------------------- helpers


def lua_key(k):
    return k if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", k) and k not in ("end", "and", "or", "not", "if", "then", "else", "for", "in", "do", "nil", "true", "false", "function", "local", "return", "repeat", "until", "while", "break", "elseif") else '["%s"]' % k


def lua(v, ind=0):
    pad = "    " * ind
    if v is None:
        return "nil"
    if isinstance(v, bool):
        return "true" if v else "false"
    if isinstance(v, int):
        return str(v)
    if isinstance(v, float):
        return str(int(v)) if v == int(v) and abs(v) < 1e15 else "%.9g" % v
    if isinstance(v, str):
        return '"%s"' % v.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n").replace("\r", "\\r")
    if isinstance(v, (list, tuple)):
        if not v:
            return "{}"
        if all(not isinstance(x, (dict, list, tuple)) for x in v):
            return "{ " + ", ".join(lua(x) for x in v) + " }"
        return "{\n" + "".join(pad + "    " + lua(x, ind + 1) + ",\n" for x in v) + pad + "}"
    if isinstance(v, dict):
        if not v:
            return "{}"
        simple = all(not isinstance(x, (dict, list, tuple)) for x in v.values())
        keys = sorted(v) if not isinstance(v, collections.OrderedDict) else list(v)
        if simple and len(keys) <= 6:
            return "{ " + ", ".join("%s = %s" % (lua_key(k), lua(v[k])) for k in keys) + " }"
        return "{\n" + "".join(pad + "    %s = %s,\n" % (lua_key(k), lua(v[k], ind + 1)) for k in keys) + pad + "}"
    raise TypeError(type(v))


def write(path, text, newline="\n"):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf8", newline=newline) as f:
        f.write(text)


def norm_mesh(s):
    """`Item_Pearl._xmsh` (compiled spelling used by some templates of the mod) and `Item_Pearl.xmsh` name the same resource."""
    return re.sub(r"\._(x[a-z]+)$", r".\1", s) if isinstance(s, str) else s


def vec_text(t):
    return "; ".join(("%g" % x) for x in t)


# ---------------------------------------------------------------------------------------------------------------- loading


class Data:
    pass


def load(modroot, baseline):
    d = Data()
    d.base = X.load_baseline(baseline)
    groups = X.archive_groups(modroot)
    d.layers = {"common": X.Layered(), "compiled": X.Layered()}
    flat_list = sorted(((n, kind, name, f) for (kind, name), lst in groups.items() for n, f in lst), key=lambda x: (x[0], x[1], x[2]))
    for n, kind, name, f in flat_list:
        if kind in d.layers:
            d.layers[kind].add("%s/%s.p%02d" % (kind, name, n), g3pak.Pak(f))
    d.van, d.vdir = {}, {}
    for k, (label, pak, e) in d.base["common"].files.items():
        if k.endswith(".tple"):
            t = g3tple.parse(pak.read(e))
            d.van[t.name[:-5]] = t
            d.vdir[t.name[:-5]] = e.path
    d.mod, d.mlabel = {}, {}
    for k, (label, pak, e) in d.layers["common"].files.items():
        if k.endswith(".tple"):
            t = g3tple.parse(pak.read(e))
            d.mod[t.name[:-5]] = t
            d.mlabel[t.name[:-5]] = label
    d.guid = {}
    for t in list(d.van.values()):
        for g in t.guids:
            d.guid.setdefault(g, t.name[:-5])
    for t in d.mod.values():
        for g in t.guids:
            d.guid[g] = t.name[:-5]
    return d


def flat_typed(t, guid):
    """({key: value}, {key: type}) of a template; repeated keys get #2, #3; GUID references are replaced by template names."""
    vals, types, c = collections.OrderedDict(), {}, collections.Counter()
    for p in t.props:
        c[p.key] += 1
        k = p.key if c[p.key] == 1 else "%s#%d" % (p.key, c[p.key])
        v = p.value
        if isinstance(v, str) and v.startswith("@"):
            v = guid.get(v, v)
        vals[k] = v
        types[k] = p.type
    return vals, types


def arrays_of(P, prefix):
    return X.array(P, prefix)


class Ctx:
    def __init__(self, enums_extra):
        self.warn = []
        self.unconverted = []
        self.enums = {k: dict(v) for k, v in ENUMS.items()}
        for k, v in (enums_extra or {}).get("enums", {}).items():
            self.enums.setdefault(k, {}).update({int(n): s for n, s in v.items()})
        self.skills = list(SKILL_NAMES)
        for n, s in ((enums_extra or {}).get("skills") or {}).items():
            n = int(n)
            while len(self.skills) <= n:
                self.skills.append(None)
            self.skills[n] = s

    def skill(self, n):
        if isinstance(n, int) and 0 <= n < len(self.skills) and self.skills[n]:
            return self.skills[n]
        return None

    def bad(self, kind, name, reason, needs="M19"):
        self.unconverted.append({"kind": kind, "name": name, "reason": reason, "needs": needs})

    def enum(self, typ, n):
        m = re.match(r"bTPropertyContainer<enum (\w+)>", typ or "")
        if not m:
            return None
        names = self.enums.get(m.group(1), {})
        if n in names:
            return "%s_%s" % (m.group(1), names[n])
        return None


def prop_value(ctx, key, typ, v, owner):
    """a property value in the form `risen.template.patch` takes; None (and an `unconverted` note) when unknown"""
    if typ and typ.startswith("bTPropertyContainer<enum"):
        s = ctx.enum(typ, v)
        if s is None:
            ctx.bad("property", "%s %s" % (owner, key), "enum value %r of %s has no known name (tools/extmod_enums.json)" % (v, typ), "M19")
        return s
    if typ == "bCVector" or (isinstance(v, tuple) and typ in ("bCVector", "bCFloatColor", "bCRange1", "bCRange3", "bCVector2")):
        return vec_text(v)
    if isinstance(v, tuple):
        return vec_text(v)
    if key.endswith("MeshFileName"):
        return norm_mesh(v)
    if isinstance(v, str) and v.startswith("@"):
        ctx.bad("property", "%s %s" % (owner, key), "reference to an unknown template (%s)" % v, "author")
        return None
    return v


# ---------------------------------------------------------------------------------------------------------------- items


def item_view(ctx, P, owner):
    """{value, damage, mana_cost, use_type, category, requires[], modifies[], recipe{}, props{}} of a flat property dict"""
    out = {"props": {}}
    scalar = {"gCItem_PS.GoldValue": "value", "gCDamage_PS.DamageAmount": "damage", "gCCastInfo_PS.ManaCost": "mana_cost",
              "gCItem_PS.UseType": "use_type", "gCItem_PS.Category": "category"}
    for k, v in P.items():
        if k in scalar:
            out[scalar[k]] = v
        elif "[]" in k or k.startswith("gCRecipe_PS."):
            continue
        elif k.startswith(NOISE) or k.split(".")[0] in SKIP_SETS or "#" in k:
            continue
        else:
            out["props"][k] = v
    req = []
    for e in arrays_of(P, "gCItem_PS.RequiredSkills[]."):
        s = ctx.skill(e.get("Skill"))
        if s is None:
            ctx.bad("skill", owner, "required skill number %r has no name" % (e.get("Skill"),), "M19")
            continue
        req.append((s, e.get("Amount", 0)))
    mods = []
    for e in arrays_of(P, "gCItem_PS.ModifySkills[]."):
        s = ctx.skill(e.get("Skill"))
        if s is None:
            ctx.bad("skill", owner, "modified skill number %r has no name" % (e.get("Skill"),), "M19")
            continue
        m = MODES.get(e.get("Modifier", 0))
        if m is None:
            ctx.bad("skill", owner, "modifier mode %r unknown" % (e.get("Modifier"),), "M19")
            m = "add"
        mods.append((s, e.get("Amount", 0), m))
    out["requires"], out["modifies"] = req, mods
    if "gCRecipe_PS.Craft" in P:
        ing = []
        for e in arrays_of(P, "gCRecipe_PS.CraftIngredients[]."):
            ing.append((e.get("ItemTemplate"), e.get("ItemAmount", 1)))
        rs = []
        for e in arrays_of(P, "gCRecipe_PS.RequiredSkills[]."):
            s = ctx.skill(e.get("Skill"))
            if s:
                rs.append((s, e.get("Amount", 0)))
        out["recipe"] = {"station": STATIONS.get(P["gCRecipe_PS.Craft"]), "craft": P["gCRecipe_PS.Craft"], "ingredients": ing, "skills": rs,
                         "result": P.get("gCRecipe_PS.ResultItem"), "amount": P.get("gCRecipe_PS.ResultAmount", 1)}
    return out


def skills_map(lst):
    out = collections.OrderedDict()
    for s, a in lst:
        out[s] = max(out.get(s, a), a)
    return out


def req_lua(lst):
    return [{"skill": s, "amount": a} for s, a in lst]


def mod_lua(lst):
    out = []
    for s, a, m in lst:
        e = {"skill": s, "amount": a}
        if m != "add":
            e["mode"] = m
        out.append(e)
    return out


def prop_diff(ctx, name, was_p, now_p, was_t, now_t, skip_keys=()):
    """{set: {prop: value}} of properties that differ (both sides present: a set the baseline lacks cannot be added)"""
    out = collections.OrderedDict()
    for k in sorted(set(was_p) | set(now_p)):
        if k in skip_keys:
            continue
        a, b = was_p.get(k), now_p.get(k)
        if k.endswith("MeshFileName"):
            a, b = norm_mesh(a), norm_mesh(b)
        if a == b:
            continue
        s, p = k.split(".", 1)
        if k not in was_p:
            ctx.bad("property", "%s %s" % (name, k), "the baseline template has no such property (a property set cannot be added at run time)", "engine")
            continue
        if k not in now_p:
            continue
        v = prop_value(ctx, k, now_t.get(k) or was_t.get(k), b, name)
        if v is None:
            continue
        out.setdefault(s, {})[p] = v
    return out


def gen_items(ctx, d):
    """existing templates the mod changed"""
    items, patches, recipes_new, stats = collections.OrderedDict(), collections.OrderedDict(), collections.OrderedDict(), collections.Counter()
    for name in sorted(d.mod):
        if name not in d.van:
            continue
        a, at = flat_typed(d.van[name], d.guid)
        b, bt = flat_typed(d.mod[name], d.guid)
        path = d.vdir[name]
        diffkeys = [k for k in set(a) | set(b) if a.get(k) != b.get(k) and norm_mesh(a.get(k)) != norm_mesh(b.get(k))
                    and not k.startswith(NOISE) and k.split(".")[0] not in SKIP_SETS]
        if not diffkeys:
            stats["identical (re-saved only)"] += 1
            continue
        if not path.startswith("Items/") or "gCItem_PS.UseType" not in a:
            # monsters: the actor file is the only thing that is data (the rest is runtime state of the saved template);
            # item-like templates without gCItem_PS (projectiles): every scalar property that differs
            if path.startswith("Items/"):
                sets = prop_diff(ctx, name, {k: v for k, v in a.items() if "[]" not in k}, {k: v for k, v in b.items() if "[]" not in k}, at, bt)
                if sets:
                    patches[name] = sets
                    stats["projectile-like templates patched"] += 1
                continue
            want = {k: b[k] for k in diffkeys if k == "eCAnimation_PS.ResourceFilePath"}
            other = [k for k in diffkeys if k not in want]
            if want:
                patches[name] = {"eCAnimation_PS": {"ResourceFilePath": norm_mesh(b["eCAnimation_PS.ResourceFilePath"])}}
                stats["monster actor swaps"] += 1
            if other:
                ctx.warn.append("%s: ignored changes of runtime state: %s" % (name, ", ".join(sorted(other))))
            continue
        va, vb = item_view(ctx, a, name), item_view(ctx, b, name)
        entry = collections.OrderedDict()
        if "recipe" in vb and "recipe" not in va:
            # an item that becomes a recipe (pearls, gold, ore ...): a property set cannot be added to a template at run time
            r = vb["recipe"]
            recipes_new[name] = r
            stats["items that became recipes"] += 1
        for k in ("value", "damage", "mana_cost"):
            if k in vb and vb.get(k) != va.get(k):
                if k not in va:
                    ctx.bad("property", "%s %s" % (name, k), "the baseline template has no such property", "engine")
                else:
                    entry[k] = vb[k]
        for k, en in (("use_type", "gEItemUseType"), ("category", "gEItemCategory")):
            if k in vb and vb.get(k) != va.get(k):
                nm = ctx.enums.get(en, {}).get(vb[k])
                if nm is None:
                    ctx.bad("property", "%s %s" % (name, k), "enum value %r of %s unknown" % (vb[k], en), "M19")
                else:
                    entry[k] = "%s_%s" % (en, nm)
        if vb["requires"] != va["requires"]:
            entry["requires"] = req_lua(vb["requires"])
        if vb["modifies"] != va["modifies"]:
            entry["modifies"] = mod_lua(vb["modifies"])
        pd = prop_diff(ctx, name, va["props"], vb["props"], at, bt)
        if pd:
            entry["properties"] = pd
        if entry:
            items[name] = entry
            stats["items changed"] += 1
        elif name not in recipes_new:
            stats["items: nothing convertible"] += 1
    return items, patches, recipes_new, stats


# ---------------------------------------------------------------------------------------------------------------- new templates


def pick_base(ctx, d, name, m, mt, vanitems):
    """the shipped item template that is most like the new one (so only a few properties differ)"""
    mv = item_view(ctx, m, name)

    def sets(P):
        return {k.split(".")[0] for k in P if k.split(".")[0] not in SKIP_SETS}

    ms = sets(m)
    best = None
    for vn, (P, view) in vanitems.items():
        s = 0
        s += 100 * (("gCRecipe_PS.Craft" in P) != ("gCRecipe_PS.Craft" in m))
        s += 50 * (P.get("gCItem_PS.UseType") != m.get("gCItem_PS.UseType"))
        s += 30 * (P.get("gCItem_PS.HoldType") != m.get("gCItem_PS.HoldType"))
        s += 20 * (P.get("gCItem_PS.Category") != m.get("gCItem_PS.Category"))
        s += 12 * len(ms ^ sets(P))
        for k in ("gCItem_PS.CanEquipScript", "gCItem_PS.EquipScript", "gCItem_PS.UnEquipScript", "gCDamage_PS.DamageScript", "gCItem_PS.EffectMaterial",
                  "gCDamage_PS.DamageType", "gCItem_PS.IsDangerousWeapon", "gCInteraction_PS.UseType"):
            s += 4 * (P.get(k) != m.get(k))
        for k in set(P) | set(m):
            if "[]" in k or k.startswith(NOISE) or k.startswith("gCRecipe_PS.") or k.split(".")[0] in SKIP_SETS:
                continue
            if k in ("gCItem_PS.GoldValue", "gCItem_PS.SortValue", "gCItem_PS.IconImage", "eCMesh_PS.MeshFileName", "gCDamage_PS.DamageAmount"):
                continue
            s += (P.get(k) != m.get(k))
        s += 0.001 * len(vn)  # deterministic tie-break
        if best is None or s < best[0] or (s == best[0] and vn < best[1]):
            best = (s, vn)
    return best[1]


def gen_new(ctx, d):
    new_items, recipes, stats = collections.OrderedDict(), collections.OrderedDict(), collections.Counter()
    vanitems = {}
    for vn, t in d.van.items():
        if d.vdir[vn].startswith("Items/"):
            P, _ = flat_typed(t, d.guid)
            if "gCItem_PS.UseType" in P:
                vanitems[vn] = (P, None)
    for name in sorted(d.mod):
        if name in d.van:
            continue
        m, mt = flat_typed(d.mod[name], d.guid)
        if "gCItem_PS.UseType" not in m:
            ctx.bad("template", name, "not an item template (new NPCs / other templates are not part of this mod)", "engine")
            continue
        base = pick_base(ctx, d, name, m, mt, vanitems)
        bP, _ = flat_typed(d.van[base], d.guid)
        _, bt = flat_typed(d.van[base], d.guid)
        mv, bv = item_view(ctx, m, name), item_view(ctx, bP, base)
        e = collections.OrderedDict()
        e["base"] = base
        for k in ("value", "damage", "mana_cost"):
            if k in mv:
                if k in bv:
                    e[k] = mv[k]
                else:
                    ctx.bad("property", "%s %s" % (name, k), "the best base %s has no such property" % base, "engine")
        for k, en in (("use_type", "gEItemUseType"), ("category", "gEItemCategory")):
            if k in mv and mv.get(k) != bv.get(k):
                nm = ctx.enums.get(en, {}).get(mv[k])
                if nm is None:
                    ctx.bad("property", "%s %s" % (name, k), "enum value %r of %s unknown" % (mv[k], en), "M19")
                else:
                    e[k] = "%s_%s" % (en, nm)
        if mv["requires"] or bv["requires"]:
            e["requires"] = req_lua(mv["requires"])
        if mv["modifies"] or bv["modifies"]:
            e["modifies"] = mod_lua(mv["modifies"])
        props = collections.OrderedDict()
        pd = prop_diff(ctx, name, bv["props"], mv["props"], bt, mt)
        for s, p in pd.items():
            for pn, v in p.items():
                if (s, pn) == ("eCMesh_PS", "MeshFileName"):
                    e["mesh"] = v
                elif (s, pn) == ("gCItem_PS", "IconImage"):
                    e["icon"] = v
                elif (s, pn) == ("eCMesh_PS", "MaterialSwitch"):
                    e["material"] = v
                else:
                    props.setdefault(s, {})[pn] = v
        if props:
            e["properties"] = props
        new_items[name] = e
        stats["new items"] += 1
        if "recipe" in mv:
            r = mv["recipe"]
            station = r["station"]
            if station is None:
                ctx.bad("recipe", name, "recipe station number %r unknown" % (r["craft"],), "M19")
                continue
            if r["amount"] != 1:
                ctx.warn.append("%s: ResultAmount %s (the engine's craft makes one result)" % (name, r["amount"]))
            unresolved = [i for i, _ in r["ingredients"] if not isinstance(i, str) or i.startswith("@")] + ([r["result"]] if not isinstance(r["result"], str) or r["result"].startswith("@") else [])
            if unresolved:
                ctx.bad("recipe", name, "ingredient or result unresolved: %s" % unresolved, "author")
                continue
            recipes[name] = recipe_lua(r)
            stats["new recipe items"] += 1
    return new_items, recipes, stats


def recipe_lua(r):
    ing = collections.OrderedDict()
    lst = []
    for i, c in r["ingredients"]:
        lst.append({"item": i, "count": c})
    return collections.OrderedDict([("station", r["station"]), ("ingredients", lst), ("skills", dict(skills_map(r["skills"]))), ("result", r["result"])])


# ---------------------------------------------------------------------------------------------------------------- strings


def norm_cell(s):
    s = X.norm_text(s)
    if "''" in s:   # a spreadsheet artefact: "Aric''s sword'" for "Aric's sword" (a lost leading quote, the rest doubled)
        s = s.replace("''", "'")
        if s.endswith("'") and not s.startswith("'"):
            s = s[:-1]
    elif s.startswith("'") and s.count("'") == 1:
        s = s[1:]
    return None if s in PLACEHOLDERS else s


def csv_field(s):
    if s == "" or any(c in s for c in ';"\r\n#') or s != s.strip():
        return '"%s"' % s.replace('"', '""')
    return s


def gen_strings(ctx, d, out, wanted_ids, melee=False):
    """new rows of items / focus / documents and the changed rows of the templates the mod touches -> strings/*.csv"""
    vt = {}
    for k, (label, pak, e) in d.base["compiled"].files.items():
        if k.endswith(".tab"):
            vt[k] = g3tab.read(pak.read(e))
    rows = {lang: collections.OrderedDict() for lang in LANG_OF.values()}
    stats = collections.Counter()
    for k, (label, pak, e) in sorted(d.layers["compiled"].files.items()):
        if k not in ("items.tab", "focus.tab", "documents.tab") and not (k == "skills.tab" and melee):
            if k.endswith(".tab"):
                ctx.bad("strings", k, "not applied: %s (skill names / hud texts belong to the melee system and are applied with it)" % k, "M21")
            continue
        cols, mr = g3tab.read(pak.read(e))
        vc, vr = vt.get(k, ([], {}))
        for rid, cells in mr.items():
            isnew = rid not in vr
            if not isnew and not (rid.startswith(("FO_", "ITEMDESC_")) and rid.split("_", 1)[1] in wanted_ids) and not (k == "skills.tab" and melee):
                continue
            for col, text in cells.items():
                lang = LANG_OF.get(col)
                if lang is None:
                    continue
                t = norm_cell(text)
                if t is None:
                    continue
                if not isnew:
                    old = norm_cell(vr[rid].get(col, ""))
                    if old == t:
                        continue
                rows[lang][rid] = t
                stats["%s %s rows" % (k, "new" if isnew else "changed")] += 1 if lang == "English" else 0
    # fallback for every other language: the English text; specific languages override only where the mod has a real translation
    sdir = os.path.join(out, "strings")
    os.makedirs(sdir, exist_ok=True)
    english = rows["English"]
    write(os.path.join(sdir, "all.csv"), "# generated from the mod's string tables (English); other languages override\nid;text\n" +
          "".join("%s;%s\n" % (i, csv_field(t)) for i, t in sorted(english.items())), "\r\n")
    for lang in ("German", "French", "Italian", "Spanish"):
        r = rows[lang]
        write(os.path.join(sdir, lang + ".csv"), "id;text\n" + "".join("%s;%s\n" % (i, csv_field(t)) for i, t in sorted(r.items())), "\r\n")
        stats["strings " + lang] = len(r)
    stats["strings English"] = len(english)
    return stats


# ---------------------------------------------------------------------------------------------------------------- assets


def gen_assets(ctx, d, out, copy):
    adir = os.path.join(out, "assets")
    if copy:
        shutil.rmtree(adir, ignore_errors=True)
        os.makedirs(adir, exist_ok=True)
    names, replaced, new, same, dup = {}, [], [], 0, []
    total = 0
    for kind in ("common", "compiled"):
        L = d.layers[kind]
        for key in sorted(L.files):
            label, pak, e = L.files[key]
            bare = os.path.basename(e.path)
            if not ASSET_EXT.search(bare):
                continue
            data = pak.read(e)
            bk = d.base[kind].files.get(key)
            if bk is not None and bk[1].read(bk[2]) == data:
                same += 1
                continue
            if bare.lower() in names:
                dup.append(bare)
            names[bare.lower()] = bare
            (replaced if bk is not None else new).append(bare)
            total += len(data)
            if copy:
                with open(os.path.join(adir, bare), "wb") as f:
                    f.write(data)
    return {"new": sorted(new), "replaced": sorted(replaced), "identical_skipped": same, "bytes": total, "duplicates": sorted(dup)}



# ---------------------------------------------------------------------------------------------------------------- dialogues and world (phase B)



def resolve_name(ctx, known, item, where):
    """case-insensitive template name; a name the mod misspelled is mapped when exactly one template fits (reported as a warning)"""
    low = str(item).lower()
    if low in known:
        return known[low]
    cands = []
    if low.endswith("_01") and low[:-3] in known:
        cands = [known[low[:-3]]]
    if not cands:
        cands = sorted({v for k, v in known.items() if k.startswith(low)})
    if not cands:
        tail = low.split("_")[-1]
        cands = sorted({v for k, v in known.items() if k.split("_")[:2] == low.split("_")[:2] and k.endswith(tail) and len(tail) > 6})
    if len(cands) == 1:
        ctx.warn.append("%s: '%s' is not a template; assumed to mean '%s' (the mod's own typo)" % (where, item, cands[0]))
        return cands[0]
    return None


def gen_dialog(ctx, d):
    """XP rewards and trader refills the mod added to existing dialogue infos (compiled_infos.bin of the newest library volume)"""
    import tempfile
    tmp = tempfile.mkdtemp(prefix="extmod_infos_")
    try:
        res = X.infos_stage(d.base, d.layers, tmp, {})
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    if not res:
        return {"xp": {"add": [], "change": []}, "trade": []}
    label = sorted(res)[-1]
    r = res[label]
    known = {n.lower(): n for n in list(d.van) + list(d.mod)}
    amt = re.compile(r"([+-])\('gCInfoCommandGiveXP\.Amount', (\d+)\)")
    add, change = [], []
    for x in sorted(r["xp_changes"], key=lambda x: x["info"]):
        plus = [int(m.group(2)) for l in x["diff"] for m in [amt.match(l)] if m and m.group(1) == "+"]
        minus = [int(m.group(2)) for l in x["diff"] for m in [amt.match(l)] if m and m.group(1) == "-"]
        if len(plus) == 1 and not minus:
            add.append({"info": x["info"], "amount": plus[0], "owner": x["owner"]})
        elif len(plus) == 1 and len(minus) == 1:
            change.append({"info": x["info"], "old": minus[0], "new": plus[0], "owner": x["owner"]})
        else:
            ctx.bad("dialogue", x["info"], "XP change not understood: %s" % x["diff"], "M20")
    trade = []
    for info in sorted(r["trade_refills"]):
        seen, entries = set(), []
        for trader, item, count in r["trade_refills"][info]:
            real = resolve_name(ctx, known, item, "trade " + trader)
            if real is None:
                ctx.bad("trade", "%s %s" % (trader, item), "no template of that name in the mod or the game (trader entry skipped)", "author")
                continue
            if (trader, real) in seen:
                continue
            seen.add((trader, real))
            entries.append({"trader": trader, "item": real, "count": int(count)})
        trade.append({"info": info, "entries": entries})
    return {"xp": {"add": add, "change": change}, "trade": trade, "layer": label}


def gen_world(ctx, d, research):
    """containers that got items, NPCs that got equipment, teacher limits: from the world-file analysis (RESEARCH_teachers_data.json, derived from the archives)"""
    if not research or not os.path.exists(research):
        ctx.warn.append("no world research file: containers / equipment / teacher limits skipped")
        return {"containers": [], "equipment": [], "teachers": []}
    j = json.load(open(research, encoding="utf8"))
    known = {n.lower(): n for n in list(d.van) + list(d.mod)}
    containers = []
    for c in j.get("container_additions", []):
        items = []
        for a in c["added"]:
            real = resolve_name(ctx, known, a["item"], "container " + c["name"])
            if real is None:
                ctx.bad("container", c["name"], "item %s unknown" % a["item"], "author")
                continue
            n = a["count"]
            for r in c.get("removed", []):
                if r["item"] == a["item"]:
                    n -= r["count"]
            if n > 0:
                items.append({"item": real, "count": n})
        if items:
            containers.append({"id": c["entity_id"], "name": c["name"], "layer": os.path.basename(c["layer"]), "items": items})
    equipment = []
    for e in j.get("npc_equipment_with_mod_items", []):
        for a in e["added"]:
            real = known.get(a["item"].lower())
            if real:
                equipment.append({"id": e["entity_id"], "name": e["name"], "item": real})
    teachers = [{"id": t["entity_id"], "name": t["name"], "skill": t["skill"], "max": t["new_max"], "old": t["old_max"]} for t in j.get("teacher_limit_changes", [])]
    return {"containers": containers, "equipment": equipment, "teachers": teachers}



# ---------------------------------------------------------------------------------------------------------------- combat table (phase C)


def _canon(x):
    """a parsed combat table without the bookkeeping of the reader (end offsets, encodings) so that equal content compares equal"""
    if isinstance(x, dict):
        return {k: _canon(v) for k, v in x.items() if k not in ("end", "enc", "acc")}
    if isinstance(x, (list, tuple)):
        return [_canon(v) for v in x]
    return x


def gen_combat(ctx, d, out, mode):
    """a patched copy of the user's own compiled_combatspecies.bin (the melee system of the mod), written as a loose asset: the engine finds the file by
    its bare name in the flat name cache, a mounted directory shadows library.pak (docs/RE.md section 26 / 45). Everything that differs between the mod's
    table and the baseline's is copied species by species (styles, moves, fight AIs, species properties like JumpBack); the rest stays the baseline's."""
    if mode == "off":
        return None
    import copy
    import g3cs
    bk = d.base["compiled"].files.get("compiled_combatspecies.bin")
    hist = d.layers["compiled"].history.get("compiled_combatspecies.bin", [])
    if not bk or not hist:
        ctx.warn.append("no combat table in the baseline or the mod")
        return None
    pick = [h for h in hist if ("library.%s" % mode) in h[0]]
    if not pick:
        ctx.warn.append("the mod has no combat table volume library.%s" % mode)
        return None
    base = g3cs.parse_bytes(bk[1].read(bk[2]))
    mod = g3cs.parse_bytes(pick[-1][1].read(pick[-1][2]))
    res = copy.deepcopy(base)
    mod_by = {sp["name"]: sp for sp in mod["species"]}
    changed = collections.OrderedDict()
    for sp in res["species"]:
        m = mod_by.get(sp["name"])
        if m is None:
            continue
        what = []
        for key in ("styles", "moves", "ais"):
            if _canon(sp[key]) != _canon(m[key]):
                sp[key] = copy.deepcopy(m[key])
                what.append("%s (%d)" % (key, len(m[key])))
        mprops = {p["n"]: p for p in m["obj"]["props"]}
        for p in sp["obj"]["props"]:
            q = mprops.get(p["n"])
            if q is not None and _canon(q["v"]) != _canon(p["v"]):
                p["v"] = copy.deepcopy(q["v"])
                what.append("property %s" % p["n"])
        if what:
            changed[sp["name"]] = what
    data = g3cs.write_bytes(res)
    check = g3cs.parse_bytes(data)
    for sp in check["species"]:
        m = mod_by.get(sp["name"])
        if m is not None and any(_canon(sp[k]) != _canon(m[k]) for k in ("styles", "moves", "ais")):
            ctx.warn.append("combat table: species %s differs from the mod's after writing" % sp["name"])
    os.makedirs(os.path.join(out, "assets"), exist_ok=True)
    with open(os.path.join(out, "assets", "compiled_combatspecies.bin"), "wb") as f:
        f.write(data)
    return {"mode": mode, "volume": pick[-1][0], "bytes": len(data), "changed": changed}


# ---------------------------------------------------------------------------------------------------------------- main


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--mod", required=True, help="folder with the mod's .pNN archives (common/ and compiled/ below it)")
    ap.add_argument("--baseline", required=True, help="the game's data directory (common/*.pak, compiled/*.pak), read only")
    ap.add_argument("--out", required=True, help="output mod directory (replaced), e.g. mods/risen_extended")
    ap.add_argument("--enums", default=os.path.join(HERE, "extmod_enums.json"), help="enum names learned from the engine (tools/extmod_enums.py)")
    ap.add_argument("--no-assets", action="store_true", help="do not copy the loose resource files")
    ap.add_argument("--combat", choices=("p01", "p02", "off"), default="p01",
                    help="the combat table: p01 = the melee restructure of the changelog (1H / 2H / staff styles, reach, no backwards hop), p02 = what the newest volume ships "
                         "(the classic table plus a few values), off = the game's own")
    ap.add_argument("--research", default="B:/DevDrive/risen-ghidra/extmod/RESEARCH_teachers_data.json",
                    help="world-file analysis (container items, equipment, teacher limits; the lrent files are not read by this tool)")
    a = ap.parse_args()
    enums_extra = json.load(open(a.enums, encoding="utf8")) if os.path.exists(a.enums) else None
    ctx = Ctx(enums_extra)
    out = a.out
    if os.path.isdir(out):
        for sub in ("data", "strings"):
            shutil.rmtree(os.path.join(out, sub), ignore_errors=True)
    os.makedirs(out, exist_ok=True)
    d = load(a.mod, a.baseline)

    items, patches, recipes_new, st_items = gen_items(ctx, d)
    new_items, recipes, st_new = gen_new(ctx, d)
    dialog = gen_dialog(ctx, d)
    if os.path.exists(a.research):
        chap = {t["info"]: t.get("chapter") for t in json.load(open(a.research, encoding="utf8")).get("trader_stock_additions_by_chapter", [])}
        for t in dialog["trade"]:
            t["chapter"] = chap.get(t["info"])
    world = gen_world(ctx, d, a.research)
    wanted = set(items) | set(new_items) | set(recipes_new)
    st_str = gen_strings(ctx, d, out, wanted, a.combat == "p01")
    assets = gen_assets(ctx, d, out, not a.no_assets)
    combat = gen_combat(ctx, d, out, a.combat)

    # items that became recipes -> new recipe items the player is given (a property set cannot be added to a shipped template)
    new_recipes = collections.OrderedDict()
    for name, r in sorted(recipes_new.items()):
        if r["station"] is None:
            ctx.bad("recipe", name, "recipe station number %r unknown" % (r["craft"],), "M19")
            continue
        e = recipe_lua(r)
        e["learn"] = True
        e["from"] = name
        new_recipes[name + "_Goldsmith" if r["station"] == "goldsmith" else name + "_Recipe"] = e

    # the one light: It_Cast_Light range (a changed item whose only change is a property)
    data_dir = os.path.join(out, "data")
    hdr = "-- generated by tools/extmod_gen.py from the archives of the Risen Extended Mod. Private: do not commit or redistribute.\n"
    write(os.path.join(data_dir, "items.lua"), hdr + "return " + lua(items) + "\n")
    write(os.path.join(data_dir, "new_items.lua"), hdr + "return " + lua(new_items) + "\n")
    write(os.path.join(data_dir, "recipes.lua"), hdr + "return " + lua({"edit": recipes, "new": new_recipes}) + "\n")
    write(os.path.join(data_dir, "patches.lua"), hdr + "return " + lua(patches) + "\n")
    write(os.path.join(data_dir, "dialog.lua"), hdr + "return " + lua(dialog) + "\n")
    write(os.path.join(data_dir, "world.lua"), hdr + "return " + lua(world) + "\n")
    write(os.path.join(data_dir, "assets.lua"), hdr + "return " + lua({"new": assets["new"], "replaced": assets["replaced"]}) + "\n")
    for f in ("main.lua", "check.lua", "config.lua"):
        src = os.path.join(HERE, "extmod_templates", f)
        if os.path.exists(src):
            shutil.copyfile(src, os.path.join(out, f))
    write(os.path.join(out, "mod.toml"),
          'id          = "risen_extended"\nname        = "Risen Extended (generated)"\nversion     = "0.1.0"\nauthor      = "generated by tools/extmod_gen.py from the Risen Extended Mod (private)"\n'
          'description = "Local conversion of the Risen Extended Mod data: item numbers, new items and recipes, texts, monster skins, loose assets. For private play, not for redistribution."\n'
          'main        = "main.lua"\nassets      = "assets"\n')
    write(os.path.join(out, ".gitignore"), "*\n")
    write(os.path.join(out, "NOTICE.txt"),
          "This directory was generated from the Risen Extended Mod by tools/extmod_gen.py.\nIt contains the work of the mod's author (numbers, texts, item and recipe design, meshes, textures, actors)\n"
          "and is for private use only: do not commit it, do not redistribute it.\n")

    deferred = [
        {"item": "wind strength (WeatherStates.g3ws)", "needs": "M22", "note": "not converted yet"},
        {"item": "hud2.tab texts", "needs": "M21", "note": "the mod's table has 130 fewer rows than Enhanced's; nothing of it is applied"},
        {"item": "trader stock of chapters 2-4, chapter gates", "needs": "M20", "note": "added as RunScript commands to the chapter infos, which the engine runs at the chapter change: not played that far"},
        {"item": "the melee table is chosen at generation time (--combat p01|p02|off)", "needs": "M21", "note": "the engine reads the file once at start"},
    ]
    manifest = collections.OrderedDict([
        ("generator", "tools/extmod_gen.py"),
        ("counts", collections.OrderedDict([
            ("items_changed", len(items)), ("new_items", len(new_items)), ("new_recipe_items_edited", len(recipes)), ("items_that_became_recipes", len(new_recipes)),
            ("template_patches", len(patches)), ("xp_infos", len(dialog["xp"]["add"]) + len(dialog["xp"]["change"])), ("trade_entries", sum(len(t["entries"]) for t in dialog["trade"])), ("containers", len(world["containers"])), ("teacher_limits", len(world["teachers"])), ("assets_new", len(assets["new"])), ("assets_replaced", len(assets["replaced"])),
            ("assets_identical_skipped", assets["identical_skipped"]), ("asset_bytes", assets["bytes"])])),
        ("stats", dict(st_items + st_new + st_str)),
        ("items_changed", sorted(items)), ("new_items", sorted(new_items)), ("new_recipes", sorted(new_recipes)), ("patches", sorted(patches)),
        ("combat_table", combat), ("assets", assets), ("unconverted", ctx.unconverted), ("warnings", ctx.warn), ("deferred", deferred),
    ])
    write(os.path.join(out, "manifest.json"), json.dumps(manifest, indent=1, ensure_ascii=False) + "\n")
    print(json.dumps(manifest["counts"], indent=1))
    print("unconverted: %d, warnings: %d" % (len(ctx.unconverted), len(ctx.warn)))
    for u in ctx.unconverted[:40]:
        print("  unconverted %(kind)s %(name)s: %(reason)s [%(needs)s]" % u)


if __name__ == "__main__":
    main()
