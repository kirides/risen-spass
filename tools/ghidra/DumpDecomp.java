// Headless helper: decompile functions and dump callers/callees.
//
// Script args: <outfile> <spec> [<spec> ...]
//   spec = hex RVA relative to the image base        (e.g. 0x964460)
//        | name:<substring of function name>         (all matches, max 20)
//        | strref:<substring of a defined string>    (functions that reference the string)
//
// Usage (after the program was imported/analyzed once):
//   analyzeHeadless <projdir> <proj> -process <file> -noanalysis -readOnly \
//       -scriptPath <this dir> -postScript DumpDecomp.java out.txt 0x964460 name:LoadScriptDLL
//@category Risen

import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.LinkedHashSet;
import java.util.Set;

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.data.StringDataInstance;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.DataIterator;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;

public class DumpDecomp extends GhidraScript {

    private DecompInterface decomp;
    private PrintWriter out;

    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 2) {
            printerr("usage: DumpDecomp.java <outfile> <spec>...");
            return;
        }
        decomp = new DecompInterface();
        decomp.openProgram(currentProgram);
        out = new PrintWriter(new FileWriter(args[0], true));
        out.println("##### " + currentProgram.getName() + " imageBase=" + currentProgram.getImageBase());

        Set<Function> funcs = new LinkedHashSet<>();
        for (int i = 1; i < args.length; i++) {
            String spec = args[i];
            if (spec.startsWith("name:")) {
                String q = spec.substring(5).toLowerCase();
                int n = 0;
                FunctionIterator it = currentProgram.getFunctionManager().getFunctions(true);
                while (it.hasNext() && n < 20) {
                    Function f = it.next();
                    if (f.getName(true).toLowerCase().contains(q)) {
                        funcs.add(f);
                        n++;
                    }
                }
            } else if (spec.startsWith("strref:")) {
                String q = spec.substring(7);
                DataIterator di = currentProgram.getListing().getDefinedData(true);
                while (di.hasNext()) {
                    Data d = di.next();
                    if (!StringDataInstance.isString(d)) continue;
                    Object v = d.getValue();
                    if (v == null || !v.toString().contains(q)) continue;
                    out.println("// string @ " + d.getAddress() + " = \"" + v + "\"");
                    ReferenceIterator ri = currentProgram.getReferenceManager().getReferencesTo(d.getAddress());
                    while (ri.hasNext()) {
                        Reference r = ri.next();
                        Function f = currentProgram.getFunctionManager().getFunctionContaining(r.getFromAddress());
                        if (f != null) funcs.add(f);
                    }
                }
            } else {
                long rva = Long.decode(spec);
                Address a = currentProgram.getImageBase().add(rva);
                Function f = currentProgram.getFunctionManager().getFunctionContaining(a);
                if (f == null) {
                    out.println("// no function at RVA " + spec + " (" + a + ")");
                } else {
                    funcs.add(f);
                }
            }
        }

        for (Function f : funcs) {
            dump(f);
        }
        out.close();
    }

    private void dump(Function f) throws Exception {
        long rva = f.getEntryPoint().subtract(currentProgram.getImageBase());
        out.println();
        out.println("// ================================================================");
        out.println("// " + f.getName(true) + "  entry=" + f.getEntryPoint() + "  rva=0x" + Long.toHexString(rva));
        out.println("// " + f.getSignature().getPrototypeString());
        StringBuilder callers = new StringBuilder();
        int n = 0;
        ReferenceIterator ri = currentProgram.getReferenceManager().getReferencesTo(f.getEntryPoint());
        while (ri.hasNext() && n < 12) {
            Reference r = ri.next();
            Function c = currentProgram.getFunctionManager().getFunctionContaining(r.getFromAddress());
            callers.append(c != null ? c.getName(true) : r.getFromAddress().toString()).append("; ");
            n++;
        }
        out.println("// callers: " + callers);
        DecompileResults res = decomp.decompileFunction(f, 120, monitor);
        if (res.decompileCompleted()) {
            out.println(res.getDecompiledFunction().getC());
        } else {
            out.println("// decompile failed: " + res.getErrorMessage());
        }
        out.flush();
    }
}
