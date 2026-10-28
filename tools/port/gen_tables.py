"""Generate new byte patterns for every signature and instruction pin from the new hoi4.exe.

Usage: python -I gen_tables.py <scan.json> <new hoi4.exe> <out.json>

Rules (RE findings 1.1 / 6.1 of the original table):
- whole instructions only; start at the new RVA, cover the same number of instructions
  as the old pattern;
- an instruction whose old bytes contained `??` keeps its RIP/rel32 field masked;
- signatures must match exactly once in .text: extend by whole instructions (up to 128
  bytes) until unique;
- no DIR64 relocation may fall inside the pattern.
Instruction pins are exact bytes of the same number of instructions, no uniqueness rule.
"""

import json
import os
import pathlib
import re
import sys

import capstone  # ty: ignore[unresolved-import] - not in the ty --with list of the check command
import pefile  # ty: ignore[unresolved-import] - not in the ty --with list of the check command
from capstone import x86  # ty: ignore[unresolved-import] - not in the ty --with list of the check command

IMAGE_REL_BASED_DIR64 = 10
"""Base-relocation type of a 64-bit absolute pointer (IMAGE_REL_BASED_DIR64)."""

REL32_SIZE = 4
"""Size in bytes of a 32-bit RIP-relative or rel32 displacement field."""

DISASM_WINDOW = 0x200
"""Bytes of code read from the new image at each candidate start address."""

MAX_PATTERN_BYTES = 120
"""Longest pattern, in bytes, that the uniqueness extension may build."""

with pathlib.Path(sys.argv[1]).open(encoding="utf-8") as scan_file:
    scan = json.load(scan_file)
exe = sys.argv[2]
out = pathlib.Path(sys.argv[3])
map_path = pathlib.Path(__file__).resolve().parent / os.environ.get(
    "CAPIE_PORT_MAP", "map_1.19.4.0.json"
)
mp = json.loads(map_path.read_text(encoding="utf-8"))
pe = pefile.PE(exe)
pe.parse_data_directories()
img = pe.get_memory_mapped_image()
t = next(s for s in pe.sections if s.Name.rstrip(b"\0") == b".text")
text = t.get_data()
tb = t.VirtualAddress
relocs = set()
for blk in getattr(pe, "DIRECTORY_ENTRY_BASERELOC", []):
    for e in blk.entries:
        if e.type == IMAGE_REL_BASED_DIR64:
            relocs.update(range(e.rva, e.rva + 8))
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
md.detail = True


def instrs(
    data: bytes, addr: int, n: int | None = None, limit: int | None = None
) -> list[capstone.CsInsn]:
    """Disassemble instructions until a count or a byte limit is reached.

    Args:
        data: Machine code to disassemble.
        addr: Address the code is loaded at.
        n: Stop after this many instructions, or None for no count limit.
        limit: Stop once the instructions cover this many bytes, or None for no byte limit.

    Returns:
        The disassembled instructions in address order.
    """
    res = []
    for i in md.disasm(data, addr):
        res.append(i)
        if n is not None and len(res) >= n:
            break
        if limit is not None and i.address + i.size - addr >= limit:
            break
    return res


def masked_field(i: capstone.CsInsn) -> range:
    """Return the byte offsets of an instruction's RIP-relative or rel32 field.

    Args:
        i: Disassembled instruction.

    Returns:
        Offsets within the instruction that must be masked; empty when it has no such field.
    """
    has_rip = any(op.type == x86.X86_OP_MEM and op.mem.base == x86.X86_REG_RIP for op in i.operands)
    if has_rip and i.disp_size:
        return range(i.disp_offset, i.disp_offset + i.disp_size)
    if (
        i.group(capstone.CS_GRP_JUMP) or i.group(capstone.CS_GRP_CALL)
    ) and i.imm_size == REL32_SIZE:
        return range(i.imm_offset, i.imm_offset + REL32_SIZE)
    return range(0)


def old_shape(h: str) -> list[bool]:
    """Tell, per instruction, whether the old pattern contained a wildcard.

    Args:
        h: Old hex pattern, two characters per byte; ``??`` marks a wildcard byte.

    Returns:
        One flag per instruction of the pattern, True when that instruction had a wildcard.
    """
    raw = bytes(int(h[k : k + 2], 16) if h[k : k + 2] != "??" else 0 for k in range(0, len(h), 2))
    wild = [h[k : k + 2] == "??" for k in range(0, len(h), 2)]
    return [any(wild[i.address : i.address + i.size]) for i in md.disasm(raw, 0)]


def render(ins: list[capstone.CsInsn], mask_flags: list[bool]) -> str:
    """Render instructions as a hex pattern, wildcarding the masked fields.

    Args:
        ins: Instructions to render.
        mask_flags: Per-instruction flags; True masks that instruction's RIP/rel32 field.

    Returns:
        The hex pattern, two characters per byte, with ``??`` for masked bytes.
    """
    hx = ""
    for i, m in zip(ins, mask_flags, strict=False):
        f = masked_field(i) if m else range(0)
        hx += "".join("??" if k in f else f"{b:02x}" for k, b in enumerate(i.bytes))
    return hx


def count(hx: str) -> int:
    """Count the matches of a hex pattern in the .text section.

    Args:
        hx: Hex pattern, two characters per byte; ``??`` matches any byte.

    Returns:
        The number of non-overlapping matches in ``text``.
    """
    parts = [
        b"." if hx[k : k + 2] == "??" else re.escape(bytes([int(hx[k : k + 2], 16)]))
        for k in range(0, len(hx), 2)
    ]
    pat = b"".join(parts)
    return sum(1 for _ in re.finditer(pat, text, re.DOTALL))


res = {}
old = {e["id"]: e for e in scan}
for kind in ("signatures", "instruction_pins"):
    for ident, new_hex in mp[kind].items():
        new = int(new_hex, 16)
        e = old[ident]
        shape = old_shape(e["hex"])
        ins = instrs(img[new : new + DISASM_WINDOW], new, n=len(shape))
        flags = list(shape)
        hx = render(ins, flags)
        if kind == "signatures":
            while count(hx) != 1 and len(hx) // 2 < MAX_PATTERN_BYTES:
                more = instrs(img[new : new + DISASM_WINDOW], new, n=len(ins) + 1)
                if len(more) == len(ins):
                    break
                ins = more
                flags.append(any(flags) and bool(masked_field(ins[-1])))
                hx = render(ins, flags)
        n = len(hx) // 2
        rel = sorted(r for r in relocs if new <= r < new + n)
        uniq = count(hx) if kind == "signatures" else None
        res[ident] = {
            "kind": kind,
            "rva": hex(new),
            "hex": hx,
            "bytes": n,
            "unique": uniq,
            "dir64": [hex(r) for r in rel],
            "asm": "; ".join(f"{i.mnemonic} {i.op_str}" for i in ins),
        }
        flag = "" if (uniq in (None, 1) and not rel) else "  <-- CHECK"
        print(f"{kind[:4]} {ident:34} {new:#9x} {n:3}B uniq={uniq} reloc={len(rel)}{flag}")
out.write_text(json.dumps(res, indent=1), encoding="utf-8")
