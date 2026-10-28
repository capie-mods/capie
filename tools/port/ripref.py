"""Find a string (or RVA) in the image and every RIP-relative reference to it in .text.

Usage: python -I ripref.py <exe> str:<text> | rva:<hex>
"""

import sys
from typing import Any, cast

import numpy as np
import pefile

MAX_STRING_TARGETS = 5
"""Maximum number of occurrences of a string that are searched for references."""

pe = cast("Any", pefile.PE(sys.argv[1], fast_load=True))
img = pe.get_memory_mapped_image()
t = next(s for s in pe.sections if s.Name.rstrip(b"\0") == b".text")
raw = t.get_data()
d = np.frombuffer(raw, np.uint8)
base = t.VirtualAddress
n = len(raw) - 4
d32 = (
    d[0:n].astype(np.int64)
    | (d[1 : n + 1].astype(np.int64) << 8)
    | (d[2 : n + 2].astype(np.int64) << 16)
    | (d[3 : n + 3].astype(np.int64) << 24)
)
d32 = np.where(d32 >= 2**31, d32 - 2**32, d32)
for arg in sys.argv[2:]:
    kind, val = arg.split(":", 1)
    if kind == "str":
        b = val.encode() + b"\0"
        targets = []
        i = img.find(b)
        while i != -1 and len(targets) < MAX_STRING_TARGETS:
            if img[i - 1 : i] == b"\0":
                targets.append(i)
            i = img.find(b, i + 1)
    else:
        targets = [int(val, 16)]
    for T in targets:
        # disp at p: target = base + p + 4 + extra (0 for most lea/mov; imm after: 0..4)
        refs = []
        for extra in (0, 1, 4):
            refs += [
                int(base + p) for p in np.nonzero(base + np.arange(n) + 4 + extra + d32 == T)[0]
            ]
        print(arg, hex(T), len(refs), [hex(r) for r in sorted(refs)[:12]])
