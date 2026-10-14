"""Demangle the *.exports.txt files produced by dump_exports.py using dbghelp.

Usage: demangle_exports.py <dir>
Writes <dir>/<name>.demangled.txt with "rva  ordinal  demangled" per line.
"""
import ctypes
import pathlib
import sys

UNDNAME_COMPLETE = 0x0000
UNDNAME_NO_MS_KEYWORDS = 0x0002
UNDNAME_NO_ACCESS_SPECIFIERS = 0x0080
UNDNAME_NO_MEMBER_TYPE = 0x0200
UNDNAME_NO_ALLOCATION_LANGUAGE = 0x0010

FLAGS = (UNDNAME_NO_MS_KEYWORDS | UNDNAME_NO_ALLOCATION_LANGUAGE
         | UNDNAME_NO_ACCESS_SPECIFIERS | UNDNAME_NO_MEMBER_TYPE)

dbghelp = ctypes.WinDLL("dbghelp.dll")
dbghelp.UnDecorateSymbolName.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint, ctypes.c_uint]
dbghelp.UnDecorateSymbolName.restype = ctypes.c_uint


def undecorate(name: str) -> str:
    if not name.startswith("?"):
        return name
    buf = ctypes.create_string_buffer(4096)
    n = dbghelp.UnDecorateSymbolName(name.encode(), buf, len(buf), FLAGS)
    return buf.value.decode(errors="replace") if n else name


def main() -> None:
    d = pathlib.Path(sys.argv[1])
    for f in d.glob("*.exports.txt"):
        out = []
        for line in f.read_text(encoding="utf-8").splitlines():
            if line.startswith("#"):
                out.append(line)
                continue
            rva, ordinal, name = line.split(" ", 2)[0], None, None
            parts = line.split()
            rva, ordinal, name = parts[0], parts[1], parts[2]
            out.append(f"{rva} {ordinal:>5} {undecorate(name)}")
        (d / f.name.replace(".exports.", ".demangled.")).write_text("\n".join(out) + "\n", encoding="utf-8")
        print(f"{f.name}: {len(out)} lines")


if __name__ == "__main__":
    main()
