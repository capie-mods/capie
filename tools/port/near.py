"""For each call site of TARGET, report which FIND function was called within WINDOW bytes before.
Usage: python -I near.py <exe> target finds(comma) window"""
import sys, numpy as np, pefile
pe = pefile.PE(sys.argv[1], fast_load=True)
t = next(s for s in pe.sections if s.Name.rstrip(b"\0") == b".text")
raw = t.get_data(); d = np.frombuffer(raw, np.uint8); base = t.VirtualAddress
n = len(raw) - 5; op = d[:n]
d32 = (d[1:n+1].astype(np.int64) | (d[2:n+2].astype(np.int64) << 8) | (d[3:n+3].astype(np.int64) << 16) | (d[4:n+4].astype(np.int64) << 24))
d32 = np.where(d32 >= 2**31, d32 - 2**32, d32)
tgt = base + np.arange(n) + 5 + d32
call = op == 0xE8
T = int(sys.argv[2], 16); finds = [int(x, 16) for x in sys.argv[3].split(",")]; W = int(sys.argv[4], 0)
sites = np.nonzero(call & (tgt == T))[0]
from collections import Counter
c = Counter()
for s in sites:
    lo = max(0, s - W); seg = np.nonzero(call[lo:s] & np.isin(tgt[lo:s], finds))[0]
    if len(seg): c[hex(int(tgt[lo + seg[-1]]))] += 1
    else: c["none"] += 1
print(len(sites), c.most_common())
