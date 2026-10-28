"""Generate new byte patterns for every signature and instruction pin from the new hoi4.exe.

Usage: python -I gen_tables.py <scan.json> <new hoi4.exe> <out.json>

Rules (RE findings 1.1 / 6.1 of the original table):
- whole instructions only; start at the new RVA, cover the same number of instructions as the old pattern;
- an instruction whose old bytes contained `??` keeps its RIP/rel32 field masked;
- signatures must match exactly once in .text: extend by whole instructions (up to 128 bytes) until unique;
- no DIR64 relocation may fall inside the pattern.
Instruction pins are exact bytes of the same number of instructions, no uniqueness rule.
"""
import json, os, pathlib, re, sys
import capstone, pefile
from capstone import x86

scan = json.load(open(sys.argv[1])); exe = sys.argv[2]; out = pathlib.Path(sys.argv[3])
mp = json.loads((pathlib.Path(__file__).resolve().parent / os.environ.get("CAPIE_PORT_MAP", "map_1.19.4.0.json")).read_text())
pe = pefile.PE(exe); pe.parse_data_directories()
img = pe.get_memory_mapped_image()
t = next(s for s in pe.sections if s.Name.rstrip(b"\0") == b".text"); text = t.get_data(); tb = t.VirtualAddress
relocs = set()
for blk in getattr(pe, "DIRECTORY_ENTRY_BASERELOC", []):
    for e in blk.entries:
        if e.type == 10: relocs.update(range(e.rva, e.rva + 8))
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64); md.detail = True

def instrs(data, addr, n=None, limit=None):
    res = []
    for i in md.disasm(data, addr):
        res.append(i)
        if n is not None and len(res) >= n: break
        if limit is not None and i.address + i.size - addr >= limit: break
    return res

def masked_field(i):
    if any(op.type == x86.X86_OP_MEM and op.mem.base == x86.X86_REG_RIP for op in i.operands) and i.disp_size:
        return range(i.disp_offset, i.disp_offset + i.disp_size)
    if (i.group(capstone.CS_GRP_JUMP) or i.group(capstone.CS_GRP_CALL)) and i.imm_size == 4:
        return range(i.imm_offset, i.imm_offset + 4)
    return range(0)

def old_shape(h):
    raw = bytes(int(h[k:k + 2], 16) if h[k:k + 2] != "??" else 0 for k in range(0, len(h), 2))
    wild = [h[k:k + 2] == "??" for k in range(0, len(h), 2)]
    shape = []
    for i in md.disasm(raw, 0):
        shape.append(any(wild[i.address:i.address + i.size]))
    return shape

def render(ins, mask_flags):
    hx = ""
    for i, m in zip(ins, mask_flags):
        f = masked_field(i) if m else range(0)
        hx += "".join("??" if k in f else f"{b:02x}" for k, b in enumerate(i.bytes))
    return hx

def count(hx):
    pat = b"".join(b"." if hx[k:k + 2] == "??" else re.escape(bytes([int(hx[k:k + 2], 16)])) for k in range(0, len(hx), 2))
    return sum(1 for _ in re.finditer(pat, text, re.S))

res = {}
old = {e["id"]: e for e in scan}
for kind in ("signatures", "instruction_pins"):
    for ident, new in mp[kind].items():
        new = int(new, 16); e = old[ident]; shape = old_shape(e["hex"])
        ins = instrs(img[new:new + 0x200], new, n=len(shape))
        flags = list(shape)
        hx = render(ins, flags)
        if kind == "signatures":
            while count(hx) != 1 and len(hx) // 2 < 120:
                more = instrs(img[new:new + 0x200], new, n=len(ins) + 1)
                if len(more) == len(ins): break
                ins = more; flags.append(any(flags) and bool(masked_field(ins[-1])))
                hx = render(ins, flags)
        n = len(hx) // 2
        rel = sorted(r for r in relocs if new <= r < new + n)
        uniq = count(hx) if kind == "signatures" else None
        res[ident] = {"kind": kind, "rva": hex(new), "hex": hx, "bytes": n, "unique": uniq, "dir64": [hex(r) for r in rel],
                      "asm": "; ".join(f"{i.mnemonic} {i.op_str}" for i in ins)}
        flag = "" if (uniq in (None, 1) and not rel) else "  <-- CHECK"
        print(f"{kind[:4]} {ident:34} {new:#9x} {n:3}B uniq={uniq} reloc={len(rel)}{flag}")
out.write_text(json.dumps(res, indent=1))
