"""For each vtable slot, disassemble the target (follow one jmp) and report displacements in a set.

Usage: python -I vtscan.py <exe> vt_rva nslots disp,disp...
"""

import struct
import sys

import capstone  # ty: ignore[unresolved-import]
import pefile  # ty: ignore[unresolved-import]

MIN_CODE_RVA = 0x1000
"""First RVA treated as code; a slot whose target falls below it ends the table."""

pe = pefile.PE(sys.argv[1], fast_load=True)
img = pe.get_memory_mapped_image()
IB = pe.OPTIONAL_HEADER.ImageBase
"""Preferred image base of the executable, subtracted from absolute slot pointers."""
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
vt = int(sys.argv[2], 16)
n = int(sys.argv[3])
want = {int(x, 16) for x in sys.argv[4].split(",")}
for k in range(n):
    f = struct.unpack_from("<Q", img, vt + 8 * k)[0] - IB
    if not (MIN_CODE_RVA <= f < len(img)):
        print("end at slot", hex(8 * k))
        break
    hits = []
    for depth in range(2):
        for i in md.disasm(img[f : f + 0x600], f):
            hits.extend(
                f"{i.address:x}:{i.mnemonic} {i.op_str}"
                for w in want
                if f"+ {hex(w)}]" in i.op_str or i.op_str.endswith(f", {hex(w)}")
            )
            if i.mnemonic in {"ret", "int3"}:
                break
            if i.mnemonic == "jmp" and i.op_str.startswith("0x") and depth == 0:
                f = int(i.op_str, 16)
                break
        else:
            break
    if hits:
        print(hex(8 * k), hex(f), hits[:4])
