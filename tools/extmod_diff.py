#!/usr/bin/env python3
"""Diff a Risen mod that ships .pNN patch archives against the game's own data (docs/EXTENDED_MOD_ANALYSIS.md).

The mod is read as data only (nothing in it is executed). The baseline is a game data directory that holds
`common/*.pak` and `compiled/*.pak` (Risen Enhanced, or a classic install; a baseline's own .pNN files are applied too).

  python -I tools/extmod_diff.py --mod <mod dir> --baseline <game>/data --out <dir> [--lua]

Writes into <out>:
  inventory.tsv        every file of every archive: layer, kind, file, size, new | changed | same, baseline location
  templates.json       templates: new ones (all properties) and changed ones (old -> new per property, GUID references resolved)
  strings.json         string tables (compiled/strings/*.tab): new / changed rows per language, normalised
  infos.json           compiled_infos.bin (dialogues): changed and new infos, XP rewards, trader refills
  combat.json          compiled_combatspecies.bin: species and melee style changes, per layer
  world.json           common/projects/*.lrent: items placed into containers, teacher limits (TeachSkills), counts
  balance_items.lua    with --lua: the existing-item changes as a `risen.balance` spec (prototype of the M18 converter)
  summary.txt          counts

Layering: archives of one base name are applied in the order of their number (.p00, .p01, ...), later files win.
Files are matched by bare lower-case file name (the mod's archives are flat, the game's are not). Nothing here
needs the game; all readers are in tools/g3pak.py, g3tple.py, g3tab.py (formats: docs/RE.md section 43).
"""
import argparse
import collections
import difflib
import glob
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import g3pak  # noqa: E402
import g3tab  # noqa: E402
import g3tple  # noqa: E402

# gESkill values (docs/RE.md section 43: 0..24 read from the enum registration, 25/29/30/31/32 confirmed by recipes and rings)
SKILLS = ["HitPoints", "ManaPoints", "Level", "Experience", "LearnPoints", "MaxHitPoints", "MaxManaPoints", "Strength", "Dexterity",
          "Intelligence", "ProtEdge", "ProtBlunt", "ProtPoint", "ProtFire", "ProtIce", "ProtMagic", "Sword", "Axe", "Staff", "Bow",
          "CrossBow", "Circle", "Fireball", "Frost", "Missile", "Smith", None, None, "Pickpocket", "Sneak", "Acrobat", "Alchemy",
          "Scribe", "Trophy"]
USE_TYPES = {0: "None", 1: "1H", 2: "2H", 3: "BS", 4: "Arrow", 5: "Bow", 6: "CrossBow", 7: "Bolt", 9: "Shield", 10: "Armor",
             11: "Helmet", 12: "Staff", 13: "Amulet", 14: "Ring", 18: "Axe", 19: "Cast"}
DAMAGE_TYPES = {0: "None", 1: "Edge", 2: "Blunt", 3: "Point", 4: "Fire", 5: "Ice", 6: "Magic"}
CATEGORIES = {0: "None", 1: "Weapon", 2: "Armor", 3: "Consumable", 5: "Magic"}
CRAFT = {0: "alchemy", 3: "frying", 4: "goldsmith", 5: "scroll", 6: "forge", 7: "anvil", 8: "coolweapon", 9: "grindstone"}
# properties that change whenever a template is merely re-saved (runtime state, collision, AI bookkeeping)
NOISE = ("eCCollisionShape.", "eCRigidBody_PS.", "gCNavigation_PS.", "gCScriptRoutine_PS.", "gCInventory_PS.")
SKIP_BASELINE = ("speech", "sounds")


def skill_name(n):
    return SKILLS[n] if isinstance(n, int) and 0 <= n < len(SKILLS) and SKILLS[n] else "skill%s" % n


def norm_text(s):
    """The mod's string tables went through a spreadsheet: quotes doubled and wrapped, ` and acute for apostrophes."""
    s = s.strip()
    if len(s) >= 2 and s[0] == "'" and s[-1] == "'":
        s = s[1:-1].replace("''", "'")
    return s.replace("`", "'").replace("´", "'").strip()


# ---------------------------------------------------------------------------------------------- archives

class Layered:
    """bare lower-case file name -> (layer label, Pak, Entry), later layers win."""

    def __init__(self):
        self.files = {}
        self.history = collections.defaultdict(list)

    def add(self, label, pak):
        for e in pak.entries:
            k = os.path.basename(e.path).lower()
            self.files[k] = (label, pak, e)
            self.history[k].append((label, pak, e))


