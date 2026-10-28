"""Re-locate every CAPIE code pin from HOI4 1.19.3.0 in a new hoi4.exe.

Usage: python -I port_scan.py <capie repo> <new hoi4.exe> <out.json>
"""

import bisect
import json
import pathlib
import re
import sys
from typing import Any

import capstone  # ty: ignore[unresolved-import] - not in the ty --with list of the check command
import pefile  # ty: ignore[unresolved-import] - not in the ty --with list of the check command
from capstone import x86  # ty: ignore[unresolved-import] - not in the ty --with list of the check command

MIN_HEX_CHARS = 4
"""Shortest hex string, in characters, that counts as a code pattern."""

REL32_SIZE = 4
"""Size in bytes of a 32-bit RIP-relative or rel32 displacement field."""

MIN_ANCHOR_BYTES = 12
"""Shortest pattern, in bytes, that may serve as an anchor for the delta search."""

repo, exe, out = map(pathlib.Path, sys.argv[1:4])
src = repo / "plugin/capie/src"
layout = (src / "core/game_layout.hpp").read_text(encoding="utf-8")
rvas = {
    m[1]: int(m[2], 16)
    for m in re.finditer(
        r"constexpr std::uintptr_t (\w+) = (0x[0-9A-Fa-f]+);",
        layout.split("namespace off")[0],
    )
}

files = [
    src / "core/signature_table.hpp",
    *sorted(src.glob("features/**/*pins*.hpp")),
    src / "features/console/console_wiring.hpp",
]
entries: list[dict[str, Any]] = []
for f in files:
    # Join split string literals.
    text = re.sub(r'"\s*\n\s*"', "", f.read_text(encoding="utf-8"))
    for m in re.finditer(r"\{([^{}]*?)\}", text):
        body = m[1]
        hexes = [
            h
            for h in re.findall(r'"([0-9a-fA-F?]+)"', body)
            if len(h) >= MIN_HEX_CHARS and len(h) % 2 == 0
        ]
        if not hexes:
            continue
        ident_match = re.search(r'"([^"]+)"', body)
        if ident_match is None:
            msg = "pin entry without a quoted identifier"
            raise TypeError(msg)
        ident = ident_match[1]
        rest = body.split(",")
        rva = None
        for part in rest:
            stripped = part.strip()
            if re.fullmatch(r"0x[0-9A-Fa-f]+", stripped):
                rva = int(stripped, 16)
                break
            mm = re.fullmatch(r"(?:core::)?rva::(\w+)", stripped)
            if mm:
                rva = rvas[mm[1]]
                break
        entries.append({"file": f.name, "id": ident, "old_rva": rva, "hex": hexes[0]})

pe = pefile.PE(str(exe), fast_load=True)
text_sec = next(s for s in pe.sections if s.Name.rstrip(b"\0") == b".text")
data = bytes(text_sec.get_data())
base = int(text_sec.VirtualAddress)
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
md.detail = True


def masked(hexs: str) -> tuple[bytes, list[bool]]:
    """Split a hex pattern with ``??`` wildcards into its bytes and a match mask.

    Args:
        hexs: Hex pattern, two characters per byte; ``??`` marks a wildcard byte.

    Returns:
        The pattern bytes (wildcards as 0) and, per byte, True where the byte must match.
    """
    raw = bytes(
        int(hexs[i : i + 2], 16) if hexs[i : i + 2] != "??" else 0 for i in range(0, len(hexs), 2)
    )
    mask = [hexs[i : i + 2] != "??" for i in range(0, len(hexs), 2)]
    return raw, mask


def mask_rel(raw: bytes, mask: list[bool]) -> list[bool]:
    """Wildcard the RIP-relative and rel32 fields of the instructions in a pattern.

    Args:
        raw: Pattern bytes.
        mask: Per-byte match flags; the list passed in is not modified.

    Returns:
        A new per-byte mask with the displacement and rel32 bytes set to wildcards.
    """
    mask = list(mask)
    for ins in md.disasm(raw, 0):
        rel = ins.group(capstone.CS_GRP_JUMP) or ins.group(capstone.CS_GRP_CALL)
        riprel = any(
            op.type == x86.X86_OP_MEM and op.mem.base == x86.X86_REG_RIP for op in ins.operands
        )
        if riprel and ins.disp_size:
            for k in range(ins.disp_offset, ins.disp_offset + ins.disp_size):
                if ins.address + k < len(mask):
                    mask[ins.address + k] = False
        elif rel and ins.imm_size == REL32_SIZE:
            for k in range(ins.imm_offset, ins.imm_offset + REL32_SIZE):
                if ins.address + k < len(mask):
                    mask[ins.address + k] = False
    return mask


def find(raw: bytes, mask: list[bool], limit: int = 3) -> list[int]:
    """Locate a masked pattern in the .text section.

    Args:
        raw: Pattern bytes.
        mask: Per-byte match flags; False bytes match any value.
        limit: Maximum number of hits to return.

    Returns:
        Virtual addresses of up to ``limit`` matches, in address order.
    """
    pat = b"".join(re.escape(bytes([b])) if m else b"." for b, m in zip(raw, mask, strict=True))
    return [base + m.start() for m in re.finditer(pat, data, re.DOTALL)][:limit]


results: list[dict[str, Any]] = []
for e in entries:
    raw, mask = masked(e["hex"])
    hits = find(raw, mask)
    how = "exact"
    if len(hits) != 1:
        m2 = mask_rel(raw, mask)
        h2 = find(raw, m2)
        if h2:
            hits, how = h2, "masked"
    e.update(hits=hits, how=how, n=len(raw))
    results.append(e)

anchors = sorted(
    (e["old_rva"], e["hits"][0])
    for e in results
    if len(e["hits"]) == 1 and e["old_rva"] and e["n"] >= MIN_ANCHOR_BYTES
)
keys = [a[0] for a in anchors]
for e in results:
    if e["old_rva"] is None:
        continue
    i = bisect.bisect_right(keys, e["old_rva"]) - 1
    near = [anchors[j] for j in (i, i + 1) if 0 <= j < len(anchors)]
    deltas = sorted({n - o for o, n in near})
    e["candidate_deltas"] = deltas
    if len(e["hits"]) != 1:
        raw, mask = masked(e["hex"])
        for d in deltas:
            off = e["old_rva"] + d - base
            if 0 <= off < len(data) and all(
                not m or data[off + k] == b for k, (b, m) in enumerate(zip(raw, mask, strict=True))
            ):
                e["via_delta"] = e["old_rva"] + d
                break

out.write_text(json.dumps(results, indent=1), encoding="utf-8")
ok = sum(len(e["hits"]) == 1 for e in results)
vd = sum("via_delta" in e for e in results)
print(f"entries {len(results)}: unique {ok}, via-delta {vd}, missing {len(results) - ok - vd}")
for e in results:
    if len(e["hits"]) != 1 and "via_delta" not in e:
        old = e["old_rva"] and hex(e["old_rva"])
        hits = [hex(h) for h in e["hits"]]
        print(f"  MISS {e['file']:28} {e['id']:40} old={old} hits={hits} n={e['n']}")
