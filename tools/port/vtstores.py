"""List 'lea rax,[rip+vt]; mov [reg+off],rax' pairs in functions, naming RTTI vtables.

Usage: python -I vtstores.py <exe> <vtables.tsv> start:end[,start:end]
"""

import pathlib
import sys
from typing import Any, cast

import capstone
import pefile

pe = cast("Any", pefile.PE(sys.argv[1], fast_load=True))
img = pe.get_memory_mapped_image()
with pathlib.Path(sys.argv[2]).open() as vtables_file:
    names = {
        int(a, 16): f"{n}@{o}"
        for a, o, n in (line.rstrip("\n").split("\t") for line in vtables_file)
    }
md = cast("Any", capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64))
for rng in sys.argv[3].split(","):
    f, e = (int(x, 16) for x in rng.split(":"))
    print("==", hex(f))
    pend = {}
    for i in md.disasm(img[f:e], f):
        s = i.op_str
        if i.mnemonic == "lea" and "rip +" in s:
            reg = s.split(",")[0]
            t = i.address + i.size + int(s.split("rip + ")[1].split("]")[0], 16)
            if t in names:
                pend[reg] = names[t]
        elif i.mnemonic == "mov" and s.startswith("qword ptr [") and s.split(", ")[-1] in pend:
            print(f"  {i.address:x} {s:40} <- {pend[s.split(', ')[-1]]}")
        elif i.mnemonic == "call":
            print(f"  {i.address:x} call {s}")
