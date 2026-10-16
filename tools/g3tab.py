#!/usr/bin/env python3
"""Offline reader for Risen's compiled string tables (compiled/strings/*.tab, magic "TAB0"). docs/RE.md section 43.

Layout (little endian): "TAB0", u16 version (1), u16 1, FILETIME (8 bytes), u32 column_count, then per column
    u8 1, u8 1, u8 0, u16 name_length, name (UTF-16LE),  u32 row_count, row_count x (u16 length, UTF-16LE text)
Column 0 is "ID"; the other columns are the languages (German_Text, English_Text, ...). Rows are the same in every column.

Usage:
  python -I tools/g3tab.py dump <file.tab>                         id <TAB> column <TAB> text, one line per cell
  python -I tools/g3tab.py dump <archive.pak> <name.tab>
Library: read(bytes) -> (columns: list of names, rows: dict id -> {column: text})
"""
import struct
import sys


def read(b):
    if b[:4] != b"TAB0":
        raise ValueError("not a TAB0 string table")
    p = 16
    ncols = struct.unpack_from("<I", b, p)[0]
    p += 4
    names, cols = [], []
    for _ in range(ncols):
        p += 3
        n = struct.unpack_from("<H", b, p)[0]
        p += 2
        names.append(b[p:p + 2 * n].decode("utf-16le"))
        p += 2 * n
        cnt = struct.unpack_from("<I", b, p)[0]
        p += 4
        col = []
        for _ in range(cnt):
            n = struct.unpack_from("<H", b, p)[0]
            p += 2
            col.append(b[p:p + 2 * n].decode("utf-16le"))
            p += 2 * n
        cols.append(col)
    if p != len(b):
        raise ValueError("%d trailing bytes" % (len(b) - p))
    rows = {}
    for i, rid in enumerate(cols[0]):
        rows[rid] = {names[c]: cols[c][i] for c in range(1, ncols)}
    return names, rows


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
    names, rows = read(data)
    out = sys.stdout
    if hasattr(out, "reconfigure"):
        out.reconfigure(encoding="utf-8")
    for rid, cells in rows.items():
        for col, text in cells.items():
            out.write("%s\t%s\t%s\n" % (rid, col, text.replace("\n", "\\n").replace("\r", "\\r")))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
