#!/usr/bin/env python3
"""Lossless reader/writer for compiled_combatspecies.bin (docs/RE.md section 43: the engine's eCDocArchive "V001" in the GENOMFLE container;
written for the Extended Mod research, RESEARCH_combat.md). Used by tools/extmod_gen.py to patch the user's own file.

parse_file(path) -> doc;  write_file(doc) -> bytes.  parse -> write reproduces the file byte for byte (checked by selftest()).

doc  = {"strs": [...], "flag": int, "species": [Species...]}
Species = {"name": "Human.xcs", "ft": (lo, hi), "obj": Obj (gCCombatSpecies props), "ver": u16, "styles": [Obj], "moves": [Obj], "ais": [Obj],
           "acc": accessor header ints}
Obj  = {"cls": str|None, "v0": u16, "v1": u16, "props": [Prop], "extra": bytes, "acc": (u16,u8,u16,u8,u16)|None}
Prop = {"n": name, "t": type, "pv": u16, "v": value, "enc": form}
value kinds: float/int/bool, str|None (strings; enc = payload size 2/3/5), ("proxy", None|guidhex) , ("enum", Type, int), ("floats", [..]),
             ("arr", ver_u8, [Obj]), ("obj", Obj)
"""
import struct, sys, collections

OD = collections.OrderedDict
STRT = ("bCString", "gCScriptProxyAIFunction", "eCScriptProxyScript", "gCScriptProxyAIState", "gCScriptProxyAICallback")


class R:
    def __init__(self, b, strs):
        self.b, self.p, self.strs = b, 0, strs

    def u8(self):
        v = self.b[self.p]; self.p += 1; return v

    def u16(self):
        v = struct.unpack_from("<H", self.b, self.p)[0]; self.p += 2; return v

    def u32(self):
        v = struct.unpack_from("<I", self.b, self.p)[0]; self.p += 4; return v

    def s(self):
        i = self.u16()
        return None if i == 0xFFFF else self.strs[i]


def read_strings(b):
    off = struct.unpack_from("<I", b, 10)[0]
    assert b[off:off + 4] == b"\xef\xbe\xad\xde"
    assert b[off + 4] == 1
    cnt = struct.unpack_from("<I", b, off + 5)[0]
    q = off + 9
    out = []
    for _ in range(cnt):
        n = struct.unpack_from("<H", b, q)[0]; q += 2
        out.append(b[q:q + n].decode("latin-1")); q += n
    assert q == len(b)
    return out, off


