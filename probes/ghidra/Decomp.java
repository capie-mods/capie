// Decompile functions at RVAs listed in a file. args: <listfile> <outfile>
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.cmd.function.CreateFunctionCmd;
import java.io.*;
import java.nio.file.*;
import java.util.*;
public class Decomp extends GhidraScript {
  public void run() throws Exception {
    String[] a = getScriptArgs();
    List<String> lines = Files.readAllLines(Paths.get(a[0]));
    PrintWriter pw = new PrintWriter(new FileWriter(a[1]));
    DecompInterface di = new DecompInterface();
    DecompileOptions opt = new DecompileOptions();
    di.setOptions(opt);
    di.toggleCCode(true); di.toggleSyntaxTree(true);
    di.openProgram(currentProgram);
    long base = currentProgram.getImageBase().getOffset();
    for (String ln : lines) {
      ln = ln.trim(); if (ln.isEmpty() || ln.startsWith("#")) continue;
      String[] parts = ln.split("\s+");
      long rva = Long.decode(parts[0]);
      String name = parts.length>1?parts[1]:null;
      Address ad = toAddr(base+rva);
      try {
        if (getInstructionAt(ad)==null) { new DisassembleCommand(ad,null,true).applyTo(currentProgram, monitor); }
        Function f = getFunctionAt(ad);
        if (f==null) { new CreateFunctionCmd(ad).applyTo(currentProgram, monitor); f=getFunctionAt(ad);}        
        if (f==null) { pw.println("// FAILED create "+Long.toHexString(rva)); continue; }
        if (name!=null) try { f.setName(name, ghidra.program.model.symbol.SourceType.USER_DEFINED);} catch(Exception e){}
        DecompileResults r = di.decompileFunction(f, 120, monitor);
        pw.println("// ===== RVA 0x"+Long.toHexString(rva)+" "+(name==null?"":name));
        if (r.decompileCompleted()) pw.println(r.getDecompiledFunction().getC()); else pw.println("// decompile failed: "+r.getErrorMessage());
      } catch (Exception e) { pw.println("// EXC "+Long.toHexString(rva)+" "+e); }
      pw.flush();
    }
    pw.close();
  }
}
