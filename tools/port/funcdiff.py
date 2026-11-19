"""Function-level diff of two ``hoi4.exe`` files: move any old RVA to the new file.

Usage::

    python -I funcdiff.py index <exe> <out.pkl>
    python -I funcdiff.py map <old exe> <new exe> <old.pkl> <new.pkl> <rva> [<rva> ...]
    python -I funcdiff.py stats <old.pkl> <new.pkl>

``index`` splits ``.text`` into functions (a function starts after ``int3`` padding at a 16-byte
aligned address) and hashes every function after masking the RIP displacement and the rel32 of
every call and jump, so a function that only moved, or only calls moved functions, keeps its
hash. ``map`` aligns the two function lists (``difflib``, identical hashes are anchors) and moves
each given RVA: inside an identical function by its offset, inside a changed region by aligning the
instructions of the region. ``stats`` prints how many functions are identical.

Used in the 1.19.5.0 to 1.20.0.0 port (``docs/research/port-1.20.0.0.md``). Two traps, both
learned there: capstone reports ``disp_size`` 2 for the RIP operand of some SSE instructions, so the
mask always covers 4 bytes; and a run of identical blocks (token registrations) can be aligned one
block off, so a pin in such code is confirmed by the string or token it is about, not by this tool.
"""

import bisect
import difflib
import hashlib
import pickle
import re
import sys
from pathlib import Path
from typing import Any, cast

import capstone
import pefile
from capstone import x86

ALIGN = 16
"""Functions start at multiples of this many bytes after the ``int3`` padding."""

DISP_BYTES = 4
"""Size of a RIP displacement or rel32 field."""

MAX_INSN = 16
"""Longest x86 instruction in bytes."""

HASH_BYTES = 12
"""Digest size of a function hash."""

MIN_ARGS = 3
"""Smallest argument count of a command (program, command, first argument)."""

MAP_ARGS = 6
"""Fewest arguments of the ``map`` command (program, command, two exes, two indexes, one RVA)."""

STATS_ARGS = 4
"""Argument count of the ``stats`` command."""

Func = tuple[int, int, int, bytes]
"""One indexed function: start RVA, size, instruction count, hash."""

_md = cast("Any", capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64))
_md.detail = True


def _masked(ins: Any) -> bytes:  # noqa: ANN401 - capstone has no type hints
    """Return the bytes of one instruction with RIP and rel32 fields zeroed.

    Args:
        ins: Decoded instruction (with detail).

    Returns:
        The instruction bytes.
    """
    raw = bytes(ins.bytes)
    field = None
    if any(op.type == x86.X86_OP_MEM and op.mem.base == x86.X86_REG_RIP for op in ins.operands):
        field = ins.disp_offset
    elif (
        ins.group(capstone.CS_GRP_JUMP) or ins.group(capstone.CS_GRP_CALL)
    ) and ins.imm_size == DISP_BYTES:
        field = ins.imm_offset
    if field is None:
        return raw
    return raw[:field] + bytes(DISP_BYTES) + raw[field + DISP_BYTES :]


class Image:
    """A mapped image: ``.text`` bytes and the masked instruction stream of functions.

    Args:
        path: Executable to map.
    """

    def __init__(self, path: Path) -> None:
        """Map the file.

        Args:
            path: Executable to map.
        """
        pe = cast("Any", pefile.PE(str(path), fast_load=True))
        self.img = cast("bytes", pe.get_memory_mapped_image())
        text = next(s for s in pe.sections if s.Name.rstrip(b"\0") == b".text")
        self.lo = int(text.VirtualAddress)
        self.size = int(text.Misc_VirtualSize)

    def stream(self, start: int, size: int) -> list[tuple[int, bytes]]:
        """Decode a code range into ``(address, masked bytes)`` pairs.

        Args:
            start: First RVA.
            size: Number of bytes.

        Returns:
            One pair per instruction; an undecodable byte becomes one ``?`` entry.
        """
        out: list[tuple[int, bytes]] = []
        pos, end = start, start + size
        while pos < end:
            ins = next(_md.disasm(self.img[pos : pos + MAX_INSN], pos, 1), None)
            if ins is None:
                out.append((pos, b"?" + self.img[pos : pos + 1]))
                pos += 1
            else:
                out.append((pos, _masked(ins)))
                pos += ins.size
        return out

    def functions(self) -> list[Func]:
        """Split ``.text`` into functions and hash each one.

        Returns:
            The functions in address order.
        """
        code = self.img[self.lo : self.lo + self.size]
        starts = [0] if code[0] != 0xCC else []  # noqa: PLR2004 - int3 opcode
        starts += [
            m.end()
            for m in re.finditer(rb"\xcc+", code)
            if m.end() < len(code) and (self.lo + m.end()) % ALIGN == 0
        ]
        starts = sorted(set(starts))
        funcs: list[Func] = []
        for i, s in enumerate(starts):
            end = starts[i + 1] if i + 1 < len(starts) else len(code)
            body = code[s:end].rstrip(b"\xcc")
            stream = self.stream(self.lo + s, len(body))
            digest = hashlib.blake2b(b"".join(b for _, b in stream), digest_size=HASH_BYTES)
            funcs.append((self.lo + s, len(body), len(stream), digest.digest()))
        return funcs


