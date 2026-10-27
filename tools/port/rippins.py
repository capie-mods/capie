"""Move every RIP/rel32 target pin by its anchor's delta and read the new target.

Usage: python -I rippins.py <capie repo> <scan.json> <new hoi4.exe>
Prints: id, old disp, new disp, new next, new target, and the new value expected from map_1.19.4.0.json.
"""
import bisect, json, os, pathlib, re, sys
import pefile

repo, scan, exe = pathlib.Path(sys.argv[1]), json.load(open(sys.argv[2])), sys.argv[3]
here = pathlib.Path(__file__).resolve().parent
mp = json.loads((pathlib.Path(__file__).resolve().parent / os.environ.get("CAPIE_PORT_MAP", "map_1.19.4.0.json")).read_text())
pe = pefile.PE(exe, fast_load=True); img = pe.get_memory_mapped_image()

anchors = {e["old_rva"]: e["hits"][0] for e in scan if len(e["hits"]) == 1 and e["n"] >= 16 and e["old_rva"]}
for k, v in mp["signatures"].items():
    for e in scan:
        if e["id"] == k and e["old_rva"]: anchors[e["old_rva"]] = int(v, 16)
keys = sorted(anchors)

src = repo / "plugin/capie/src"
layout_text = (src / "core/game_layout.hpp").read_text() + (src / "features/console/console_layout.hpp").read_text() \
    + (src / "features/host/host_probe.hpp").read_text()
names = {m[1]: int(m[2], 16) for m in re.finditer(r"(\w+) = (0x[0-9A-Fa-f]+);", layout_text)}
files = [src / "core/signature_table.hpp", *sorted(src.glob("features/**/*pins*.hpp")), src / "features/console/console_wiring.hpp"]
for f in files:
    for m in re.finditer(r'\{"([\w.]+)",\s*(0x[0-9A-Fa-f]+),\s*(0x[0-9A-Fa-f]+),\s*([\w:]+|0x[0-9A-Fa-f]+)\}', f.read_text()):
        ident, disp, nxt, target = m[1], int(m[2], 16), int(m[3], 16), m[4]
        old_target = int(target, 16) if target.startswith("0x") else names.get(target.split("::")[-1])
        i = bisect.bisect_right(keys, disp) - 1
        a = keys[i]; nd = disp + anchors[a] - a
        if ident in mp["rip_pins"]: nd = int(mp["rip_pins"][ident][0], 16)
        tgt = nd + 4 + int.from_bytes(img[nd:nd + 4], "little", signed=True)
        print(f"{f.name[:20]:20} {ident:34} old {disp:#9x}->{old_target and hex(old_target)}  new disp {nd:#9x} next {nd + 4:#9x} -> {tgt:#9x}  (anchor {a:#x}, +{disp - a:#x})")
