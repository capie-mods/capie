"""Fuzzy re-location of CAPIE pins that the exact scan could not place.

Usage: python -I fuzzy.py <scan.json> <new hoi4.exe> <out.json>
"""

import bisect
import json
import pathlib
import sys
from typing import Any

import numpy as np  # ty: ignore[unresolved-import]
import pefile  # ty: ignore[unresolved-import]

MIN_ANCHOR_BYTES = 16
"""Shortest pattern, in bytes, that can serve as a trusted anchor."""

scan, exe, out = sys.argv[1:4]
with pathlib.Path(scan).open() as fh:
    r = json.load(fh)
pe = pefile.PE(exe, fast_load=True)
t = next(s for s in pe.sections if s.Name.rstrip(b"\0") == b".text")
data = np.frombuffer(t.get_data(), dtype=np.uint8)
base = t.VirtualAddress

# Trusted anchors: long patterns (>= 16 bytes) with one hit.
anchors = sorted(
    (e["old_rva"], e["hits"][0] - e["old_rva"])
    for e in r
    if len(e["hits"]) == 1 and e["n"] >= MIN_ANCHOR_BYTES and e["old_rva"]
)
keys = [a[0] for a in anchors]


def predicted(old: int) -> list[int]:
    """Predict displacements for an unplaced pin from the neighbouring trusted anchors.

    Args:
        old: Old RVA of the pin.

    Returns:
        Sorted, de-duplicated displacements of the anchors on either side of ``old``.
    """
    i = bisect.bisect_right(keys, old) - 1
    return sorted({anchors[j][1] for j in (i, i + 1) if 0 <= j < len(anchors)})


res: list[dict[str, Any]] = []
for e in r:
    old = e["old_rva"]
    if old is None:
        continue
    trusted = len(e["hits"]) == 1 and e["n"] >= MIN_ANCHOR_BYTES
    if trusted:
        continue
    h = e["hex"]
    n = len(h) // 2
    pat = np.array(
        [int(h[2 * i : 2 * i + 2], 16) if h[2 * i : 2 * i + 2] != "??" else 0 for i in range(n)],
        np.uint8,
    )
    msk = np.array([h[2 * i : 2 * i + 2] != "??" for i in range(n)])
    ds = predicted(old)
    lo = old + min(ds) - 0x3000 - base
    hi = old + max(ds) + 0x3000 - base
    lo = max(lo, 0)
    hi = min(hi, len(data) - n)
    win = np.lib.stride_tricks.sliding_window_view(data[lo : hi + n], n)
    score = ((win == pat) | ~msk).sum(1)
    best = np.argsort(-score)[:3]
    cands: list[dict[str, Any]] = []
    for b in best:
        rva = base + lo + int(b)
        got = bytes(data[lo + b : lo + b + n]).hex()
        diff = [i for i in range(n) if msk[i] and got[2 * i : 2 * i + 2] != h[2 * i : 2 * i + 2]]
        cands.append(
            {
                "rva": rva,
                "score": int(score[b]),
                "of": n,
                "delta": rva - old,
                "pred_err": min(abs(rva - old - d) for d in ds),
                "diff_at": diff,
                "got": got,
            }
        )
    res.append({"id": e["id"], "file": e["file"], "old": old, "hex": h, "pred": ds, "cands": cands})

with pathlib.Path(out).open("w") as fh:
    json.dump(res, fh, indent=1)
for x in res:
    c = x["cands"][0]
    c2 = x["cands"][1]
    flag = (
        "OK "
        if c["score"] == c["of"] and c2["score"] < c["of"]
        else ("AMB" if c["score"] == c2["score"] else "DIF")
    )
    print(
        f"{flag} {x['id']:34} {hex(x['old']):>9}->{hex(c['rva']):>9} "
        f"{c['score']}/{c['of']} perr={hex(c['pred_err'])} next={c2['score']} "
        f"diff@{c['diff_at'][:12]}"
    )