class Aligned:
    """Two function lists aligned by hash.

    Args:
        old: Function list of the old file.
        new: Function list of the new file.
    """

    def __init__(self, old: list[Func], new: list[Func]) -> None:
        """Align the lists.

        Args:
            old: Function list of the old file.
            new: Function list of the new file.
        """
        self.old, self.new = old, new
        self.starts = [f[0] for f in old]
        self.pairs: dict[int, int] = {}
        self.regions: dict[int, tuple[int, int, int, int]] = {}
        matcher = difflib.SequenceMatcher(
            None, [f[3] for f in old], [f[3] for f in new], autojunk=False
        )
        for tag, i1, i2, j1, j2 in matcher.get_opcodes():
            if tag == "equal":
                self.pairs.update({i1 + k: j1 + k for k in range(i2 - i1)})
            else:
                for i in range(i1, i2):
                    self.regions[i] = (i1, i2, j1, j2)

    def function_of(self, rva: int) -> int | None:
        """Return the index of the old function that holds an RVA.

        Args:
            rva: Old RVA.

        Returns:
            The index, or None outside every function.
        """
        i = bisect.bisect_right(self.starts, rva) - 1
        if i >= 0 and rva < self.old[i][0] + self.old[i][1]:
            return i
        return None


def move(aligned: Aligned, old_img: Image, new_img: Image, rva: int) -> tuple[int | None, str]:
    """Move one old RVA to the new file.

    Args:
        aligned: The aligned function lists.
        old_img: Old image.
        new_img: New image.
        rva: Old RVA (the start of an instruction).

    Returns:
        ``(new RVA or None, "exact" | "region" | "outside")``.
    """
    i = aligned.function_of(rva)
    if i is None:
        return None, "outside"
    if i in aligned.pairs:
        return aligned.new[aligned.pairs[i]][0] + rva - aligned.old[i][0], "exact"
    i1, i2, j1, j2 = aligned.regions[i]
    old_s = [p for k in range(i1, i2) for p in old_img.stream(aligned.old[k][0], aligned.old[k][1])]
    new_s = [p for k in range(j1, j2) for p in new_img.stream(aligned.new[k][0], aligned.new[k][1])]
    matcher = difflib.SequenceMatcher(
        None, [b for _, b in old_s], [b for _, b in new_s], autojunk=False
    )
    for tag, a1, a2, b1, _ in matcher.get_opcodes():
        if tag == "equal":
            for k in range(a2 - a1):
                if old_s[a1 + k][0] == rva:
                    return new_s[b1 + k][0], "region"
    return None, "region"


def main() -> int:
    """Run one command.

    Returns:
        Process exit code.
    """
    args = sys.argv
    if len(args) >= MIN_ARGS and args[1] == "index":
        funcs = Image(Path(args[2])).functions()
        Path(args[3]).write_bytes(pickle.dumps(funcs))
        print(len(funcs), "functions")
    elif len(args) >= MAP_ARGS and args[1] == "map":
        old_img, new_img = Image(Path(args[2])), Image(Path(args[3]))
        aligned = Aligned(
            pickle.loads(Path(args[4]).read_bytes()),  # noqa: S301 - our own index file
            pickle.loads(Path(args[5]).read_bytes()),  # noqa: S301 - our own index file
        )
        for text in args[6:]:
            new, kind = move(aligned, old_img, new_img, int(text, 16))
            print(f"{text} -> {new and hex(new)} ({kind})")
    elif len(args) >= STATS_ARGS and args[1] == "stats":
        aligned = Aligned(
            pickle.loads(Path(args[2]).read_bytes()),  # noqa: S301 - our own index file
            pickle.loads(Path(args[3]).read_bytes()),  # noqa: S301 - our own index file
        )
        total = len(aligned.old)
        print(f"{len(aligned.pairs)} of {total} old functions identical, {len(aligned.new)} new")
    else:
        print(__doc__)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
