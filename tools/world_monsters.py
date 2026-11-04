#!/usr/bin/env python3
"""Where does the game place its monsters?  Reads the world layers (*.lrent) of projects.pak and the monster templates of templates.pak
(read only, nothing is written to the game) and prints one JSON file with every placed monster: template, layer, position.

    python tools/world_monsters.py <data/common dir> <out.json>

Record layout (docs/RE.md section 43, "A world layer entity record"): the creator template's GUID is preceded by `01 00 01`, followed by
`00 00 00 00 d6 00` (the local matrix); 128 bytes before the `01 00 01` starts the entity's 16 float world matrix (row-major, position in row 3).
"""
import json
import math
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import g3pak      # noqa: E402
import g3tple     # noqa: E402

REF = re.compile(rb"\x01\x00\x01(.{16})\x00\x00\x00\x00\xd6\x00", re.S)


def monster_guids(templates_pak):
    """GUID -> template name for every template below Monster/."""
    out = {}
    for e in templates_pak.entries:
        if not e.path.lower().startswith("monster/") or not e.path.lower().endswith(".tple"):
            continue
        t = g3tple.parse(templates_pak.read(e))
        name = os.path.basename(e.path)[:-5]
        group = e.path.split("/")[1]
        for g in t.guids:
            out[bytes.fromhex(g[1:])] = (name, group)
    return out


def main(argv):
    if len(argv) != 3:
        print(__doc__)
        return 2
    common, out = argv[1], argv[2]
    tp = g3pak.Pak(os.path.join(common, "templates.pak"))
    guids = monster_guids(tp)
    pp = g3pak.Pak(os.path.join(common, "projects.pak"))
    rows = []
    for e in pp.entries:
        if not e.path.lower().endswith(".lrent"):
            continue
        b = pp.read(e)
        for m in REF.finditer(b):
            hit = guids.get(m.group(1))
            if not hit:
                continue
            at = m.start() - 128
            if at < 0:
                continue
            f = struct.unpack_from("<16f", b, at)
            x, y, z = f[12], f[13], f[14]
            if not all(math.isfinite(v) and abs(v) < 1e6 for v in (x, y, z)):
                continue
            rows.append({"template": hit[0], "group": hit[1], "layer": os.path.basename(e.path)[:-6], "folder": os.path.dirname(e.path),
                         "x": round(x), "y": round(y), "z": round(z)})
    json.dump(rows, open(out, "w"), indent=0)
    print("%d monsters placed, %d templates, %d layers" % (len(rows), len({r["template"] for r in rows}), len({r["layer"] for r in rows})))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