def archive_groups(root):
    """{(kind, base): [(number, path)]} for every <kind>/<base>.pNN below root."""
    groups = collections.defaultdict(list)
    for f in glob.glob(os.path.join(root, "**", "*.p[0-9][0-9]"), recursive=True):
        kind = os.path.basename(os.path.dirname(f))
        base, ext = os.path.splitext(os.path.basename(f))
        groups[(kind, base)].append((int(ext[2:]), f))
    return {k: sorted(v) for k, v in groups.items()}


def load_baseline(data):
    out = {"common": Layered(), "compiled": Layered()}
    for kind in out:
        for pak in sorted(glob.glob(os.path.join(data, kind, "*.pak"))):
            base = os.path.splitext(os.path.basename(pak))[0]
            if base.startswith(SKIP_BASELINE):
                continue
            out[kind].add("%s/%s.pak" % (kind, base), g3pak.Pak(pak))
            for n, f in sorted((int(os.path.splitext(x)[1][2:]), x) for x in glob.glob(os.path.join(data, kind, base + ".p[0-9][0-9]"))):
                out[kind].add("%s/%s.p%02d" % (kind, base, n), g3pak.Pak(f))
    return out


# ---------------------------------------------------------------------------------------------- templates

def flat(t):
    """ordered {key: value} of a parsed template; repeated keys (array members) get #2, #3, ..."""
    d, c = collections.OrderedDict(), collections.Counter()
    for p in t.props:
        c[p.key] += 1
        d[p.key if c[p.key] == 1 else "%s#%d" % (p.key, c[p.key])] = p.value
    return d


def array(P, prefix):
    """elements of an array property as list of dicts, e.g. array(P, 'gCItem_PS.ModifySkills[].')"""
    out, cur = [], None
    for k, v in P.items():
        if not k.startswith(prefix):
            continue
        f = k[len(prefix):].split("#")[0]
        if cur is None or f in cur:
            cur = {}
            out.append(cur)
        cur[f] = v
    return out


def summarise_item(P):
    d = {}
    for key, name in (("gCItem_PS.GoldValue", "value"), ("gCDamage_PS.DamageAmount", "damage"), ("gCCastInfo_PS.ManaCost", "mana_cost"),
                      ("gCItem_PS.UseType", "use_type"), ("gCItem_PS.HoldType", "hold_type"), ("gCItem_PS.Category", "category"),
                      ("gCDamage_PS.DamageType", "damage_type"), ("gCItem_PS.SortValue", "sort"), ("gCItem_PS.IconImage", "icon"),
                      ("eCMesh_PS.MeshFileName", "mesh"), ("gCDamage_PS.DamageMultiplier", "multiplier")):
        if key in P:
            d[name] = P[key]
    req = [(skill_name(e.get("Skill")), e.get("Amount")) for e in array(P, "gCItem_PS.RequiredSkills[].")]
    mod = [(skill_name(e.get("Skill")), e.get("Amount"), e.get("Modifier")) for e in array(P, "gCItem_PS.ModifySkills[].")]
    if req:
        d["requires"] = req
    if mod:
        d["modifies"] = mod
    return d


def template_stage(base, layers, out):
    van, vdir = {}, {}
    for k, (label, pak, e) in base["common"].files.items():
        if k.endswith(".tple"):
            t = g3tple.parse(pak.read(e))
            van[t.name[:-5]] = t
            vdir[t.name[:-5]] = e.path
    mod = {}
    for k, (label, pak, e) in layers["common"].files.items():
        if k.endswith(".tple"):
            t = g3tple.parse(pak.read(e))
            mod[t.name[:-5]] = (t, label)
    guid = {}
    for t in list(van.values()) + [m[0] for m in mod.values()]:
        for g in t.guids:
            guid.setdefault(g, t.name[:-5])
    for t, _ in mod.values():  # a mod template wins over a baseline one with the same GUID
        for g in t.guids:
            guid[g] = t.name[:-5]

    def conv(t):
        d = flat(t)
        for k, v in d.items():
            if isinstance(v, str) and v.startswith("@"):
                d[k] = guid.get(v, v)
        return d

    changed, new = {}, {}
    for name, (t, label) in sorted(mod.items()):
        b = conv(t)
        if name in van:
            a = conv(van[name])
            diff = {k: [a.get(k), b.get(k)] for k in set(a) | set(b) if a.get(k) != b.get(k)}
            changed[name] = {"dir": vdir[name], "layer": label, "noise_only": all(k.startswith(NOISE) for k in diff) if diff else True,
                             "diff": diff, "was": summarise_item(a), "now": summarise_item(b)}
        else:
            new[name] = {"layer": label, "summary": summarise_item(b), "props": b}
    json.dump({"changed": changed, "new": new}, open(os.path.join(out, "templates.json"), "w", encoding="utf8"), indent=1, default=str)
    return van, vdir, mod, changed, new, conv


