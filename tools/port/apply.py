"""Write the 1.19.4.0 values into the CAPIE sources.

Usage: python -I apply.py <capie repo> <new_tables.json>
Rewrites: signature/instruction pin entries (RVA literal + pattern), RIP pins (displacement, next,
literal target), and the named constants of game_layout.hpp, host_probe.hpp and console_layout.hpp.
An entry whose RVA is a named constant (`rva::x`, `core::rva::x`, `layout::x`) gets that constant
rewritten instead; a constant that is an alias of another one (`x = core::rva::y;`) is left alone.
"""

import json
import os
import pathlib
import re
import sys
from functools import partial

HEX_LINE_DIGITS = 96
"""Hex digits per line when a long signature pattern is split into several string literals."""

MIN_PATTERN_HEX_DIGITS = 4
"""Fewest hex digits a string literal needs to count as the signature pattern of an entry."""

DEFAULT_PATTERN_INDENT = 5
"""Indent of a continuation line when the pattern does not start on its own line."""

RIP_PIN_MIN_NUMBERS = 2
"""Hex numbers a RIP pin needs before its displacement and next address are rewritten."""

RIP_PIN_TARGET_NUMBERS = 3
"""Hex numbers a RIP pin needs in its body before the literal target is rewritten."""

repo = pathlib.Path(sys.argv[1])
tables = json.loads(pathlib.Path(sys.argv[2]).read_text())
mp = json.loads(
    (
        pathlib.Path(__file__).resolve().parent
        / os.environ.get("CAPIE_PORT_MAP", "map_1.19.4.0.json")
    ).read_text()
)
src = repo / "plugin/capie/src"
files = [
    src / "core/signature_table.hpp",
    *sorted(src.glob("features/**/*pins*.hpp")),
    src / "features/console/console_wiring.hpp",
]


def fmt_hex(h: str, *, upper: bool, indent: int) -> str:
    """Format a hex pattern as one or more quoted C string literals.

    Args:
        h: Hex digits, with ``??`` for wildcard bytes.
        upper: Whether to write the digits in upper case.
        indent: Spaces to indent the continuation lines by.

    Returns:
        The quoted literal, split into pieces of ``HEX_LINE_DIGITS`` digits when it is long.
    """
    h = h.upper().replace("??", "??") if upper else h.lower()
    if len(h) <= HEX_LINE_DIGITS:
        return f'"{h}"'
    parts = [h[k : k + HEX_LINE_DIGITS] for k in range(0, len(h), HEX_LINE_DIGITS)]
    return ("\n" + " " * indent).join(f'"{p}"' for p in parts)


def as_hex(v: str) -> str:
    """Format a hex number from the tables as a C literal.

    Args:
        v: Hex number as text, with or without a ``0x`` prefix.

    Returns:
        The number as ``0x`` followed by upper-case hex digits.
    """
    return "0x" + format(int(v, 16), "X")


# Constants named by table entries, with their new value.
named: dict[str, dict[str, str]] = {"rva": {}, "host": {}, "console": {}}


