#!/usr/bin/env python3
"""Compares the output of risen-pack2mod (C++) with the output of tools/extmod_gen.py (the Python oracle) for the same mod and baseline.

    python -I tools/pack2mod_golden.py <python output dir> <C++ output dir>

Compared: the set of files; data/*.lua (the generated first line differs by design); strings/*.csv; features.json; assets/ and variants/ byte by byte
(SHA-1); main.lua / config.lua ignoring line ends; the manifest's counts, statistics, unconverted entries and warnings. mod.toml, NOTICE.txt, report.md
and the manifest layout are the C++ tool's own. Exit code 0 = identical in everything compared, 1 = differences (each one is printed).
"""
import hashlib
import json
import os
import sys


def files(root):
    out = {}
    for dp, dn, fn in os.walk(root):
        for f in fn:
            p = os.path.join(dp, f)
            out[os.path.relpath(p, root).replace("\\", "/")] = p
    return out


def sha(p):
    h = hashlib.sha1()
    with open(p, "rb") as f:
        for b in iter(lambda: f.read(1 << 20), b""):
            h.update(b)
    return h.hexdigest()


def lines(p, skip_first=False):
    t = open(p, "rb").read().decode("utf8").replace("\r\n", "\n")
    L = t.split("\n")
    return L[1:] if skip_first else L