def lua_str(s):
    return '"%s"' % str(s).replace("\\", "\\\\").replace('"', '\\"')


def lua_skills(pairs):
    items = ["%s = %d" % (n, a) for n, a in pairs if not n.startswith("skill")]
    return "{ " + ", ".join(items) + " }"


def emit_lua(out, changed, new, van, conv_fn, mod):
    """risen.balance spec for the existing items the mod changed (the M18 converter writes the same shape for everything)."""
    L = ["-- generated by tools/extmod_diff.py: values the mod changed on EXISTING item templates (old values in comments)",
         "-- derived from the mod's data: for private use, do not redistribute", "return {", "    items = {"]
    for name, c in sorted(changed.items()):
        if c["noise_only"] or not c["dir"].startswith("Items/"):
            continue
        was, now = c["was"], c["now"]
        parts = []
        for key in ("value", "damage", "mana_cost"):
            if key in now and now.get(key) != was.get(key):
                parts.append("%s = %s" % (key, now[key]))
        if now.get("requires") != was.get("requires"):
            parts.append("requires = " + lua_skills(now.get("requires", [])))
        if now.get("modifies") != was.get("modifies"):
            mods = [(n, a) for n, a, m in now.get("modifies", [])]
            parts.append("modifies = " + lua_skills(mods))
        if now.get("use_type") != was.get("use_type") and now.get("use_type") in USE_TYPES:
            parts.append("use_type = %s" % lua_str("gEItemUseType_" + USE_TYPES[now["use_type"]]))
        props = []
        if now.get("sort") != was.get("sort") and "sort" in now:
            props.append("gCItem_PS = { SortValue = %s }" % now["sort"])
        if now.get("damage_type") != was.get("damage_type") and now.get("damage_type") in DAMAGE_TYPES:
            props.append("gCDamage_PS = { DamageType = %s }" % lua_str("gEDamageType_" + DAMAGE_TYPES[now["damage_type"]]))
        if props:
            parts.append("properties = { " + ", ".join(props) + " }")
        if parts:
            L.append("        %s = { %s }," % (name, ", ".join(parts)))
    L += ["    },", "}"]
    open(os.path.join(out, "balance_items.lua"), "w", encoding="utf8", newline="\n").write("\n".join(L) + "\n")


# ---------------------------------------------------------------------------------------------- strings

def strings_stage(base, layers, out):
    res = {}
    vt = {}
    for k, (label, pak, e) in base["compiled"].files.items():
        if k.endswith(".tab"):
            vt[k] = g3tab.read(pak.read(e))
    for k, (label, pak, e) in sorted(layers["compiled"].files.items()):
        if not k.endswith(".tab"):
            continue
        mc, mr = g3tab.read(pak.read(e))
        vc, vr = vt.get(k, ([], {}))
        r = {"layer": label, "baseline_rows": len(vr), "mod_rows": len(mr), "languages": mc[1:],
             "baseline_only_rows": [i for i in vr if i not in mr], "baseline_only_languages": [c for c in vc[1:] if c not in mc],
             "new": {}, "changed": {}}
        for i, cells in mr.items():
            if i not in vr:
                r["new"][i] = {c: norm_text(t) for c, t in cells.items()}
                continue
            ch = {c: [norm_text(vr[i][c]), norm_text(t)] for c, t in cells.items() if c in vr[i] and norm_text(vr[i][c]) != norm_text(t)}
            if ch:
                r["changed"][i] = ch
        res[k] = r
    json.dump(res, open(os.path.join(out, "strings.json"), "w", encoding="utf8"), indent=1, ensure_ascii=False)
    return res


# ---------------------------------------------------------------------------------------------- infos / combat / world

def split_infos(props):
    infos, cur = collections.OrderedDict(), None
    for p in props:
        if p.pset == "gCInfo" and p.name == "Name":
            cur, n = p.value, 1
            while cur in infos:
                n += 1
                cur = "%s#%d" % (p.value, n)
            infos[cur] = []
        if cur is not None:
            infos[cur].append((p.key, p.value))
    return infos


