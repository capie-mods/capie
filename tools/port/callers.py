"""List rel32 call/jmp sites targeting given RVAs.

Usage: python -I callers.py <exe> rva[,rva...] [max]
"""

import sys

import numpy as np  # ty: ignore[unresolved-import] - numpy is not in the ty environment
import pefile  # ty: ignore[unresolved-import] - pefile is not in the ty environment

CALL_OPCODE = 0xE8
"""Opcode byte of a near relative call (E8 rel32)."""
JMP_OPCODE = 0xE9
"""Opcode byte of a near relative jmp (E9 rel32)."""
MAX_ARG_INDEX = 3
"""Index in sys.argv of the optional maximum number of sites printed per target."""

pe = pefile.PE(sys.argv[1], fast_load=True)
t = next(s for s in pe.sections if s.Name.rstrip(b"\0") == b".text")
d = np.frombuffer(t.get_data(), np.uint8)
base = t.VirtualAddress
raw = t.get_data()
idx = np.arange(len(raw) - 5)
op = d[: len(raw) - 5]
d32 = (
    d[1 : len(raw) - 4].astype(np.int64)
    | (d[2 : len(raw) - 3].astype(np.int64) << 8)
    | (d[3 : len(raw) - 2].astype(np.int64) << 16)
    | (d[4 : len(raw) - 1].astype(np.int64) << 24)
)
d32 = np.where(d32 >= 2**31, d32 - 2**32, d32)
tgt = base + idx + 5 + d32
mx = int(sys.argv[MAX_ARG_INDEX]) if len(sys.argv) > MAX_ARG_INDEX else 15
for text in sys.argv[2].split(","):
    a = int(text, 16)
    hits = np.nonzero(((op == CALL_OPCODE) | (op == JMP_OPCODE)) & (tgt == a))[0]
    print(hex(a), len(hits), [hex(base + int(h)) for h in hits[:mx]])
