"""For each FindX RVA, find the 'lea rdx,[rip+X]' feeding the log call and print the C string at X."""
import sys, capstone, pefile
pe = pefile.PE(sys.argv[1], fast_load=True); img = pe.get_memory_mapped_image()
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
for a in sys.argv[2].split(","):
    a = int(a, 16); strs = []
    for i in md.disasm(img[a:a + 0x300], a):
        if i.mnemonic == "lea" and "rip +" in i.op_str:
            tgt = i.address + i.size + int(i.op_str.split("rip + ")[1].rstrip("]"), 16)
            s = img[tgt:tgt + 120].split(b"\0")[0]
            if len(s) > 3 and all(32 <= c < 127 for c in s): strs.append(s.decode())
        if i.mnemonic == "ret": break
    print(hex(a), strs)
