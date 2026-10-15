// Headless helper: print the function pointers of a vtable (or any pointer table).
//
// Script args: <outfile> <rva>:<count> [<rva>:<count> ...]
//   rva   = hex RVA of the table relative to the image base (e.g. 0x54420), or sym:<vftable symbol name> (first match,
//           e.g. sym:CFFFileSystemModule2::vftable; every match is listed when the name occurs more than once)
//   count = number of 8-byte entries
// Output per entry: index, offset in the table, target RVA and the name Ghidra knows (often FUN_...).
// Feed the target RVAs to DumpDecomp.java to read the functions.
//@category Risen

import java.io.FileWriter;
import java.io.PrintWriter;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;

public class DumpVtable extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 2) {
            printerr("usage: DumpVtable.java <outfile> <rva>:<count>...");
            return;
        }
        PrintWriter out = new PrintWriter(new FileWriter(args[0], true));
        out.println("##### " + currentProgram.getName() + " imageBase=" + currentProgram.getImageBase());
        long base = currentProgram.getImageBase().getOffset();
        for (int i = 1; i < args.length; i++) {
            String[] p = args[i].split(":(?=[0-9]+$)");
            long rva;
            if (p[0].startsWith("sym:")) {
                String want = p[0].substring(4);
                ghidra.program.model.symbol.SymbolIterator si = currentProgram.getSymbolTable().getSymbolIterator("*" + want.substring(want.lastIndexOf(':') + 1), true);
                long found = -1;
                while (si.hasNext()) {
                    ghidra.program.model.symbol.Symbol sy = si.next();
                    if (sy.getName(true).equals(want)) { found = sy.getAddress().getOffset() - base; break; }
                }
                if (found < 0) { out.println("// symbol not found: " + want); continue; }
                rva = found;
            } else {
                rva = Long.decode(p[0]);
            }
            int count = Integer.parseInt(p[1]);
            out.println("// table rva=0x" + Long.toHexString(rva) + " entries=" + count);
            for (int k = 0; k < count; k++) {
                Address a = currentProgram.getImageBase().add(rva + 8L * k);
                long v = currentProgram.getMemory().getLong(a);
                Address t = currentProgram.getImageBase().getNewAddress(v);
                Function f = currentProgram.getFunctionManager().getFunctionAt(t);
                out.println(String.format("[%3d] +0x%03x -> rva 0x%x  %s", k, 8 * k, v - base, f != null ? f.getName(true) : "?"));
            }
        }
        out.close();
    }
}
