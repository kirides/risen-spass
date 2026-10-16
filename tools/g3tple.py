#!/usr/bin/env python3
"""Offline reader for Risen entity templates (*.tple, "GENOMFLE"/"GENOMETP" files). docs/RE.md section 43.

File layout (little endian):
  "GENOMFLE" u16 1  u32 string_table_offset       file header
  "GENOMETP" u16 0xdb ... 16 byte GUID ...      template header
  property data                                  nested records, see below
  at string_table_offset:  u32 0xDEADBEEF, u8 version, u32 string_count,
                           string_count x (u16 length, bytes)   -- the file's string table, ends exactly at EOF
Entry 0 of the table is the template name, entry 1 the entity name; property sets, property names and type names follow
in order of first use. Property data refers to strings by index.

A property is   u16 name_index, u16 type_index, u16 0x1e, u32 size, payload[size]
  type "int"/"long" i32, "short" i16, "float" f32, "bool" u8, "bCVector" 3 x f32, "bCFloatColor" 4 x f32,
  "bCString"/"eCScriptProxyScript"/"gCScriptProxy*" u16 string index (0xffff = empty),
  "bTPropertyContainer<enum X>" u16 0xc9 + u32 value (the enum bits), nested containers (bTObjArray<class X>, class
  records) are entered, their members are properties again.
A property set (eCMesh_PS, gCItem_PS, ...) starts after the six bytes 01 00 01 01 00 01 followed by u16 name_index.

This is a tolerant scanner, not a full parser: it walks the stream, recognises property headers and resynchronises byte
by byte; check the result against the engine (risen.template.get) before trusting a value of an unfamiliar type.

Usage:
  python -I tools/g3tple.py dump <file.tple>              all properties as  Set.Name = value
  python -I tools/g3tple.py dump <pak> <path-in-pak>      the same for a template inside a packed archive
Library: parse(bytes) -> Template with .name, .guid ("@" + 32 hex, how other templates refer to this one), .strings, .props (list of Prop(set, name, type, value, off, size)).
"""
import struct
import sys

PRIMS = {"int", "long", "short", "float", "bool", "bCVector", "bCFloatColor", "bCString", "unsigned long", "unsigned int",
         "char", "eCTemplateEntityProxy", "eCEntityProxy", "eCGuiBitmapProxy2", "eCScriptProxyScript",
         "gCScriptProxyAIState", "gCScriptProxyAIFunction", "gCScriptProxyAICallback", "bCMatrix", "bCQuaternion",
         "bCBox", "bCRect", "bCEulerAngles", "bCFloatAlphaColor", "bCImageResourceString", "bCRange1", "bCRange3",
         "bCVector2", "bCImageOrMaterialResourceString", "bCMeshResourceString", "bCSpeedTreeResourceString"}
STRING_TYPES = {"bCString", "eCScriptProxyScript", "gCScriptProxyAIState", "gCScriptProxyAIFunction",
                "gCScriptProxyAICallback", "eCTemplateEntityProxy", "eCEntityProxy", "eCGuiBitmapProxy2",
                "bCImageResourceString", "bCImageOrMaterialResourceString", "bCMeshResourceString",
                "bCSpeedTreeResourceString"}


class Prop:
    __slots__ = ("pset", "name", "type", "value", "off", "size")

    def __init__(self, pset, name, typ, value, off, size):
        self.pset, self.name, self.type, self.value, self.off, self.size = pset, name, typ, value, off, size

    @property
    def key(self):
        return "%s.%s" % (self.pset, self.name)

    def __repr__(self):
        return "%s.%s = %r" % (self.pset, self.name, self.value)


class Template:
    pass


def read_strings(b):
    size = struct.unpack_from("<I", b, 10)[0]
    p = size
    if p + 9 > len(b) or b[p:p + 4] != b"\xef\xbe\xad\xde":
        raise ValueError("no string table")
    cnt = struct.unpack_from("<I", b, p + 5)[0]
    q = p + 9
    strs = []
    for _ in range(cnt):
        n = struct.unpack_from("<H", b, q)[0]
        q += 2
        strs.append(b[q:q + n].decode("latin-1"))
        q += n
    return strs, p


