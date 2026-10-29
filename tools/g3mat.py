"""Offline reader for Risen's compiled materials (._xmat, GR01/SM01), docs/RE.md section 60.

A material is a 0x28-byte header and a sequence of property objects (the grammar of bCAccessorPropertyObject, with inline
strings instead of a string table):

  object  : u16 1, u8 1 | u16 1, u8 1, str class | u16 1, u8 0, u16 classVersion | u16 0xc9, u32 size | body (size bytes)
  body    : u16 0xc9, u32 nProps, per property: str name, str type, u16 0x1e, u32 size, payload
  str     : u16 length, bytes
  tail    : class specific bytes behind the body (node links, GUID, editor position), up to the next object header

Usage:  python -I tools/g3mat.py <file._xmat | archive.pak> [--all]     (dumps the objects; --all checks every material of a pak)
"""
import re
import struct
import sys

HDR = b"\x01\x00\x01\x01\x00\x01"
# an object header followed by a plausible class name (tail data can contain the six header bytes by chance)
NEXT = re.compile(rb"\x01\x00\x01\x01\x00\x01[\x08-\x40]\x00(?:eC|gC)[A-Za-z0-9_]+\x01\x00\x00", re.S)


def u16(b, p):
    return struct.unpack_from("<H", b, p)[0]


def u32(b, p):
    return struct.unpack_from("<I", b, p)[0]


def rstr(b, p):
    n = u16(b, p)
    return b[p + 2:p + 2 + n].decode("latin1"), p + 2 + n


class Obj:
    def __init__(self):
        self.cls = ""
        self.version = 0
        self.begin = 0
        self.body_begin = 0
        self.body_end = 0
        self.props = []  # (name, type, payload_begin, payload_end)
        self.tail = b""


def parse_object(b, p):
    o = Obj()
    o.begin = p
    if b[p:p + 6] != HDR:
        raise ValueError("no object header at %d" % p)
    o.cls, q = rstr(b, p + 6)
    if b[q:q + 3] != b"\x01\x00\x00":
        raise ValueError("class record at %d" % q)
    o.version = u16(b, q + 3)
    q += 5
    if u16(b, q) != 0xC9:
        raise ValueError("no 0xc9 at %d" % q)
    size = u32(b, q + 2)
    o.body_begin = q + 6
    o.body_end = o.body_begin + size
    q = o.body_begin
    if u16(b, q) != 0xC9:
        raise ValueError("no body 0xc9 at %d" % q)
    n = u32(b, q + 2)
    q += 6
    for _ in range(n):
        name, q = rstr(b, q)
        typ, q = rstr(b, q)
        if u16(b, q) != 0x1E:
            raise ValueError("property %s: no 0x1e at %d" % (name, q))
        psize = u32(b, q + 2)
        o.props.append((name, typ, q + 6, q + 6 + psize))
        q += 6 + psize
    if q + 2 == o.body_end and u16(b, q) == 0xC9:  # the class's own trailer
        q += 2
    if q > o.body_end:
        raise ValueError("%s: properties end at %d, body at %d" % (o.cls, q, o.body_end))
    o.props_end = q  # a node's body also holds what follows it (the shader's covers the nodes it uses)
    return o


def parse(b):
    if b[:8] != b"GR01SM01":
        raise ValueError("not a GR01/SM01 material")
    objs = []
    p = 0x28
    while p < len(b):
        o = parse_object(b, p)
        m = NEXT.search(b, o.props_end)
        end = len(b) if m is None else m.start()
        o.tail = b[o.props_end:end]
        objs.append(o)
        p = end
    return objs


def dump(b):
    print("header", b[:0x28].hex(), "len", len(b))
    for o in parse(b):
        print("%-22s v=%#x @%d body %d..%d tail %d" % (o.cls, o.version, o.begin, o.body_begin, o.body_end, len(o.tail)))
        for name, typ, s, e in o.props:
            if typ in ("bCImageResourceString", "bCString"):
                v = rstr(b, s)[0]
            else:
                v = b[s:e].hex()
            print("    %-26s %-48s %s" % (name, typ, v))
        print("    tail", len(o.tail), o.tail.hex())


if __name__ == "__main__":
    path = sys.argv[1]
    if "--all" in sys.argv:
        sys.path.insert(0, __file__.rsplit("tools", 1)[0] + "tools")
        from g3pak import Pak
        pak = Pak(path)
        ok = bad = 0
        classes = {}
        for e in pak.entries:
            name = e.path
            if not name.lower().endswith("._xmat"):
                continue
            try:
                for o in parse(pak.read(e)):
                    classes[o.cls] = classes.get(o.cls, 0) + 1
                ok += 1
            except Exception as ex:  # noqa: BLE001
                bad += 1
                if bad < 10:
                    print("FAIL", name, ex)
        print("ok", ok, "bad", bad, classes)
    else:
        dump(open(path, "rb").read())
