"""Write the 1.19.4.0 values into the CAPIE sources.

Usage: python -I apply.py <capie repo> <new_tables.json>
Rewrites: signature/instruction pin entries (RVA literal + pattern), RIP pins (displacement, next,
literal target), and the named constants of game_layout.hpp, host_probe.hpp and console_layout.hpp.
"""
import json, os, pathlib, re, sys

repo = pathlib.Path(sys.argv[1]); tables = json.loads(pathlib.Path(sys.argv[2]).read_text())
mp = json.loads((pathlib.Path(__file__).resolve().parent / os.environ.get("CAPIE_PORT_MAP", "map_1.19.4.0.json")).read_text())
src = repo / "plugin/capie/src"
files = [src / "core/signature_table.hpp", *sorted(src.glob("features/**/*pins*.hpp")), src / "features/console/console_wiring.hpp"]

def fmt_hex(h, upper, indent):
    h = h.upper().replace("??", "??") if upper else h.lower()
    if len(h) <= 96: return f'"{h}"'
    parts = [h[k:k + 96] for k in range(0, len(h), 96)]
    return ("\n" + " " * indent).join(f'"{p}"' for p in parts)

def U(v): return "0x" + format(int(v, 16), "X")

for f in files:
    text = f.read_text(); orig = text
    def entry(m):
        body = m.group(0); ident = re.match(r'\{"([\w.]+)"', body)[1]
        if ident in tables:
            t = tables[ident]
            lits = list(re.finditer(r'"(?:[0-9A-Fa-f?]{2})+"(?:\s*\n\s*"(?:[0-9A-Fa-f?]{2})+")*', body))
            pat = next(l for l in lits if len(re.sub(r'[^0-9A-Fa-f?]', "", l.group(0))) >= 4)
            old_hex = re.sub(r'[^0-9A-Fa-f?]', "", pat.group(0))
            upper = any(c in "ABCDEF" for c in old_hex)
            line_start = body.rfind("\n", 0, pat.start()) + 1
            indent = len(body[line_start:]) - len(body[line_start:].lstrip()) if "\n" in body[:pat.start()] else 5
            body = body[:pat.start()] + fmt_hex(t["hex"], upper, indent) + body[pat.end():]
            rm = re.search(r',\s*(0x[0-9A-Fa-f]{4,8})\s*,', body)
            if rm: body = body[:rm.start(1)] + U(t["rva"]) + body[rm.end(1):]
        elif ident in mp["rip_pins"]:
            d, n, tgt = mp["rip_pins"][ident]
            nums = list(re.finditer(r'0x[0-9A-Fa-f]+', body))
            if len(nums) < 2: return body  # displacement/next come from named layout constants
            body = body[:nums[0].start()] + U(d) + body[nums[0].end():]
            nums = list(re.finditer(r'0x[0-9A-Fa-f]+', body))
            body = body[:nums[1].start()] + U(n) + body[nums[1].end():]
            if tgt.startswith("0x"):
                nums = list(re.finditer(r'0x[0-9A-Fa-f]+', body))
                if len(nums) >= 3: body = body[:nums[2].start()] + U(tgt) + body[nums[2].end():]
        return body
    text = re.sub(r'\{"[\w.]+",[^{}]*\}', entry, text)
    if text != orig: f.write_text(text); print("rewrote", f.relative_to(repo))

def set_consts(path, values, ns=None):
    text = path.read_text(); seg_lo, seg_hi = 0, len(text)
    if ns:
        seg_lo = text.index(f"namespace {ns} {{"); seg_hi = text.index("}  // namespace " + ns, seg_lo)
    seg = text[seg_lo:seg_hi]
    for name, val in values.items():
        if val == "TODO": val = "0x0"
        seg, k = re.subn(rf"(\b{name} = )0x[0-9A-Fa-f]+;", rf"\g<1>{U(val)};", seg)
        if k != 1: print("  !", path.name, name, "matched", k)
    path.write_text(text[:seg_lo] + seg + text[seg_hi:]); print("constants", path.relative_to(repo))

set_consts(src / "core/game_layout.hpp", mp["rva"], "rva")
set_consts(src / "core/game_layout.hpp", {k: v[1] for k, v in mp["off"].items() if not k.startswith("session")}, "off")
host = {k: v for k, v in mp["host"].items() if k != "ingame_idler_session_slot_target"}
host.update({k: v[1] for k, v in mp["off"].items() if k.startswith("session")})
set_consts(src / "features/host/host_probe.hpp", host)
set_consts(src / "features/console/console_layout.hpp", mp["console"])