def rewrite_entry(m: re.Match[str], f: pathlib.Path) -> str:
    """Rewrite one ``{"id", ...}`` entry of a pins header with the new values.

    Args:
        m: Match of the whole entry.
        f: The header the entry is in; ``host`` in its name selects the host constants owner.

    Returns:
        The entry text with the new pattern, RVA and RIP values.
    """
    body = m.group(0)
    ident = body[2 : body.index('"', 2)]  # the entry starts with {"<ident>"
    if ident in tables:
        t = tables[ident]
        lits = list(re.finditer(r'"(?:[0-9A-Fa-f?]{2})+"(?:\s*\n\s*"(?:[0-9A-Fa-f?]{2})+")*', body))
        pat = next(
            lit
            for lit in lits
            if len(re.sub(r"[^0-9A-Fa-f?]", "", lit.group(0))) >= MIN_PATTERN_HEX_DIGITS
        )
        old_hex = re.sub(r"[^0-9A-Fa-f?]", "", pat.group(0))
        upper = any(c in "ABCDEF" for c in old_hex)
        line_start = body.rfind("\n", 0, pat.start()) + 1
        if "\n" in body[: pat.start()]:
            indent = len(body[line_start:]) - len(body[line_start:].lstrip())
        else:
            indent = DEFAULT_PATTERN_INDENT
        body = (
            body[: pat.start()] + fmt_hex(t["hex"], upper=upper, indent=indent) + body[pat.end() :]
        )
        rm = re.search(r",\s*(0x[0-9A-Fa-f]{4,8})\s*,", body)
        if rm:
            body = body[: rm.start(1)] + as_hex(t["rva"]) + body[rm.end(1) :]
        nm = None if rm else re.search(r",\s*(?:core::)?(rva|layout)::(\w+)\s*,", body)
        if nm:
            owner = "rva" if nm[1] == "rva" else ("host" if "host" in f.name else "console")
            named[owner][nm[2]] = t["rva"]
    elif ident in mp["rip_pins"]:
        d, n, tgt = mp["rip_pins"][ident]
        nums = list(re.finditer(r"0x[0-9A-Fa-f]+", body))
        if len(nums) < RIP_PIN_MIN_NUMBERS:
            return body  # displacement/next come from named layout constants
        body = body[: nums[0].start()] + as_hex(d) + body[nums[0].end() :]
        nums = list(re.finditer(r"0x[0-9A-Fa-f]+", body))
        body = body[: nums[1].start()] + as_hex(n) + body[nums[1].end() :]
        if tgt.startswith("0x"):
            nums = list(re.finditer(r"0x[0-9A-Fa-f]+", body))
            if len(nums) >= RIP_PIN_TARGET_NUMBERS:
                body = body[: nums[2].start()] + as_hex(tgt) + body[nums[2].end() :]
    return body


for f in files:
    text = f.read_text()
    orig = text
    text = re.sub(r'\{"[\w.]+",[^{}]*\}', partial(rewrite_entry, f=f), text)
    if text != orig:
        f.write_text(text)
        print("rewrote", f.relative_to(repo))


def set_consts(path: pathlib.Path, values: dict[str, str], ns: str | None = None) -> None:
    """Rewrite the hex value of each named constant of a header, in place.

    Args:
        path: The header to edit.
        values: Constant name -> new hex value; ``TODO`` stands for ``0x0``.
        ns: Namespace to restrict the rewrite to; the whole file when None.
    """
    text = path.read_text()
    seg_lo, seg_hi = 0, len(text)
    if ns is not None:
        seg_lo = text.index(f"namespace {ns} {{")
        seg_hi = text.index("}  // namespace " + ns, seg_lo)
    seg = text[seg_lo:seg_hi]
    for name, val in values.items():
        value = "0x0" if val == "TODO" else val
        if re.search(rf"\b{name} = [A-Za-z_][\w:]*;", seg):
            continue  # alias of another constant
        seg, k = re.subn(rf"(\b{name} = )0x[0-9A-Fa-f]+;", rf"\g<1>{as_hex(value)};", seg)
        if k != 1:
            print("  !", path.name, name, "matched", k)
    path.write_text(text[:seg_lo] + seg + text[seg_hi:])
    print("constants", path.relative_to(repo))


set_consts(src / "core/game_layout.hpp", {**named["rva"], **mp["rva"]}, "rva")
set_consts(
    src / "core/game_layout.hpp",
    {k: v[1] for k, v in mp["off"].items() if not k.startswith("session")},
    "off",
)
host = {
    **named["host"],
    **{k: v for k, v in mp["host"].items() if k != "ingame_idler_session_slot_target"},
}
host.update({k: v[1] for k, v in mp["off"].items() if k.startswith("session")})
set_consts(src / "features/host/host_probe.hpp", host)
set_consts(src / "features/console/console_layout.hpp", {**named["console"], **mp["console"]})
