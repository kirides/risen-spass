// Headless helper: which script/string is a global data object initialised with?
//
// Script args: <outfile> <rva> [<rva> ...]   (rva of a data address, relative to the image base, e.g. 0x2cf2b0)
// For every reference to the address it prints the function and the instructions around the reference (the dynamic initialiser
// constructs e.g. an eCScriptProxyScript there from a string, so the string label shows up in the listing).
// Usage: see tools/ghidra/DumpDecomp.java (same invocation, -postScript DataInit.java out.txt 0x2cf2b0)
//@category Risen

import java.io.FileWriter;
import java.io.PrintWriter;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;

public class DataInit extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        PrintWriter out = new PrintWriter(new FileWriter(args[0], true));
        for (int i = 1; i < args.length; i++) {
            Address a = currentProgram.getImageBase().add(Long.decode(args[i]));
            out.println("== data " + args[i] + " at " + a);
            ReferenceIterator ri = currentProgram.getReferenceManager().getReferencesTo(a);
            while (ri.hasNext()) {
                Reference r = ri.next();
                Function c = currentProgram.getFunctionManager().getFunctionContaining(r.getFromAddress());
                out.println("  " + r.getReferenceType() + " from " + r.getFromAddress() + "  in " + (c != null ? c.getName(true) : "<none>"));
                Instruction ins = currentProgram.getListing().getInstructionAt(r.getFromAddress());
                if (ins == null) continue;
                Instruction p = ins;
                for (int k = 0; k < 6 && p != null; k++) {
                    Instruction q = p.getPrevious();
                    if (q == null) break;
                    p = q;
                }
                for (int k = 0; k < 10 && p != null; k++) {
                    StringBuilder extra = new StringBuilder();
                    for (Reference rf : p.getReferencesFrom()) {
                        ghidra.program.model.listing.Data d = currentProgram.getListing().getDataAt(rf.getToAddress());
                        if (d != null && d.hasStringValue()) extra.append("   ; \"").append(d.getValue()).append("\"");
                    }
                    out.println("      " + p.getAddress() + "  " + p + extra);
                    p = p.getNext();
                }
            }
        }
        out.close();
    }
}
