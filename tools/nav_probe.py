#!/usr/bin/env python
"""Probe for Risen's compiled navigation map (compiled_navigation_<World>.xnav).

Usage:  python -I tools/nav_probe.py <file.xnav> [more files]
        python -I tools/nav_probe.py --pak <navigation.pak> World Testlevel

Layout = gCNavigationMapCompiler::WriteLists (Game.dll 0x908af0) / ReadLists (0x90b4e0).
Primitive encodings (all little endian), read from the engine's writers:
  marker   u8 1 before every array;  array = marker, u32 count, elements
  PID      16-byte GUID + u32 (bCPropertyID, 20 bytes)
  eCEntityProxy      u16 1, u8 has, [PID]
  eCPropertySetProxy u16 1, u8 hasType, [u16 string-table index], u16 1, u8 has, [PID]
  vec      3 x f32
The file is a GENO container: "GENOMFLE", u16 version, u32 string-table offset (+10); payload at +14.
"""
import math
import struct
import subprocess
import sys
from pathlib import Path


class Reader:
    def __init__(self, d):
        self.d, self.p = d, 0

    def u8(self):
        v = self.d[self.p]; self.p += 1; return v

    def u16(self):
        v = struct.unpack_from('<H', self.d, self.p)[0]; self.p += 2; return v

    def u32(self):
        v = struct.unpack_from('<I', self.d, self.p)[0]; self.p += 4; return v

    def f32(self):
        v = struct.unpack_from('<f', self.d, self.p)[0]; self.p += 4; return v

    def vec(self):
        v = struct.unpack_from('<3f', self.d, self.p); self.p += 12; return v

    def pid(self):
        v = self.d[self.p:self.p + 16].hex(); self.p += 20; return v

    def arr(self, f):
        if self.u8() != 1:
            raise ValueError('array marker expected at 0x%x' % (self.p - 1))
        return [f() for _ in range(self.u32())]

    def proxy(self):
        self.u16()
        return self.pid() if self.u8() else None

    def ps(self):
        self.u16()
        ty = self.u16() if self.u8() else None
        self.u16()
        return (ty, self.pid() if self.u8() else None)

    def way(self):
        v = struct.unpack_from('<fII', self.d, self.p); self.p += 12; return v


def parse(data):
    if data[:8] != b'GENOMFLE':
        raise ValueError('not a GENO container')
    tbl = struct.unpack_from('<I', data, 10)[0]
    q = tbl + 4 + 1                      # DEADBEEF, u8 1, u32 n, n x (u16 len, text)
    n = struct.unpack_from('<I', data, q)[0]; q += 4
    strings = []
    for _ in range(n):
        ln = struct.unpack_from('<H', data, q)[0]; q += 2
        strings.append(data[q:q + ln].decode('latin1')); q += ln
    r = Reader(data)
    r.p = 14
    if data[r.p:r.p + 11] != b'GE3-NAV-MAP':
        raise ValueError('no GE3-NAV-MAP tag')
    r.p += 11
    m = {'strings': strings, 'table_offset': tbl}
    m['version'] = r.u32(); m['reserved'] = r.u32()
    m['filetime'] = struct.unpack_from('<Q', data, r.p)[0]; r.p += 8
    m['counts'] = [r.u32() for _ in range(6)]   # zones, paths, negzones, prefpaths, circles, interactions
    m['clusters'] = r.arr(lambda: r.arr(lambda: (r.arr(r.proxy), r.arr(r.proxy))))
    m['bounds'] = struct.unpack_from('<6f', data, r.p); r.p += 24
    m['negzone_idx'] = r.arr(r.u32)
    m['negzones'] = r.arr(lambda: dict(pts=r.arr(r.vec), f=r.f32(), v=r.vec(), flag=r.u8(), id=r.pid(), zone=r.pid()))
    m['circles'] = r.arr(lambda: dict(pos=r.arr(r.vec), radius=r.arr(r.f32), kind=r.u32(),
                                      zones=r.arr(r.pid), id=r.pid(), f=r.f32()))
    m['prefpath_pairs'] = r.arr(lambda: (r.u32(), r.pid()))
    m['prefpaths'] = r.arr(lambda: dict(pts=r.arr(r.vec), radius=r.arr(r.f32), f=r.f32(), v=r.vec(), zone=r.pid()))
    m['zone_idx'] = r.arr(r.u32)
    m['zones'] = r.arr(lambda: dict(ps=r.ps(), n=r.u32(), neg=r.arr(r.u32), circ=r.arr(r.u32), pref=r.arr(r.u32)))
    m['circle_links'] = r.arr(lambda: (r.u32(), r.arr(lambda: (r.pid(), r.arr(r.u32)))))
    m['path_idx'] = r.arr(r.u32)
    m['paths'] = r.arr(lambda: dict(ps=r.ps(), n=r.u32(), v=[r.vec() for _ in range(6)]))
    m['interactions'] = r.arr(lambda: (r.ps(), r.ps()))
    m['freepoints'] = r.arr(lambda: (r.ps(), r.u32()))
    m['navoffsets'] = r.arr(lambda: (r.ps(), r.u32()))
    m['areas'] = r.arr(lambda: dict(ps=r.ps(), is_path=r.u8(), links=r.arr(r.u32)))
    m['links'] = r.arr(lambda: dict(v=r.vec(), a=r.ps(), b=r.ps()))
    m['ways'] = r.arr(lambda: r.arr(r.way))
    m['end'] = r.p
    return m


