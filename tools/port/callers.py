"""List rel32 call/jmp sites targeting given RVAs. Usage: python -I callers.py <exe> rva[,rva...] [max]"""
import sys, numpy as np, pefile
pe = pefile.PE(sys.argv[1], fast_load=True)
t = next(s for s in pe.sections if s.Name.rstrip(b"\0") == b".text")
d = np.frombuffer(t.get_data(), np.uint8); base = t.VirtualAddress
rel = d[1:len(d)-3].astype(np.int64)  # placeholder
disp = np.frombuffer(t.get_data()[1:len(d) - (len(d)-1) % 4 - 3] if False else b"", np.int32)
raw = t.get_data()
idx = np.arange(len(raw) - 5)
op = d[:len(raw) - 5]
d32 = (d[1:len(raw)-4].astype(np.int64) | (d[2:len(raw)-3].astype(np.int64) << 8) | (d[3:len(raw)-2].astype(np.int64) << 16) | (d[4:len(raw)-1].astype(np.int64) << 24))
d32 = np.where(d32 >= 2**31, d32 - 2**32, d32)
tgt = base + idx + 5 + d32
mx = int(sys.argv[3]) if len(sys.argv) > 3 else 15
for a in sys.argv[2].split(","):
    a = int(a, 16)
    hits = np.nonzero(((op == 0xE8) | (op == 0xE9)) & (tgt == a))[0]
    print(hex(a), len(hits), [hex(base + int(h)) for h in hits[:mx]])
