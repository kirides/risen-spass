#!/usr/bin/env python3
"""Learns the numeric values of the engine's enums from the game itself, for tools/extmod_gen.py.

The archives store enum properties as numbers; `risen.template.patch` takes the engine's names (gEItemHoldType_1H). The names come from the engine's
reflection (mods/itemdump logs "ITEM <template> <set> <prop>:<type>=<value>" for every item template), the numbers from the same templates in the game's
own archives. Joining the two gives number -> name for every enum an item template uses. SKILLID lines give the numbers of gESkill names.

    scripts/test-run.ps1 -Mods itemdump -Seconds 100 -NoAutostart        (writes <test copy>\\bin\\spass\\spass.log)
    python -I tools/extmod_enums.py --log <spass.log> --baseline <Risen data dir> [--out tools/extmod_enums.json]

The output holds game data only (names of the engine's enums), not the mod's content; it is safe to commit.
"""
import argparse
import collections
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import extmod_diff as X  # noqa: E402
import g3tple  # noqa: E402

FRIENDLY = {
    "Stat_STR": "Strength", "Stat_DEX": "Dexterity", "Stat_INT": "Intelligence", "Stat_HP": "MaxHitPoints", "Stat_MP": "MaxManaPoints",
    "Stat_LP": "LearnPoints", "Stat_XP": "Experience", "Stat_LV": "Level", "Atrib_HP": "HitPoints", "Atrib_MP": "ManaPoints",
    "Combat_Axe": "Axe", "Combat_Sword": "Sword", "Combat_Staff": "Staff", "Combat_Bow": "Bow", "Combat_CrossBow": "CrossBow",
    "Prot_Edge": "ProtEdge", "Prot_Blunt": "ProtBlunt", "Prot_Point": "ProtPoint", "Prot_Fire": "ProtFire", "Prot_Ice": "ProtIce",
    "Prot_Magic": "ProtMagic", "Magic_Missile": "Missile", "Magic_Frost": "Frost", "Magic_Fireball": "Fireball", "Magic_Circle": "Circle",
    "Misc_Scribe": "Scribe", "Misc_Trophy": "Trophy", "Misc_Acrobat": "Acrobat", "Misc_Pickpocket": "Pickpocket", "Misc_Lockpick": "Lockpick",
    "Misc_Sneak": "Sneak", "Misc_Mining": "Mining", "Misc_Smith": "Smith", "Misc_Alchemy": "Alchemy",
}
item_re = re.compile(r"ITEM (\S+) (\S+) (\w+):(.+?)=(.*)$")
skill_re = re.compile(r"SKILLID (\w+) (-?\d+)")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--log", required=True)
    ap.add_argument("--baseline", required=True)
    ap.add_argument("--out", default=os.path.join(HERE, "extmod_enums.json"))
    a = ap.parse_args()
    names = {}   # (template, "Set.Prop") -> (enum type, text)
    skills = {}
    for line in open(a.log, encoding="utf8", errors="replace"):
        i = line.find(" ITEM ")
        if i >= 0:
            m = item_re.match(line[i + 1:].rstrip())
            if m:
                tpl, st, prop, typ, val = m.groups()
                t = re.match(r"bTPropertyContainer<enum (\w+)>", typ)
                if t:
                    names[(tpl, "%s.%s" % (st, prop))] = (t.group(1), val)
            continue
        m = skill_re.search(line)
        if m and m.group(1) != "None":
            skills[int(m.group(2))] = FRIENDLY.get(m.group(1), m.group(1))
    base = X.load_baseline(a.baseline)
    found = collections.defaultdict(lambda: collections.defaultdict(collections.Counter))
    for k, (label, pak, e) in base["common"].files.items():
        if not k.endswith(".tple"):
            continue
        t = g3tple.parse(pak.read(e))
        tpl = t.name[:-5]
        for p in t.props:
            if not p.type.startswith("bTPropertyContainer<enum") or not isinstance(p.value, int):
                continue
            got = names.get((tpl, p.key))
            if got:
                typ, text = got
                suffix = text[len(typ) + 1:] if text.startswith(typ + "_") else text
                found[typ][p.value][suffix] += 1
    enums, conflicts = {}, []
    for typ in sorted(found):
        enums[typ] = {}
        for num in sorted(found[typ]):
            c = found[typ][num]
            enums[typ][str(num)] = c.most_common(1)[0][0]
            if len(c) > 1:
                conflicts.append((typ, num, dict(c)))
    out = {"enums": enums, "skills": {str(n): s for n, s in sorted(skills.items())}}
    with open(a.out, "w", encoding="utf8", newline="\n") as f:
        json.dump(out, f, indent=1, sort_keys=True)
        f.write("\n")
    print("%d enums, %d values, %d skill ids, %d conflicts -> %s" % (len(enums), sum(len(v) for v in enums.values()), len(skills), len(conflicts), a.out))
    for c in conflicts[:20]:
        print("  conflict", c)


if __name__ == "__main__":
    main()
