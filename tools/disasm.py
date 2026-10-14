"""Quick x64 disassembly of a function at an RVA straight from a PE file (no Ghidra needed).

Usage: disasm.py <pe> <rva-hex> [max_bytes=0x120]
Stops at the first `ret` that is followed by padding (int3/nop) or at max_bytes.
Resolves `call/jmp [rip+x]` targets against the import table and `lea/mov [rip+x]` against the export names.
"""
import sys

import capstone
import pefile


def main() -> None:
    path, rva = sys.argv[1], int(sys.argv[2], 16)
    limit = int(sys.argv[3], 16) if len(sys.argv) > 3 else 0x120
    pe = pefile.PE(path, fast_load=True, max_symbol_exports=1_000_000)
    pe.parse_data_directories(directories=[
        pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_IMPORT"],
        pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_EXPORT"]])
    imports = {}
    for d in getattr(pe, "DIRECTORY_ENTRY_IMPORT", []):
        for i in d.imports:
            imports[i.address - pe.OPTIONAL_HEADER.ImageBase] = f"{d.dll.decode()}!{(i.name or b'#%d' % i.ordinal).decode()}"
    exports = {s.address: s.name.decode() for s in getattr(pe, "DIRECTORY_ENTRY_EXPORT").symbols if s.name} \
        if hasattr(pe, "DIRECTORY_ENTRY_EXPORT") else {}
    data = pe.get_data(rva, limit)
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    md.detail = True
    for ins in md.disasm(data, rva):
        note = ""
        if ins.op_str.find("rip") >= 0:
            target = None
            for op in ins.operands:
                if op.type == capstone.x86.X86_OP_MEM and op.mem.base == capstone.x86.X86_REG_RIP:
                    target = ins.address + ins.size + op.mem.disp
            if target is not None:
                note = f"  ; [{target:#x}] " + imports.get(target, "")
        elif ins.mnemonic in ("call", "jmp") and ins.op_str.startswith("0x"):
            t = int(ins.op_str, 16)
            note = "  ; " + exports.get(t, "")
        print(f"{ins.address:08x}  {ins.bytes.hex():<24} {ins.mnemonic:<7} {ins.op_str}{note}")
        if ins.mnemonic == "ret":
            nxt = pe.get_data(ins.address + ins.size, 1)
            if nxt and nxt[0] in (0xCC, 0x90):
                break


if __name__ == "__main__":
    main()
