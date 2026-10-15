// Headless helper: list every caller (code reference) of the given functions.
//
// Script args: <outfile> <rva|name:substr> [...]
// Usage: see tools/ghidra/DumpDecomp.java (same invocation, -postScript Callers.java out.txt 0x8ca5c0 name:NotifyListeners)
//@category Risen

import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.LinkedHashSet;
import java.util.Set;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;

public class Callers extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        PrintWriter out = new PrintWriter(new FileWriter(args[0], true));
        for (int i = 1; i < args.length; i++) {
            String spec = args[i];
            Set<Function> targets = new LinkedHashSet<>();
            if (spec.startsWith("name:")) {
                String q = spec.substring(5).toLowerCase();
                FunctionIterator it = currentProgram.getFunctionManager().getFunctions(true);
                while (it.hasNext()) {
                    Function f = it.next();
                    if (f.getName(true).toLowerCase().contains(q)) targets.add(f);
                }
            } else {
                Address a = currentProgram.getImageBase().add(Long.decode(spec));
                Function f = currentProgram.getFunctionManager().getFunctionContaining(a);
                if (f != null) targets.add(f);
            }
            for (Function t : targets) {
                out.println("== " + t.getName(true) + " rva=0x" + Long.toHexString(t.getEntryPoint().subtract(currentProgram.getImageBase())));
                ReferenceIterator ri = currentProgram.getReferenceManager().getReferencesTo(t.getEntryPoint());
                while (ri.hasNext()) {
                    Reference r = ri.next();
                    Function c = currentProgram.getFunctionManager().getFunctionContaining(r.getFromAddress());
                    String where = c != null ? c.getName(true) + " (0x" + Long.toHexString(c.getEntryPoint().subtract(currentProgram.getImageBase())) + ")" : "<no function>";
                    String from;
                    try {
                        from = "0x" + Long.toHexString(r.getFromAddress().subtract(currentProgram.getImageBase()));
                    } catch (Exception ex) {
                        from = r.getFromAddress().toString();
                    }
                    out.println("   " + r.getReferenceType() + " from " + from + "  " + where);
                }
            }
        }
        out.close();
    }
}
