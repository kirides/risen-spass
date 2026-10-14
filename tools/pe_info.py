"""Print PE header facts relevant to hooking: image base, DllCharacteristics, CFG, debug/PDB, sections.

Usage: pe_info.py <pe> [<pe> ...]
"""
import sys

import pefile

DLL_CHARS = {
    0x0020: "HIGH_ENTROPY_VA", 0x0040: "DYNAMIC_BASE(ASLR)", 0x0080: "FORCE_INTEGRITY",
    0x0100: "NX_COMPAT", 0x0200: "NO_ISOLATION", 0x0400: "NO_SEH", 0x0800: "NO_BIND",
    0x4000: "GUARD_CF", 0x8000: "TERMINAL_SERVER_AWARE",
}


def main() -> None:
    for p in sys.argv[1:]:
        pe = pefile.PE(p, fast_load=True)
        pe.parse_data_directories(directories=[
            pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_DEBUG"],
            pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG"],
            pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_SECURITY"],
            pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_TLS"],
        ])
        oh = pe.OPTIONAL_HEADER
        flags = [n for b, n in DLL_CHARS.items() if oh.DllCharacteristics & b]
        print(f"== {p}")
        print(f"  machine=0x{pe.FILE_HEADER.Machine:x} imagebase=0x{oh.ImageBase:x} size=0x{oh.SizeOfImage:x} "
              f"entry_rva=0x{oh.AddressOfEntryPoint:x} subsystem={oh.Subsystem}")
        print(f"  DllCharacteristics=0x{oh.DllCharacteristics:x}: {', '.join(flags)}")
        sec = pe.OPTIONAL_HEADER.DATA_DIRECTORY[pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_SECURITY"]]
        print(f"  authenticode_present={sec.Size != 0}")
        for d in getattr(pe, "DIRECTORY_ENTRY_DEBUG", []):
            e = getattr(d, "entry", None)
            if e is not None and hasattr(e, "PdbFileName"):
                print(f"  pdb={e.PdbFileName.rstrip(b'\0').decode(errors='replace')}")
        print("  sections: " + ", ".join(
            f"{s.Name.rstrip(b'\0').decode()}(rva=0x{s.VirtualAddress:x},vs=0x{s.Misc_VirtualSize:x})"
            for s in pe.sections))
        print(f"  TLS={'yes' if hasattr(pe, 'DIRECTORY_ENTRY_TLS') else 'no'} "
              f"version={pe.FILE_HEADER.TimeDateStamp}")


if __name__ == "__main__":
    main()
