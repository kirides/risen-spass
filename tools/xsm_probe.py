"""Probe: decode a Risen motion (._xmot, GR01MO01 around an EMotion FX 'XSM ' stream, RE.md section 66).
Usage: python -I tools/xsm_probe.py <file._xmot> [-v]

Layout found (little endian): 'XSM ' u8 major u8 minor u8 endian u8 order; chunks {u32 id, u32 size, u32 version}.
Chunk 201 (metadata) has a trustworthy size; chunk 202 (sub-motions) has not: its body is `u32 count`, then per sub-motion
  4 x quaternion (pose, bind pose, pose scale rot, bind scale rot), 4 x vector3 (pose pos, pose scale, bind pos, bind scale),
  u32 numPosKeys, numRotKeys, numScaleKeys, numScaleRotKeys, f32 maxError, u32 name length + name, then the keys:
  position keys (vec3 + f32 time), rotation keys (quat + f32 time), scale keys (vec3 + time), scale rotation keys (quat + time).
"""
import struct
import sys


def parse(d, verbose=False):
    p = d.find(b"XSM ")
    if p < 0:
        raise SystemExit("no XSM stream")
    o = p + 8
    cid, size, ver = struct.unpack_from("<III", d, o)
    if cid != 201:
        raise SystemExit("expected metadata chunk 201")
    o += 12 + size
    cid, size, ver = struct.unpack_from("<III", d, o)
    if cid != 202:
        raise SystemExit(f"expected sub-motion chunk 202, got {cid}")
    o += 12
    (n,) = struct.unpack_from("<I", d, o)
    o += 4
    out = []
    for i in range(n):
        o += 16 * 4 + 12 * 4
        npk, nrk, nsk, nsrk = struct.unpack_from("<IIII", d, o)
        o += 16
        (maxerr,) = struct.unpack_from("<f", d, o)
        o += 4
        (ln,) = struct.unpack_from("<I", d, o)
        o += 4
        name = d[o:o + ln].decode("latin-1")
        o += ln
        keys = {"pos": [], "rot": [], "scale": [], "scalerot": []}
        for key, cnt, fl in (("pos", npk, 3), ("rot", nrk, 4), ("scale", nsk, 3), ("scalerot", nsrk, 4)):
            for _ in range(cnt):
                v = struct.unpack_from("<" + "f" * (fl + 1), d, o)
                o += 4 * (fl + 1)
                keys[key].append(v)
        out.append((name, keys))
        if verbose:
            print(f"{i:3d} {name:24s} pos {npk} rot {nrk} scale {nsk} srot {nsrk}  err {maxerr:.4g}")
    return out, o


if __name__ == "__main__":
    data = open(sys.argv[1], "rb").read()
    subs, end = parse(data, "-v" in sys.argv)
    print("sub-motions", len(subs), "ends at", end, "of", len(data), "-> trailing", len(data) - end)
    dur = max((k[-1][-1] for _, ks in subs for k in ks.values() if k), default=0)
    print("duration (s)", dur)
