"""List 'lea rax,[rip+vt]; mov [reg+off],rax' pairs in functions, naming RTTI vtables.
Usage: python -I vtstores.py <exe> <vtables.tsv> start:end[,start:end]"""
import sys, capstone, pefile
pe = pefile.PE(sys.argv[1], fast_load=True); img = pe.get_memory_mapped_image()
names = {int(a, 16): f"{n}@{o}" for a, o, n in (l.rstrip("\n").split("\t") for l in open(sys.argv[2]))}
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
for rng in sys.argv[3].split(","):
    f, e = (int(x, 16) for x in rng.split(":")); print("==", hex(f)); pend = {}
    for i in md.disasm(img[f:e], f):
        s = i.op_str
        if i.mnemonic == "lea" and "rip +" in s:
            reg = s.split(",")[0]; t = i.address + i.size + int(s.split("rip + ")[1].split("]")[0], 16)
            if t in names: pend[reg] = names[t]
        elif i.mnemonic == "mov" and s.startswith("qword ptr [") and s.split(", ")[-1] in pend:
            print(f"  {i.address:x} {s:40} <- {pend[s.split(', ')[-1]]}")
        elif i.mnemonic == "call":
            print(f"  {i.address:x} call {s}")
