#!/usr/bin/env python3
"""Self-test of the offline readers (no game data needed): builds a small G3V0 archive and a TAB0 table in memory
and reads them back. Run: python -I tools/test_g3.py"""
import os
import struct
import sys
import tempfile
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import g3pak  # noqa: E402
import g3tab  # noqa: E402
import g3tple  # noqa: E402

FT = b"\x00" * 8


def build_pak(files):
    """files: list of (path, bytes, compress). Directories are created from the paths."""
    body, entries = b"", []
    for path, data, comp in files:
        stored = zlib.compress(data) if comp else data
        entries.append((path, 0x30 + len(body), stored, len(data), comp))
        body += stored
    tree = {}
    for e in entries:
        node = tree
        parts = e[0].split("/")
        for d in parts[:-1]:
            node = node.setdefault(d, {})
        node[parts[-1]] = e

    def toc(node):
        out = struct.pack("<I", len(node))
        for name, v in node.items():
            nm = name.encode("latin-1")
            if isinstance(v, dict):
                out += struct.pack("<II", 0x20010, len(nm)) + nm + b"\0" + FT * 3 + struct.pack("<I", 0x20010) + toc(v)
            else:
                out += struct.pack("<II", 0x20020, len(nm)) + nm + b"\0" + struct.pack("<Q", v[1]) + FT * 3
                out += struct.pack("<IQII", 0x20020, 0x200000000 if v[4] else 0, len(v[2]), v[3])
        return out

    root = struct.pack("<I", 0) + FT * 3 + struct.pack("<I", 0x20010)
    t = root + toc(tree)
    t = t[:len(root)] + t[len(root):]  # root header already holds attr, toc() adds the child count
    toc_off = 0x30 + len(body)
    total = toc_off + len(t)
    head = struct.pack("<I4sQQQQQ", 1, b"G3V0", 0, 1, 0x30, toc_off, total)
    return head + body + t


def test_pak():
    data = [("Items/Sub/a.tple", b"hello world" * 10, True), ("b._xmsh", b"\x01\x02\x03", False), ("Items/c.txt", b"", False)]
    with tempfile.TemporaryDirectory() as d:
        p = os.path.join(d, "x.p00")
        open(p, "wb").write(build_pak(data))
        with g3pak.Pak(p) as pk:
            assert [e.path for e in pk.entries] == ["Items/Sub/a.tple", "Items/c.txt", "b._xmsh"], [e.path for e in pk.entries]
            assert pk.read("items/sub/A.tple") == data[0][1]
            assert pk.entries[0].compressed and not pk.entries[2].compressed
            assert pk.read("b._xmsh") == b"\x01\x02\x03"


def test_tab():
    def col(name, cells):
        o = b"\x01\x01\x00" + struct.pack("<H", len(name)) + name.encode("utf-16le") + struct.pack("<I", len(cells))
        for c in cells:
            o += struct.pack("<H", len(c)) + c.encode("utf-16le")
        return o
    b = b"TAB0" + struct.pack("<HH", 1, 1) + FT + struct.pack("<I", 3) + col("ID", ["A", "B"]) + col("German_Text", ["x", "yä"]) + col("English_Text", ["1", "2"])
    names, rows = g3tab.read(b)
    assert names == ["ID", "German_Text", "English_Text"]
    assert rows["B"] == {"German_Text": "yä", "English_Text": "2"}


def test_tple():
    strs = ["T.tple", "T", "gCItem_PS", "GoldValue", "long"]
    table = b"".join(struct.pack("<H", len(s)) + s.encode() for s in strs)
    # header (0x2e bytes), one property record GoldValue = 77 after a property-set marker, then the string table
    prop = struct.pack("<HHHI", 3, 4, 0x1E, 4) + struct.pack("<i", 77)
    body = b"\x00" * (0x2e - 0x0e) + b"\x01\x00\x01\x01\x00\x01" + struct.pack("<H", 2) + prop
    head = b"GENOMFLE" + struct.pack("<HI", 1, 0) + b"GENOMETP" + b"\xdb\x00" + bytes(range(16)) + body[:0]
    head = head.ljust(0x2e, b"\x00")
    data = head + b"\x01\x00\x01\x01\x00\x01" + struct.pack("<H", 2) + prop
    off = len(data)
    data = data[:10] + struct.pack("<I", off) + data[14:] + b"\xef\xbe\xad\xde\x01" + struct.pack("<I", len(strs)) + table
    t = g3tple.parse(data)
    assert t.name == "T.tple"
    assert [(p.pset, p.name, p.value) for p in t.props] == [("gCItem_PS", "GoldValue", 77)], t.props


if __name__ == "__main__":
    for f in (test_pak, test_tab, test_tple):
        f()
        print("ok", f.__name__)
