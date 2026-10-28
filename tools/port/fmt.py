"""For each FindX RVA, find the 'lea rdx,[rip+X]' feeding the log call; print the string at X."""

import sys
from typing import Any, cast

import capstone
import pefile

MIN_TEXT_LENGTH = 3
"""A string must be longer than this many bytes to be printed."""

FIRST_PRINTABLE = 32
"""Lowest code of printable ASCII."""

DEL_CODE = 127
"""First code after printable ASCII (DEL)."""

pe = cast("Any", pefile.PE(sys.argv[1], fast_load=True))
img = pe.get_memory_mapped_image()
md = cast("Any", capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64))
for arg in sys.argv[2].split(","):
    a = int(arg, 16)
    strs = []
    for i in md.disasm(img[a : a + 0x300], a):
        if i.mnemonic == "lea" and "rip +" in i.op_str:
            tgt = i.address + i.size + int(i.op_str.split("rip + ")[1].rstrip("]"), 16)
            s = img[tgt : tgt + 120].split(b"\0")[0]
            if len(s) > MIN_TEXT_LENGTH and all(FIRST_PRINTABLE <= c < DEL_CODE for c in s):
                strs.append(s.decode())
        if i.mnemonic == "ret":
            break
    print(hex(a), strs)