def infos_stage(base, layers, out, guid_name):
    bk = base["compiled"].files.get("compiled_infos.bin")
    if not bk:
        return {}
    A = split_infos(g3tple.parse(bk[1].read(bk[2])).props)
    res = {}
    for label, pak, e in (h for h in layers["compiled"].history.get("compiled_infos.bin", [])):
        B = split_infos(g3tple.parse(pak.read(e)).props)
        xp, refill, other, new = [], collections.defaultdict(list), [], []
        for k, rec in B.items():
            owner = dict(rec).get("gCInfo.Owner")
            if k not in A:
                new.append({"info": k, "owner": owner})
                continue
            if rec == A[k]:
                continue
            d = list(difflib.unified_diff([str(x) for x in A[k]], [str(x) for x in rec], lineterm="", n=0))
            d = [x for x in d if not x.startswith(("---", "+++", "@@"))]
            xps = [x for x in d if "GiveXP.Amount" in x]
            if xps:
                xp.append({"info": k, "owner": owner, "diff": xps})
            i = 0
            while i < len(rec):
                if rec[i] == ("gCInfoCommandRunScript.Script", "OnTradeInfo_RefillTradeItems") and rec not in (A[k],):
                    g = dict(rec[i:i + 5])
                    t = (g["gCInfoCommandRunScript.Self"], g["gCInfoCommandRunScript.Other"], g["gCInfoCommandRunScript.Param"])
                    if t not in refill[k]:
                        refill[k].append(t)
                    i += 5
                else:
                    i += 1
            rest = [x for x in d if "GiveXP" not in x and "RunScript" not in x]
            if rest:
                other.append({"info": k, "owner": owner, "diff": rest})
        res[label] = {"infos_baseline": len(A), "infos_mod": len(B), "new": new, "xp_changes": xp, "trade_refills": refill,
                      "other_changes": other}
    json.dump(res, open(os.path.join(out, "infos.json"), "w", encoding="utf8"), indent=1, default=str)
    return res


def styles_of(t):
    S, cur, species = collections.OrderedDict(), [], []
    for p in t.props:
        if p.pset == "gCCombatSpecies":
            species.append((p.name, p.value))
        if p.pset != "gCCombatStyleMelee":
            continue
        cur.append(p)
        if p.name == "Template":   # the last member of a style record
            d = {}
            for q in cur:
                d[q.name + ("#" if q.name in d else "")] = q.value
            S[d.get("Name")] = d
            cur = []
    return S, species


def combat_stage(base, layers, out):
    bk = base["library"].files.get("compiled_combatspecies.bin") if "library" in base else None
    bk = bk or base["compiled"].files.get("compiled_combatspecies.bin")
    if not bk:
        return {}
    SA, spA = styles_of(g3tple.parse(bk[1].read(bk[2])))
    res = {}
    for label, pak, e in layers["compiled"].history.get("compiled_combatspecies.bin", []):
        SB, spB = styles_of(g3tple.parse(pak.read(e)))
        r = {"styles_baseline": len(SA), "styles_mod": len(SB), "styles_new": sorted(set(SB) - set(SA)), "styles_removed": sorted(set(SA) - set(SB)),
             "styles_changed": {}, "species_changes": [(i, a, b) for i, (a, b) in enumerate(zip(spA, spB)) if a != b]}
        for n in sorted(set(SA) & set(SB)):
            ch = {k: [SA[n].get(k), SB[n].get(k)] for k in set(SA[n]) | set(SB[n]) if SA[n].get(k) != SB[n].get(k)}
            if ch:
                r["styles_changed"][n] = ch
        res[label] = r
    json.dump(res, open(os.path.join(out, "combat.json"), "w", encoding="utf8"), indent=1, default=str)
    return res


def world_stage(base, layers, out, guid_name):
    res = {}
    for label, pak, e in (h for k, hs in sorted(layers["common"].history.items()) if k.endswith(".lrent") for h in hs):
        bk = base["common"].files.get(os.path.basename(e.path).lower())
        if not bk:
            res[e.path] = {"layer": label, "baseline": None}
            continue
        a, b = g3tple.parse(bk[1].read(bk[2])), g3tple.parse(pak.read(e))
        sa = [(p.key, str(p.value)) for p in a.props]
        sb = [(p.key, str(p.value)) for p in b.props]
        placed, teach = collections.Counter(), []
        for op, i1, i2, j1, j2 in difflib.SequenceMatcher(None, sa, sb, autojunk=False).get_opcodes():
            if op == "equal":
                continue
            for k, v in sb[j1:j2]:
                if k.endswith(".Template") and v.startswith("@"):
                    placed[guid_name.get(v, v)] += 1
        def groups(t):
            G, cur = [], []
            for p in t.props:
                if "TeachSkills[]." in p.name:
                    cur.append((p.name.split("TeachSkills[].")[1], p.value))
                elif cur and p.name == "TalkedToPlayer":
                    G.append(cur)
                    cur = []
            out_g = []
            for g in G:
                recs, c = [], {}
                for k, v in g:
                    if k in c:
                        recs.append(c)
                        c = {}
                    c[k] = v
                recs.append(c)
                out_g.append([(skill_name(r.get("Skill")), r.get("MinValue"), r.get("MaxValue")) for r in recs])
            return out_g
        ga, gb = groups(a), groups(b)
        for x, y in zip(ga, gb):
            if x != y:
                teach.append([x, y])
        res[os.path.basename(e.path)] = {"layer": label, "props_baseline": len(sa), "props_mod": len(sb), "items_placed": dict(placed),
                                         "teach_groups_changed": teach}
    json.dump(res, open(os.path.join(out, "world.json"), "w", encoding="utf8"), indent=1, default=str)
    return res


