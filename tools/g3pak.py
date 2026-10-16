#!/usr/bin/env python3
"""Offline reader for Risen's packed archives (G3V0 container: *.pak and the patch volumes *.p00, *.p01, ...).

Layout (docs/RE.md section 43), all integers little endian:

  header (0x30 bytes)
    u32 version = 1, char[4] "G3V0", u64 0, u64 flags (0 / 1 / 2, meaning not decoded), u64 data_offset = 0x30,
    u64 toc_offset, u64 file_size
  data      file bodies, each stored raw or as a zlib stream (78 9c ...), in the order written
  TOC       recursive tree at toc_offset
    root dir  : u32 name_len (0), FILETIME x3, u32 attr (0x20010), u32 child_count
    child     : u32 attr  (0x20010 directory, 0x20020 file), u32 name_len, name bytes + NUL
      directory: FILETIME x3, u32 attr, u32 child_count, children...
      file     : u64 offset, FILETIME x3, u32 attr, u64 0, u32 size_a, u32 size_b
                 (size_a = stored size in the archive, size_b = size after inflating; equal = stored raw)

Usage:
  python -I tools/g3pak.py list  <archive> [--long]
  python -I tools/g3pak.py cat   <archive> <path inside>          (bytes to stdout)
  python -I tools/g3pak.py extract <archive> <outdir> [glob]       (never extracts onto an existing path outside outdir)
Library use: from g3pak import Pak
"""
import errno
import fnmatch
import mmap
import os
import struct
import sys
import zlib


class Entry:
    __slots__ = ("path", "offset", "stored", "size", "mtime")

    def __init__(self, path, offset, stored, size, mtime):
        self.path, self.offset, self.stored, self.size, self.mtime = path, offset, stored, size, mtime

    @property
    def compressed(self):
        return self.stored != self.size


class Pak:
    def __init__(self, path):
        self.path = path
        with open(path, "rb") as f:
            self.buf = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)
        b = self.buf
        ver, magic, z, self.flags, self.data_off, self.toc_off, self.total = struct.unpack_from("<I4sQQQQQ", b, 0)
        if magic != b"G3V0":
            raise ValueError("%s: not a G3V0 archive" % path)
        self.entries = []
        self._parse()
        self.by_path = {e.path.lower(): e for e in self.entries}

    def _parse(self):
        b = self.buf
        p = self.toc_off
        nl = struct.unpack_from("<I", b, p)[0]
        p += 4 + (nl + 1 if nl else 0)
        p += 24  # FILETIME x3
        attr, count = struct.unpack_from("<II", b, p)
        p += 8
        p = self._dir("", count, p)
        if p != self.total and p != len(b):
            raise ValueError("%s: TOC ends at %#x, file size %#x" % (self.path, p, self.total))

    def _dir(self, prefix, count, p):
        b = self.buf
        for _ in range(count):
            attr, nl = struct.unpack_from("<II", b, p)
            p += 8
            name = b[p:p + nl].decode("latin-1")
            p += nl + 1
            full = prefix + name
            if attr & 0x10:
                p += 24
                a2, cnt = struct.unpack_from("<II", b, p)
                p += 8
                p = self._dir(full + "/", cnt, p)
            else:
                off = struct.unpack_from("<Q", b, p)[0]
                p += 8
                mt = struct.unpack_from("<Q", b, p + 16)[0]  # last write FILETIME
                p += 24
                a2, z, s1, s2 = struct.unpack_from("<IQII", b, p)
                p += 20
                self.entries.append(Entry(full, off, s1, s2, mt))
        return p

    def close(self):
        self.buf.close()

    def __enter__(self):
        return self

    def __exit__(self, *a):
        self.close()

    def read(self, entry):
        if isinstance(entry, str):
            entry = self.by_path[entry.lower()]
        raw = bytes(self.buf[entry.offset:entry.offset + entry.stored])
        if entry.compressed:
            raw = zlib.decompress(raw)
            if len(raw) != entry.size:
                raise ValueError("%s: inflated %d bytes, TOC says %d" % (entry.path, len(raw), entry.size))
        return raw


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    cmd, arc = argv[1], argv[2]
    pak = Pak(arc)
    if cmd == "list":
        for e in pak.entries:
            if "--long" in argv:
                print("%10d %10d %s %s" % (e.size, e.stored, "z" if e.compressed else "-", e.path))
            else:
                print(e.path)
    elif cmd == "cat":
        sys.stdout.buffer.write(pak.read(argv[3]))
    elif cmd == "extract":
        out = os.path.abspath(argv[3])
        pat = argv[4] if len(argv) > 4 else "*"
        n = 0
        for e in pak.entries:
            if not fnmatch.fnmatch(e.path.lower(), pat.lower()):
                continue
            dst = os.path.abspath(os.path.join(out, e.path))
            if not dst.startswith(out + os.sep):
                print("skip (escapes outdir):", e.path)
                continue
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            with open(dst, "wb") as f:
                f.write(pak.read(e))
            n += 1
        print("extracted", n, "files")
    else:
        print(__doc__)
        return 2
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main(sys.argv))
    except OSError as ex:
        if ex.errno not in (errno.EPIPE, errno.EINVAL):   # `| head` (Windows reports EINVAL)
            raise
        sys.exit(0)