def dec_payload(r, typ, size, end):
    b, p0 = r.b, r.p
    if typ == "float":
        assert size == 4
        v, enc = struct.unpack_from("<f", b, p0)[0], None
    elif typ == "bool":
        assert size == 1
        v, enc = bool(b[p0]), None
    elif typ in ("long", "int"):
        assert size == 4
        v, enc = struct.unpack_from("<i", b, p0)[0], None
    elif typ == "short":
        assert size == 2
        v, enc = struct.unpack_from("<h", b, p0)[0], None
    elif typ in STRT:
        enc = size
        if size == 2:
            i = struct.unpack_from("<H", b, p0)[0]
        elif size == 3:
            assert b[p0:p0 + 3] == b"\x01\x00\x00"
            i = 0xFFFF
        elif size == 5:
            assert b[p0:p0 + 3] == b"\x01\x00\x01"
            i = struct.unpack_from("<H", b, p0 + 3)[0]
        else:
            raise ValueError("string size")
        v = None if i == 0xFFFF else r.strs[i]
    elif typ == "eCTemplateEntityProxy":
        enc = size
        if size == 3:
            assert b[p0:p0 + 3] == b"\x02\x00\x00"
            v = ("proxy", None)
        else:
            assert size == 23 and b[p0:p0 + 3] == b"\x02\x00\x01" and b[p0 + 19:p0 + 23] == b"\0\0\0\0"
            v = ("proxy", b[p0 + 3:p0 + 19].hex())
    elif typ.startswith("bTPropertyContainer<enum"):
        assert size == 6 and struct.unpack_from("<H", b, p0)[0] == 0xC9
        v, enc = ("enum", typ[len("bTPropertyContainer<enum "):-1], struct.unpack_from("<I", b, p0 + 2)[0]), None
    elif typ in ("bCVector2", "bCVector", "bCFloatColor", "bCRange1"):
        assert size % 4 == 0
        v, enc = ("floats", list(struct.unpack_from("<%df" % (size // 4), b, p0))), None
    elif typ.startswith("bTObjArray<class"):
        ver = r.u8(); n = r.u32()
        v, enc = ("arr", ver, [read_body(r, None, True) for _ in range(n)]), None
    elif typ.startswith(("g", "e")):
        v, enc = ("obj", read_body(r, None, True)), None
    else:
        raise ValueError("unknown type " + typ)
    assert r.p == end or typ in STRT or True
    r.p = end
    return v, enc


def read_body(r, cls, nested=False):
    v0 = r.u16()
    size = r.u32()
    end = r.p + size
    v1 = r.u16()
    n = r.u32()
    props = []
    for _ in range(n):
        name = r.s(); typ = r.s(); pv = r.u16(); sz = r.u32()
        pend = r.p + sz
        val, enc = dec_payload(r, typ, sz, pend)
        props.append({"n": name, "t": typ, "pv": pv, "v": val, "enc": enc})
    o = {"cls": cls, "v0": v0, "v1": v1, "props": props, "extra": b"", "end": end, "acc": None}
    if nested:
        o["extra"] = r.b[r.p:end]
        r.p = end
    return o


def read_accessor(r, jump=True):
    v = r.u16()
    present = r.u8()
    assert present == 1
    a = r.u16(); bb = r.u8()
    cls = r.s()
    c = r.u16(); d = r.u8(); e = r.u16()
    o = read_body(r, cls)
    o["acc"] = (v, present, a, bb, c, d, e)
    if jump:
        o["extra"] = r.b[r.p:o["end"]]
        r.p = o["end"]
    return o


def parse_bytes(b):
    strs, tbl = read_strings(b)
    assert b[:8] == b"GENOMFLE" and struct.unpack_from("<H", b, 8)[0] == 1
    r = R(b, strs)
    r.p = 14
    assert r.u32() == 0x31303056
    flag = r.u8()
    cnt = r.u32()
    heads = []
    for _ in range(cnt):
        lo = r.u32(); hi = r.u32(); nm = r.s()
        heads.append((nm, lo, hi))
    species = []
    for i in range(cnt):
        o = read_accessor(r, jump=False)
        sp = {"name": heads[i][0], "ft": (heads[i][1], heads[i][2]), "obj": o}
        sp["ver"] = r.u16()
        sp["styles"] = [read_accessor(r) for _ in range(r.u32())]
        sp["moves"] = [read_accessor(r) for _ in range(r.u32())]
        sp["ais"] = [read_accessor(r) for _ in range(r.u32())]
        assert r.p == o["end"], "species end"
        species.append(sp)
    assert r.p == tbl
    return {"strs": strs, "flag": flag, "species": species, "ver_hdr": struct.unpack_from("<H", b, 8)[0]}


def parse_file(path):
    return parse_bytes(open(path, "rb").read())


class W:
    def __init__(self, strs):
        self.strs = list(strs)
        self.idx = {}
        for i, s in enumerate(self.strs):
            self.idx.setdefault(s, i)
        self.out = bytearray()

    def sid(self, s):
        if s is None:
            return 0xFFFF
        if s not in self.idx:
            self.idx[s] = len(self.strs)
            self.strs.append(s)
        return self.idx[s]


def enc_payload(w, typ, v, enc):
    if typ == "float":
        return struct.pack("<f", v)
    if typ == "bool":
        return bytes([1 if v else 0])
    if typ in ("long", "int"):
        return struct.pack("<i", v)
    if typ == "short":
        return struct.pack("<h", v)
    if typ in STRT:
        if v is None:
            return b"\x01\x00\x00" if enc == 3 else struct.pack("<H", 0xFFFF) if enc == 2 else b"\x01\x00\x01\xff\xff"
        i = w.sid(v)
        if enc == 2:
            return struct.pack("<H", i)
        return b"\x01\x00\x01" + struct.pack("<H", i)
    if typ == "eCTemplateEntityProxy":
        if v[1] is None:
            return b"\x02\x00\x00"
        return b"\x02\x00\x01" + bytes.fromhex(v[1]) + b"\0\0\0\0"
    if typ.startswith("bTPropertyContainer<enum"):
        return struct.pack("<HI", 0xC9, v[2])
    if typ in ("bCVector2", "bCVector", "bCFloatColor", "bCRange1"):
        return struct.pack("<%df" % len(v[1]), *v[1])
    if typ.startswith("bTObjArray<class"):
        return bytes([v[1]]) + struct.pack("<I", len(v[2])) + b"".join(enc_body(w, o) for o in v[2])
    if typ.startswith(("g", "e")):
        return enc_body(w, v[1])
    raise ValueError(typ)


def enc_props(w, props):
    out = bytearray()
    out += struct.pack("<I", len(props))
    for p in props:
        pl = enc_payload(w, p["t"], p["v"], p["enc"])
        out += struct.pack("<HHH", w.sid(p["n"]), w.sid(p["t"]), p["pv"]) + struct.pack("<I", len(pl)) + pl
    return bytes(out)


def enc_body(w, o, tail=b""):
    rest = struct.pack("<H", o["v1"]) + enc_props(w, o["props"]) + o["extra"] + tail
    return struct.pack("<HI", o["v0"], len(rest)) + rest


def enc_accessor(w, o, tail=b""):
    a = o["acc"]
    return struct.pack("<HBHBHBH", a[0], a[1], a[2], a[3], a[4], a[5], a[6]) if False else (
        struct.pack("<HB", a[0], a[1]) + struct.pack("<HB", a[2], a[3]) + struct.pack("<H", w.sid(o["cls"])) +
        struct.pack("<HBH", a[4], a[5], a[6]) + enc_body(w, o, tail))


def write_bytes(doc):
    w = W(doc["strs"])
    # accessor header: v, present, a, bb, [cls], c, d, e  -> rewrite precisely as read
    body = bytearray()
    heads = bytearray()
    for sp in doc["species"]:
        heads += struct.pack("<II", sp["ft"][0], sp["ft"][1]) + struct.pack("<H", w.sid(sp["name"]))
    for sp in doc["species"]:
        tail = struct.pack("<H", sp["ver"])
        tail += struct.pack("<I", len(sp["styles"])) + b"".join(enc_accessor(w, s2) for s2 in sp["styles"])
        tail += struct.pack("<I", len(sp["moves"])) + b"".join(enc_accessor(w, s2) for s2 in sp["moves"])
        tail += struct.pack("<I", len(sp["ais"])) + b"".join(enc_accessor(w, s2) for s2 in sp["ais"])
        o = sp["obj"]
        a = o["acc"]
        rest = struct.pack("<H", o["v1"]) + enc_props(w, o["props"]) + tail
        body += struct.pack("<HB", a[0], a[1]) + struct.pack("<HB", a[2], a[3]) + struct.pack("<H", w.sid(o["cls"]))
        body += struct.pack("<HBH", a[4], a[5], a[6]) + struct.pack("<HI", o["v0"], len(rest)) + rest
    data = b"V001"[::-1][::-1]  # 'V001' as u32 0x31303056 little endian = bytes 56 30 30 31
    data = struct.pack("<I", 0x31303056) + bytes([doc["flag"]]) + struct.pack("<I", len(doc["species"])) + bytes(heads) + bytes(body)
    tbl_off = 14 + len(data)
    out = bytearray(b"GENOMFLE" + struct.pack("<H", 1) + struct.pack("<I", tbl_off))
    out += data
    out += b"\xef\xbe\xad\xde\x01" + struct.pack("<I", len(w.strs))
    for s in w.strs:
        bs = s.encode("latin-1")
        out += struct.pack("<H", len(bs)) + bs
    return bytes(out)


def selftest(paths):
    for p in paths:
        b = open(p, "rb").read()
        d = parse_bytes(b)
        nb = write_bytes(d)
        print(p, len(b), len(nb), "IDENTICAL" if nb == b else "DIFFERENT")


if __name__ == "__main__":
    selftest(sys.argv[1:])