def bbox(pts):
    if not pts:
        return None
    return tuple(round(f(p[i] for p in pts), 1) for i in (0, 1, 2) for f in (min, max))


def report(name, data):
    m = parse(data)
    ok = m['end'] == m['table_offset']
    print('== %s: %d bytes, version %d, string table %s' % (name, len(data), m['version'], m['strings']))
    print('   consumed to 0x%x, table at 0x%x -> %s' % (m['end'], m['table_offset'], 'EXACT' if ok else 'MISMATCH'))
    print('   header counts', m['counts'], 'bounds', tuple(round(b, 1) for b in m['bounds']))
    print('   clusters %d (sub-groups %d, zone refs %d)' % (len(m['clusters']), sum(len(c) for c in m['clusters']),
          sum(len(a) + len(b) for c in m['clusters'] for a, b in c)))
    for k in ('zones', 'paths', 'negzones', 'circles', 'prefpaths', 'interactions', 'freepoints', 'navoffsets',
              'areas', 'links', 'ways'):
        print('   %-13s %d' % (k, len(m[k])))
    print('   edges: area->link refs %d, way entries %d' % (sum(len(a['links']) for a in m['areas']),
          sum(len(w) for w in m['ways'])))
    pp = [v for p in m['paths'] for v in p['v']]
    cp = [v for c in m['circles'] for v in c['pos']]
    print('   path point bbox (xmin,xmax,ymin,ymax,zmin,zmax)', bbox(pp))
    print('   circle pos bbox', bbox(cp))
    if pp:
        best = min(pp, key=lambda v: (v[0] - 899) ** 2 + (v[2] + 16) ** 2)
        print('   nearest path point to (899,-16): %s  d=%.1f' % (tuple(round(c, 1) for c in best),
              math.hypot(best[0] - 899, best[2] + 16)))
    return ok


def main(argv):
    ok = True
    if argv and argv[0] == '--pak':
        pak = argv[1]
        for n in argv[2:]:
            name = 'compiled_navigation_%s.xnav' % n
            tool = Path(__file__).with_name('g3pak.py')
            data = subprocess.run([sys.executable, '-I', str(tool), 'cat', pak, name],
                                  capture_output=True, check=True).stdout
            ok &= report(name, data)
    else:
        for f in argv:
            ok &= report(f, Path(f).read_bytes())
    return 0 if ok else 1


if __name__ == '__main__':
    if len(sys.argv) < 2:
        print(__doc__); sys.exit(2)
    sys.exit(main(sys.argv[1:]))