def decode(typ, payload, strs):
    n = len(payload)
    try:
        if typ in ("int", "long") and n == 4:
            return struct.unpack("<i", payload)[0]
        if typ == "unsigned long" and n == 4 or typ == "unsigned int" and n == 4:
            return struct.unpack("<I", payload)[0]
        if typ == "short" and n == 2:
            return struct.unpack("<h", payload)[0]
        if typ == "float" and n == 4:
            return round(struct.unpack("<f", payload)[0], 5)
        if typ == "bool" and n == 1:
            return bool(payload[0])
        if typ in ("bCVector", "bCFloatColor", "bCEulerAngles", "bCQuaternion", "bCFloatAlphaColor", "bCRange1", "bCRange3",
                   "bCVector2", "bCBox", "bCMatrix") and n % 4 == 0:
            return tuple(round(x, 4) for x in struct.unpack("<%df" % (n // 4), payload))
        if typ in STRING_TYPES:
            if n == 2:
                i = struct.unpack("<H", payload)[0]
            elif n == 3:
                return ""
            elif n == 5 and payload[2] == 1:
                i = struct.unpack("<H", payload[3:])[0]
            elif n == 23 and payload[2] == 1:
                return "@" + payload[3:19].hex()  # reference to another template / entity by GUID (see Template.guid)
            else:
                return payload.hex()
            return "" if i == 0xFFFF else (strs[i] if i < len(strs) else "#%d" % i)
        if typ.startswith("bTPropertyContainer<enum") and n == 6:
            return struct.unpack("<I", payload[2:])[0]
    except Exception:
        pass
    return payload.hex()


def parse(b):
    strs, tbl = read_strings(b)
    t = Template()
    t.strings = strs
    t.name = strs[0] if strs else ""
    # a template holds several records "db 00 <16 byte GUID>"; references from other templates name the second one (the entity)
    t.guids = ["@" + b[i + 2:i + 18].hex() for i in range(0x1c, min(tbl, 0x200)) if b[i:i + 2] == bytes((0xDB, 0))]
    t.guid = t.guids[1] if len(t.guids) > 1 else (t.guids[0] if t.guids else "")
    t.props = []
    n = len(strs)
    ps_at = {}
    sig = b"\x01\x00\x01\x01\x00\x01"
    i = 0x2e
    while True:
        i = b.find(sig, i, tbl)
        if i < 0:
            break
        if i + 8 <= tbl:
            idx = struct.unpack_from("<H", b, i + 6)[0]
            if idx < n and (strs[idx].endswith("_PS") or strs[idx].startswith(("eC", "gC"))):
                ps_at[i] = strs[idx]
        i += 1
    pos = sorted(ps_at)
    cur = ""
    p = 0x2e
    pi = 0
    stack = []  # (end offset, container property name) of the arrays / records being walked
    while p + 10 <= tbl:
        while stack and p >= stack[-1][0]:
            stack.pop()
        while pi < len(pos) and pos[pi] <= p:
            cur = ps_at[pos[pi]]
            pi += 1
        ni, ti, mk, sz = struct.unpack_from("<HHHI", b, p)
        if mk == 0x1E and ni < n and ti < n and sz <= tbl - p - 10 and not strs[ni].startswith(("bT", "bC")) \
                and (strs[ti] in PRIMS or strs[ti].startswith(("bTPropertyContainer<", "bTObjArray", "bTValArray", "class ", "gC", "eC"))):
            typ = strs[ti]
            if typ in PRIMS or typ.startswith("bTPropertyContainer<"):
                payload = b[p + 10:p + 10 + sz]
                nm = "".join(x[1] + "." for x in stack) + strs[ni]
                t.props.append(Prop(cur, nm, typ, decode(typ, payload, strs), p, sz))
                p += 10 + sz
                continue
            stack.append((p + 10 + sz, strs[ni] + "[]"))
            p += 10
            continue
        p += 1
    return t


def main(argv):
    if len(argv) < 3 or argv[1] != "dump":
        print(__doc__)
        return 2
    if len(argv) == 3:
        data = open(argv[2], "rb").read()
    else:
        import os
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        import g3pak
        data = g3pak.Pak(argv[2]).read(argv[3])
    t = parse(data)
    print("#", t.name, len(t.props), "properties")
    for pr in t.props:
        print("%s.%s = %s" % (pr.pset, pr.name, pr.value))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
