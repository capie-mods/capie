"""Find 'insn with rel32/rip disp' immediately followed by given bytes; report the rel target.
Usage: python -I pair.py <exe> <prefix_hex_without_disp> <follow_hex> [label]"""
import sys, re, collections, pefile
pe = pefile.PE(sys.argv[1], fast_load=True)
t = next(s for s in pe.sections if s.Name.rstrip(b"\0") == b".text"); d = t.get_data(); b = t.VirtualAddress
pre, fol = bytes.fromhex(sys.argv[2]), bytes.fromhex(sys.argv[3])
c = collections.defaultdict(list)
for m in re.finditer(re.escape(pre) + b"(....)" + re.escape(fol), d, re.S):
    disp_at = b + m.start() + len(pre); nxt = disp_at + 4
    c[nxt + int.from_bytes(m.group(1), "little", signed=True)].append(hex(disp_at))
for tgt, sites in sorted(c.items(), key=lambda x: -len(x[1]))[:6]:
    print(sys.argv[4] if len(sys.argv) > 4 else "", "target", hex(tgt), "sites", len(sites), sites[:6])