# ---------------------------------------------------------------------------------------------- main

def inventory(base, layers, groups, out):
    rows = []
    for kind in ("common", "compiled"):
        for key, hist in sorted(layers[kind].history.items()):
            for label, pak, e in hist:
                bk = base[kind].files.get(key)
                if not bk:
                    st, where = "new", ""
                else:
                    st = "same" if bk[1].read(bk[2]) == pak.read(e) else "changed"
                    where = "%s:%s" % (bk[0], bk[2].path)
                rows.append((label, kind, e.path, e.size, st, where))
    with open(os.path.join(out, "inventory.tsv"), "w", encoding="utf8", newline="\n") as f:
        f.write("layer\tkind\tfile\tsize\tstatus\tbaseline\n")
        for r in rows:
            f.write("\t".join(map(str, r)) + "\n")
    return rows


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--mod", required=True)
    ap.add_argument("--baseline", required=True, help="game data directory with common/*.pak and compiled/*.pak (read only)")
    ap.add_argument("--out", required=True)
    ap.add_argument("--lua", action="store_true")
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    base = load_baseline(a.baseline)
    groups = archive_groups(a.mod)
    layers = {"common": Layered(), "compiled": Layered()}
    for (kind, name), lst in sorted(groups.items(), key=lambda kv: (kv[0][0], min(n for n, _ in kv[1]))):
        pass
    # apply in global number order so that "later wins" follows the .pNN numbers across base names
    flat_list = sorted(((n, kind, name, f) for (kind, name), lst in groups.items() for n, f in lst), key=lambda x: (x[0], x[1], x[2]))
    for n, kind, name, f in flat_list:
        if kind in layers:
            layers[kind].add("%s/%s.p%02d" % (kind, name, n), g3pak.Pak(f))
    rows = inventory(base, layers, groups, a.out)
    van, vdir, mod, changed, new, conv = template_stage(base, layers, a.out)
    guid_name = {}
    for t in van.values():
        for g in t.guids:
            guid_name.setdefault(g, t.name[:-5])
    for t, _ in mod.values():
        for g in t.guids:
            guid_name[g] = t.name[:-5]
    st = strings_stage(base, layers, a.out)
    inf = infos_stage(base, layers, a.out, guid_name)
    com = combat_stage(base, layers, a.out)
    wor = world_stage(base, layers, a.out, guid_name)
    if a.lua:
        emit_lua(a.out, changed, new, van, conv, mod)
    cnt = collections.Counter((r[1], r[4]) for r in rows)
    with open(os.path.join(a.out, "summary.txt"), "w", encoding="utf8") as f:
        f.write("files: %s\n" % dict(cnt))
        f.write("templates: %d changed (%d only runtime noise), %d new\n" % (len(changed), sum(1 for c in changed.values() if c["noise_only"]), len(new)))
        for t, r in st.items():
            f.write("strings %s: %d new rows, %d changed rows, %d baseline rows missing\n" % (t, len(r["new"]), len(r["changed"]), len(r["baseline_only_rows"])))
        for lab, r in inf.items():
            f.write("infos %s: %d new, %d with XP changes, %d trade-refill infos, %d other changes\n" % (lab, len(r["new"]), len(r["xp_changes"]), len(r["trade_refills"]), len(r["other_changes"])))
        for lab, r in com.items():
            f.write("combat %s: styles +%d -%d ~%d, species changes %d\n" % (lab, len(r["styles_new"]), len(r["styles_removed"]), len(r["styles_changed"]), len(r["species_changes"])))
    print(open(os.path.join(a.out, "summary.txt"), encoding="utf8").read())


if __name__ == "__main__":
    main()
