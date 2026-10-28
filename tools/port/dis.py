"""Helpers: find a masked hex prefix in .text and disassemble at RVAs of the new hoi4.exe.

Usage:
  python -I dis.py <exe> find <hex> [lo_rva hi_rva]
  python -I dis.py <exe> at <rva> [count]
"""

import re
import sys

import capstone  # ty: ignore[unresolved-import] - capstone is not in the ty environment
import pefile  # ty: ignore[unresolved-import] - pefile is not in the ty environment

LO_ARG_INDEX = 4
"""Index in sys.argv of the optional lower RVA bound of the find command."""
HI_ARG_INDEX = 5
"""Index in sys.argv of the optional upper RVA bound of the find command."""
COUNT_ARG_INDEX = 4
"""Index in sys.argv of the optional instruction count of the at command."""

exe, cmd = sys.argv[1], sys.argv[2]
pe = pefile.PE(exe, fast_load=True)
t = next(s for s in pe.sections if s.Name.rstrip(b"\0") == b".text")
data = t.get_data()
base = t.VirtualAddress
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)

if cmd == "find":
    h = sys.argv[3]
    lo = int(sys.argv[LO_ARG_INDEX], 16) - base if len(sys.argv) > LO_ARG_INDEX else 0
    hi = int(sys.argv[HI_ARG_INDEX], 16) - base if len(sys.argv) > HI_ARG_INDEX else len(data)
    pat = b"".join(
        b"." if h[i : i + 2] == "??" else re.escape(bytes([int(h[i : i + 2], 16)]))
        for i in range(0, len(h), 2)
    )
    hits = [hex(base + lo + m.start()) for m in re.finditer(pat, data[lo:hi], re.DOTALL)]
    print(len(hits), hits[:20])
else:
    for spec in sys.argv[3].split(","):
        rva = int(spec, 16)
        n = int(sys.argv[COUNT_ARG_INDEX]) if len(sys.argv) > COUNT_ARG_INDEX else 30
        print(f"--- {hex(rva)}")
        for k, i in enumerate(md.disasm(data[rva - base : rva - base + 16 * n], rva)):
            if k >= n:
                break
            extra = ""
            if i.mnemonic in ("call", "jmp") and i.op_str.startswith("0x"):
                extra = ""
            print(f"  {i.address:9x}  {i.bytes.hex():28} {i.mnemonic} {i.op_str}")
