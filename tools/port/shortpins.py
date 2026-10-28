"""Place short instruction pins inside their (anchored) function.

Usage: python -I shortpins.py <scan.json> <exe>
"""

import bisect
import json
import pathlib
import re
import sys

import capstone  # ty: ignore[unresolved-import]
import pefile  # ty: ignore[unresolved-import]

MIN_ANCHOR_BYTES = 16
"""Shortest pattern, in bytes, that can serve as a trusted anchor."""

with pathlib.Path(sys.argv[1]).open() as fh:
    scan = json.load(fh)
pe = pefile.PE(sys.argv[2], fast_load=True)
t = next(s for s in pe.sections if s.Name.rstrip(b"\0") == b".text")
d = t.get_data()
b = t.VirtualAddress
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)

manual = {  # old function -> new function (established by hand, see docs/research/port-1.19.4.0.md)
    0x22F4580: 0x23717E0,
    0x22F4D80: 0x2372230,
    0x225E320: 0x22BD0E0,
    0x225E3B0: 0x22BD170,
    0x22FBEC0: 0x2379B70,
    0xB8A1C0: 0xBDCDB0,
    0x1237180: 0x12C4AD0,
    0x22F4770: 0x2371A40,
    0x1237520: 0x12C4EA0,
    0x12374F0: 0x12C4E70,
    0x1144840: 0x11CE6B0,
    0xBB4390: 0xC07620,
    0x2A2570: 0x2B9D00,
}
anchors = {
    e["old_rva"]: e["hits"][0]
    for e in scan
    if len(e["hits"]) == 1 and e["n"] >= MIN_ANCHOR_BYTES and e["old_rva"]
}
anchors.update(manual)
keys = sorted(anchors)


def decode(h: str) -> bytes:
    """Convert a hex string such as ``"4889e5"`` into bytes.

    Args:
        h: Hexadecimal text with an even number of digits.

    Returns:
        The decoded bytes.
    """
    return bytes(int(h[i : i + 2], 16) for i in range(0, len(h), 2))


for e in scan:
    if e["n"] >= MIN_ANCHOR_BYTES and len(e["hits"]) == 1:
        continue
    old = e["old_rva"]
    h = e["hex"]
    if "?" in h or old is None:
        continue
    i = bisect.bisect_right(keys, old) - 1
    cands = []
    for j in (i, i + 1):
        if 0 <= j < len(keys):
            a = keys[j]
            pred = old + anchors[a] - a
            lo = max(pred - 0x180 - b, 0)
            hi = pred + 0x180 - b
            for m in re.finditer(re.escape(decode(h)), d[lo:hi]):
                r = b + lo + m.start()
                cands.append((abs(r - pred), r, a, old - a))
    cands.sort()
    ins = next(md.disasm(decode(h) + b"\0" * 8, 0))
    txt = f"{ins.mnemonic} {ins.op_str}"
    if cands:
        dist, r, a, rel = cands[0]
        uniq = len({c[1] for c in cands})
        print(
            f"{e['file'][:22]:22} {e['id']:36} {old:#9x} -> {r:#9x} dist={dist:#6x} "
            f"anchor={a:#x}+{rel:#x} cands={uniq}  {txt}"
        )
    else:
        print(f"{e['file'][:22]:22} {e['id']:36} {old:#9x} -> MISSING  {txt}")
