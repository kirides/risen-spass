"""Checks that tools/xsm_probe.py's reader consumes every ._xmot of animations.pak to its last byte.
Usage: python -I tools/xsm_all.py <animations.pak>"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from g3pak import Pak  # noqa: E402
import xsm_probe  # noqa: E402

pak = Pak(sys.argv[1])
ok = bad = 0
firsts = {}
for name in [e.path for e in pak.entries]:
    if not str(name).endswith("._xmot"):
        continue
    data = pak.read(name)
    try:
        subs, end = xsm_probe.parse(data)
        if end == len(data):
            ok += 1
        else:
            bad += 1
            firsts.setdefault(len(data) - end, name)
    except BaseException as e:  # SystemExit too
        bad += 1
        firsts.setdefault(str(e)[:40], name)
print("ok", ok, "bad", bad)
for k, v in list(firsts.items())[:8]:
    print(" ", k, v)
