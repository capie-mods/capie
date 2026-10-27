"""For each slot of a vtable, disassemble the target (follow one jmp) and report displacements in a set.
Usage: python -I vtscan.py <exe> vt_rva nslots disp,disp..."""
import sys, struct, capstone, pefile
pe = pefile.PE(sys.argv[1], fast_load=True); img = pe.get_memory_mapped_image(); IB = pe.OPTIONAL_HEADER.ImageBase
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
vt = int(sys.argv[2], 16); n = int(sys.argv[3]); want = {int(x, 16) for x in sys.argv[4].split(",")}
for k in range(n):
    f = struct.unpack_from("<Q", img, vt + 8 * k)[0] - IB
    if not (0x1000 <= f < len(img)): print("end at slot", hex(8 * k)); break
    hits = []
    for depth in range(2):
        for i in md.disasm(img[f:f + 0x600], f):
            for w in want:
                if f"+ {hex(w)}]" in i.op_str or i.op_str.endswith(f", {hex(w)}"): hits.append(f"{i.address:x}:{i.mnemonic} {i.op_str}")
            if i.mnemonic == "ret" or i.mnemonic == "int3": break
            if i.mnemonic == "jmp" and i.op_str.startswith("0x") and depth == 0:
                f = int(i.op_str, 16); break
        else: break
    if hits: print(hex(8 * k), hex(f), hits[:4])