def main():
    py, cpp = sys.argv[1], sys.argv[2]
    fp, fc = files(py), files(cpp)
    diffs = []
    only_py = sorted(set(fp) - set(fc))
    only_c = sorted(set(fc) - set(fp))
    expected_extra = {"report.md"}   # the C++ tool writes a report next to the manifest
    expected_py_only = {"check.lua", "play.lua"}   # the live scenarios of the Extended Mod (tools/extmod_templates), not part of a generated mod
    for f in only_py:
        if f not in expected_py_only:
            diffs.append("only in the Python output: " + f)
    for f in only_c:
        if f not in expected_extra:
            diffs.append("only in the C++ output: " + f)
    same = 0
    for f in sorted(set(fp) & set(fc)):
        a, b = fp[f], fc[f]
        if f in ("mod.toml", "NOTICE.txt", "manifest.json", "check.lua", "play.lua"):
            continue
        if f == "data/world.lua":
            # Explained differences (docs/PACK2MOD.md "Golden test"):
            #  * the oracle's research keyed entities by name, so of two chests called Object_Interact_Animated_Chest_Lizard in BD_Farm_Events.lrent it kept one;
            #    the C++ tool keys by entity id and finds both
            #  * the mod's It_Axe_Ogreaxe is a rename of the game's It_Axe_Ogre (same GUIDs): the research counted Karakos' re-serialised equipment slot as an
            #    equipped Ogreaxe; the C++ tool compares by the game's names and leaves the shipped axe (the rename is listed in its report)
            from collections import Counter
            ca, cb = Counter(lines(a, True)), Counter(lines(b, True))
            only_a, only_b = ca - cb, cb - ca
            want_a = Counter(['        { id = "3ea8344a-6c0e-4d0a-8de5-55dc4f182d33", item = "It_Axe_Ogreaxe", name = "Karakos" },'])
            want_b = Counter(['            id = "28e2d7f0-1e5e-4b6d-a0e2-5dfecef81841",', '            items = {', '                { count = 1, item = "It_Po_EXP_Special" },',
                              '                { count = 1, item = "It_BlackPearl" },', '            },', '            layer = "BD_Farm_Events.lrent",',
                              '            name = "Object_Interact_Animated_Chest_Lizard",', '        },', '        {'])
            if only_a == want_a and only_b == want_b:
                print("world.lua: 2 explained differences (extra BD_Farm chest found by entity id, Karakos' axe: a GUID rename, not applied)")
                same += 1
            else:
                diffs.append("data/world.lua differs unexpectedly: only py %r, only cpp %r" % (dict(only_a), dict(only_b)))
        elif f.startswith("data/"):
            la, lb = lines(a, True), lines(b, True)
            if la != lb:
                n = next((i for i, (x, y) in enumerate(zip(la, lb)) if x != y), min(len(la), len(lb)))
                diffs.append("%s differs at line %d: py=%r cpp=%r" % (f, n + 2, la[n] if n < len(la) else None, lb[n] if n < len(lb) else None))
            else:
                same += 1
        elif f in ("main.lua", "config.lua"):
            if lines(a) != lines(b):
                diffs.append("%s differs (ignoring line ends)" % f)
            else:
                same += 1
        elif f == "features.json":
            ja, jb = json.load(open(a, encoding="utf8")), json.load(open(b, encoding="utf8"))
            ja["default_combat"] = {"p01": "changelog", "p02": "shipped"}.get(ja.get("default_combat"), ja.get("default_combat"))
            if ja != jb:
                diffs.append("features.json differs")
            else:
                same += 1
        elif f.endswith((".csv", ".json", ".gitignore")) or f == ".gitignore":
            if open(a, "rb").read() != open(b, "rb").read():
                la, lb = lines(a), lines(b)
                n = next((i for i, (x, y) in enumerate(zip(la, lb)) if x != y), min(len(la), len(lb)))
                diffs.append("%s differs at line %d: py=%r cpp=%r" % (f, n + 1, la[n] if n < len(la) else None, lb[n] if n < len(lb) else None))
            else:
                same += 1
        else:
            if os.path.getsize(a) != os.path.getsize(b) or sha(a) != sha(b):
                diffs.append("%s differs (size %d / %d)" % (f, os.path.getsize(a), os.path.getsize(b)))
            else:
                same += 1
    # manifest
    ma = json.load(open(os.path.join(py, "manifest.json"), encoding="utf8"))
    mc = json.load(open(os.path.join(cpp, "manifest.json"), encoding="utf8"))
    for k, v in ma["counts"].items():
        if k == "containers" and (v, mc["counts"].get(k)) == (11, 12):
            continue   # the extra chest above
        if mc["counts"].get(k) != v:
            diffs.append("manifest count %s: py=%r cpp=%r" % (k, v, mc["counts"].get(k)))
    sa = {k: v for k, v in ma["stats"].items() if v}
    sc = {k: v for k, v in mc["stats"].items() if v}
    if sa != sc:
        for k in sorted(set(sa) | set(sc)):
            if sa.get(k) != sc.get(k):
                diffs.append("manifest stat %s: py=%r cpp=%r" % (k, sa.get(k), sc.get(k)))
    # skipped string tables: the oracle lists them as unconverted entries, the C++ tool counts the skipped rows in the report notes (explained difference)
    ua = sorted((u["kind"], u["name"], u["reason"], u["needs"]) for u in ma["unconverted"] if u["kind"] != "strings")
    uc = sorted((u["kind"], u["name"], u["reason"], u["needs"]) for u in mc["unconverted"] if u["kind"] != "strings")
    for u in ua:
        if u not in uc:
            diffs.append("unconverted only in the Python manifest: %r" % (u,))
    for u in uc:
        if u not in ua:
            diffs.append("unconverted only in the C++ manifest: %r" % (u,))
    wa, wc = sorted(ma["warnings"]), sorted(mc["warnings"])
    for w in wa:
        if w not in wc:
            diffs.append("warning only in the Python manifest: " + w)
    for w in wc:
        if w not in wa:
            diffs.append("warning only in the C++ manifest: " + w)
    for k in ("items_changed", "new_items", "new_recipes", "patches"):
        if ma.get(k) != mc.get(k):
            diffs.append("manifest list %s differs" % k)
    print("compared %d identical files; counts: %s" % (same, json.dumps(mc["counts"])))
    if diffs:
        print("%d DIFFERENCE(S):" % len(diffs))
        for d in diffs:
            print("  " + d)
        return 1
    print("IDENTICAL")
    return 0


if __name__ == "__main__":
    sys.exit(main())
