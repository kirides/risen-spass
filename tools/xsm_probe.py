"""Probe: list the EMotion FX chunks of a Risen motion (._xmot, GR01MO01 around an 'XSM ' stream).
Usage: python -I tools/xsm_probe.py <file._xmot>"""
import struct
import sys

d = open(sys.argv[1], "rb").read()
p = d.find(b"XSM ")
if p < 0:
    sys.exit("no XSM stream")
print("XSM at", hex(p), "version", d[p + 4], d[p + 5], "endian", d[p + 6], "order", d[p + 7])
o = p + 8
while o + 12 <= len(d):
    cid, size, ver = struct.unpack_from("<III", d, o)
    if cid > 0x400 or o + 12 + size > len(d):
        print("stop at", hex(o), "id", cid, "size", size)
        break
    print(f"chunk id={cid} size={size} ver={ver} at {hex(o)}")
    o += 12 + size
print("rest bytes after chunks:", len(d) - o)
