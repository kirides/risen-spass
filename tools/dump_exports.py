"""Dump PE export tables (mangled + undecorated-ish) to text files.

Usage: dump_exports.py <out_dir> <dll> [<dll> ...]
Writes <out_dir>/<dll_name>.exports.txt with "rva  ordinal  name" per line, plus a
summary of the top-level classes (parsed from MSVC-mangled names).
"""
import collections
import pathlib
import re
import sys

import pefile

# pefile silently stops resolving export *names* after 8192 symbols (anti-DoS default, bound as a
# constructor argument `max_symbol_exports`), which leaves thousands of exports as "#ordNNNN".
# Engine.dll alone has ~14 800, so the limit is raised.
MAX_EXPORTS = 1_000_000
pefile.MAX_SYMBOL_NAME_LENGTH = 0x2000  # decorated template names can exceed 512 chars

CLASS_RE = re.compile(r"^\?{1,2}[^@]+@([A-Za-z_][A-Za-z0-9_]*)@@")


def main() -> None:
    out_dir = pathlib.Path(sys.argv[1])
    out_dir.mkdir(parents=True, exist_ok=True)
    for dll in sys.argv[2:]:
        path = pathlib.Path(dll)
        pe = pefile.PE(str(path), fast_load=True, max_symbol_exports=MAX_EXPORTS)
        pe.parse_data_directories(
            directories=[pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_EXPORT"],
                         pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_IMPORT"]])
        exp = getattr(pe, "DIRECTORY_ENTRY_EXPORT", None)
        syms = exp.symbols if exp else []
        classes = collections.Counter()
        lines = []
        for s in syms:
            name = s.name.decode() if s.name else f"#ord{s.ordinal}"
            lines.append(f"{s.address:08x} {s.ordinal:5d} {name}")
            m = CLASS_RE.match(name)
            if m:
                classes[m.group(1)] += 1
        imports = []
        for imp in getattr(pe, "DIRECTORY_ENTRY_IMPORT", []):
            imports.append(imp.dll.decode())
        (out_dir / f"{path.stem}.exports.txt").write_text(
            f"# {path.name} machine=0x{pe.FILE_HEADER.Machine:x} "
            f"imagebase=0x{pe.OPTIONAL_HEADER.ImageBase:x} exports={len(syms)}\n"
            f"# imports: {', '.join(imports)}\n"
            f"# classes: {len(classes)}\n"
            + "\n".join(lines) + "\n", encoding="utf-8")
        print(f"{path.name}: {len(syms)} exports, {len(classes)} classes; imports: {', '.join(imports)}")


if __name__ == "__main__":
    main()
