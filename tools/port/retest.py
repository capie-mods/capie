"""Rewrite 1.19.3.0 address literals in the CAPIE tests to their 1.19.4.0 values.

Usage: python -I retest.py <capie repo> [--dry]
The old->new dictionary is built from map_1.19.4.0.json, out/scan.json and the named constants
(game_layout, host_probe, console_layout) at git HEAD versus the working tree.
Only literals of 5 to 7 hex digits that are known old addresses are rewritten.
"""
import json, os, pathlib, re, subprocess, sys

repo = pathlib.Path(sys.argv[1]).resolve(); dry = "--dry" in sys.argv
here = pathlib.Path(__file__).resolve().parent
mp = json.loads((pathlib.Path(__file__).resolve().parent / os.environ.get("CAPIE_PORT_MAP", "map_1.19.4.0.json")).read_text()); scan = json.loads((here / "out/scan.json").read_text())
o2n = {}

def consts(text):
    return {m[1]: int(m[2], 16) for m in re.finditer(r"(\w+) = (0x[0-9A-Fa-f]{5,8});", text)}

for rel in ["plugin/capie/src/core/game_layout.hpp", "plugin/capie/src/features/host/host_probe.hpp",
            "plugin/capie/src/features/console/console_layout.hpp"]:
    old = consts(subprocess.run(["git", "-C", str(repo), "show", f"HEAD:{rel}"], capture_output=True, text=True, check=True).stdout)
    new = consts((repo / rel).read_text())
    for k, v in old.items():
        if k in new and new[k] != v and new[k] != 0: o2n[v] = new[k]
for e in scan:
    for kind in ("signatures", "instruction_pins"):
        if e["id"] in mp[kind] and e["old_rva"]: o2n[e["old_rva"]] = int(mp[kind][e["id"]], 16)
pins_old = ""
for rel in subprocess.run(["git", "-C", str(repo), "ls-files", "plugin/capie/src"], capture_output=True, text=True).stdout.split():
    if rel.endswith("pins.hpp") or rel.endswith("console_wiring.hpp") or rel.endswith("signature_table.hpp"):
        pins_old += subprocess.run(["git", "-C", str(repo), "show", f"HEAD:{rel}"], capture_output=True, text=True).stdout
for m in re.finditer(r'\{"([\w.]+)",\s*(0x[0-9A-Fa-f]+),\s*(0x[0-9A-Fa-f]+),\s*([\w:]+|0x[0-9A-Fa-f]+)\}', pins_old):
    if m[1] in mp["rip_pins"]:
        d, n, t = mp["rip_pins"][m[1]]
        o2n[int(m[2], 16)] = int(d, 16); o2n[int(m[3], 16)] = int(n, 16)
        if m[4].startswith("0x") and t.startswith("0x"): o2n[int(m[4], 16)] = int(t, 16)
o2n.update({0x139F140: 0x143ADA0, 0x2B91060: 0x2C03C50, 0xB3C4A0: 0xB8E390, 0x22F3FB0: 0x2371210, 0x2AECAE8: 0x2B30200})

total = 0
for f in sorted((repo / "plugin/capie/tests").rglob("*")):
    if f.suffix not in (".cpp", ".hpp", ".py", ".json") or f.name == "check_signature_table.py": continue
    s = f.read_text(encoding="utf-8")
    def sub(m):
        v = int(m[2], 16)
        if v in o2n:
            up = any(c in "ABCDEF" for c in m[2]) or not any(c in "abcdef" for c in m[2])
            return m[1] + (format(o2n[v], "X") if up else format(o2n[v], "x"))
        return m[0]
    t, k = re.subn(r"(0x)([0-9A-Fa-f]{5,7})\b", lambda m: sub(m), s)
    changed = sum(1 for a, b in zip(re.findall(r"0x[0-9A-Fa-f]{5,7}\b", s), re.findall(r"0x[0-9A-Fa-f]{5,7}\b", t)) if a != b)
    if changed:
        total += changed; print(f"{f.relative_to(repo)}: {changed}")
        if not dry: f.write_text(t, encoding="utf-8")
print("literals rewritten:", total, "(dry)" if dry else "")
