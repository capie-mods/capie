"""Resolve the rebuild-proof anchors of ``anchors.json`` in a ``hoi4.exe`` and print every RVA.

Usage::

    python -I locate.py <exe> <anchors.json> [--check <game_layout.hpp>] [--json <out.json>]

Byte patterns break when the compiler changes; RTTI class names, string literals and the call
graph around them do not. Each entry of ``anchors.json`` names one constant of
``core/game_layout.hpp`` and a small expression that finds it again from such an anchor (the
format is described in ``README.md``). This script evaluates every expression in the given
executable and prints ``name  new-RVA  layout-RVA  status``.

With ``--check`` it compares each result with the constant of the same name in
``game_layout.hpp`` and exits with status 1 when one differs or cannot be resolved. On the
build the sources pin this is the self-check of the anchors; on a new build the printed RVAs
are the first guess of the port, which the byte patterns (``port.py triage``) then confirm.

Function boundaries come from the exception directory (``.pdata``): chained unwind entries are
followed to their primary entry, so an anchor that falls into a split-off cold block still
names the function it belongs to.
"""

import argparse
import bisect
import json
import pathlib
import re
import struct
import sys
from collections.abc import Iterator
from dataclasses import dataclass, field
from typing import Any, Protocol, cast

import capstone
import numpy as np
import pefile

RIP_EXTRA_SIZES = (0, 1, 2, 4)
"""Immediate bytes that may follow a RIP displacement (``cmp [rip+d], imm``)."""

CALL_OPCODE = 0xE8
"""Opcode of ``call rel32``."""

JMP_OPCODE = 0xE9
"""Opcode of ``jmp rel32``."""

INT3 = 0xCC
"""Padding byte MSVC puts between functions."""

MODRM_RIP_MASK = 0xC7
"""ModRM bits that select RIP-relative addressing (mod 00, r/m 101)."""

MODRM_RIP = 0x05
"""ModRM value of RIP-relative addressing after ``MODRM_RIP_MASK``."""

UNW_FLAG_CHAININFO = 0x4
"""Unwind-info flag of a chained entry (the primary entry follows the unwind codes)."""

RTTI_SIGNATURE = 1
"""Signature field of an x64 complete object locator."""

MAX_DECODE = 0x100000
"""Upper bound of bytes decoded for one function chunk."""

GETTER_INSNS = 2
"""Instructions of a member getter (``mov rax, [rcx+disp]; ret``)."""

ARRAY_INDEX = re.compile(r"^(.*)\[(\d+)\]$")
"""``name[index]`` reference to one element of a list result."""

LAYOUT_CONSTANT = re.compile(
    r"inline constexpr std::uintptr_t (\w+) = (0x[0-9A-Fa-f]+);", re.MULTILINE
)
"""A constant of ``game_layout.hpp`` written as a hexadecimal literal."""


Expr = dict[str, Any]
"""One anchor expression or predicate object of ``anchors.json`` (parsed JSON)."""


class Disassembler(Protocol):
    """The part of ``capstone.Cs`` this script calls (capstone ships no type hints)."""

    def disasm(self, code: bytes, offset: int, count: int = 0) -> Iterator[Any]:
        """Decode instructions.

        Args:
            code: Bytes to decode.
            offset: Address of the first byte.
            count: Maximum number of instructions (0 = all).

        Returns:
            The decoded instructions (``capstone.CsInsn``).
        """
        ...


class AnchorError(Exception):
    """An anchor expression does not resolve to exactly one value."""


@dataclass
class Reference:
    """One instruction that references an address.

    Attributes:
        insn: RVA of the referencing instruction.
        kind: ``call``, ``jmp`` or ``rip`` (a RIP-relative memory operand).
        target: The referenced RVA.
    """

    insn: int
    kind: str
    target: int


@dataclass
class Instruction:
    """The facts of one decoded instruction that the anchors use.

    Attributes:
        address: RVA.
        size: Length in bytes.
        mnemonic: Capstone mnemonic.
        op_str: Capstone operand text.
        branch: Target of a direct ``call``/``jmp``, else ``None``.
        rip: Target of a RIP-relative memory operand, else ``None``.
        disps: Displacements of the non-RIP memory operands.
        imms: Immediate operands (other than branch targets).
    """

    address: int
    size: int
    mnemonic: str
    op_str: str
    branch: int | None
    rip: int | None
    disps: list[int] = field(default_factory=list)
    imms: list[int] = field(default_factory=list)


class Image:
    """A mapped PE image with the indexes the anchors need.

    Args:
        path: Executable to load.
    """

    def __init__(self, path: pathlib.Path) -> None:
        """Map the image, index ``.pdata`` and the rel32 fields of ``.text``.

        Args:
            path: Executable to load.
        """
        pe = cast("Any", pefile.PE(str(path), fast_load=True))
        self.base = int(pe.OPTIONAL_HEADER.ImageBase)
        self.img = cast("bytes", pe.get_memory_mapped_image())
        sections: dict[str, tuple[int, int]] = {
            s.Name.rstrip(b"\0").decode(): (int(s.VirtualAddress), int(s.Misc_VirtualSize))
            for s in pe.sections
        }
        self.text_lo, size = sections[".text"]
        self.text_hi = self.text_lo + size
        self.rdata_lo, size = sections[".rdata"]
        self.rdata_hi = self.rdata_lo + size
        self._index_text()
        self._index_pdata(*sections[".pdata"])
        md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
        md.detail = True
        self.md = cast("Disassembler", md)
        self._insns: dict[int, list[Instruction]] = {}

    # ---- indexes ---------------------------------------------------------------------------
    def _index_text(self) -> None:
        """Compute, for every byte position, ``position + int32 at position`` (wrapping)."""
        raw = np.frombuffer(self.img, np.uint8, self.text_hi - self.text_lo, self.text_lo)
        n = len(raw) - 4
        rel = np.empty(n, np.int32)
        for k in range(4):
            count = (n - k + 3) // 4
            words = np.frombuffer(raw[k : k + 4 * count].tobytes(), "<i4")
            rel[k::4] = words + np.arange(k, k + 4 * count, 4, dtype=np.int32)
        self.raw = raw
        self.rel = rel

    def _index_pdata(self, lo: int, size: int) -> None:
        """Read the runtime function table and map each entry to its primary function.

        Args:
            lo: RVA of the ``.pdata`` section.
            size: Its virtual size.
        """
        count = size // 12
        entries = [self.u32x3(lo + 12 * i) for i in range(count)]
        entries = [e for e in entries if e[0] and e[1] > e[0]]
        entries.sort()
        self.chunk_begin = [e[0] for e in entries]
        self.chunk_end = [e[1] for e in entries]
        self.chunk_root: list[int] = []
        for begin, _end, unwind in entries:
            self.chunk_root.append(self._root(begin, unwind))
        self.chunks_of: dict[int, list[tuple[int, int]]] = {}
        for begin, end, root in zip(self.chunk_begin, self.chunk_end, self.chunk_root, strict=True):
            self.chunks_of.setdefault(root, []).append((begin, end))

    def _root(self, begin: int, unwind: int) -> int:
        """Follow chained unwind information to the primary function entry.

        Args:
            begin: Begin RVA of the entry.
            unwind: Unwind-info RVA of the entry.

        Returns:
            Begin RVA of the primary entry.
        """
        for _ in range(32):
            flags = self.img[unwind] >> 3
            if not flags & UNW_FLAG_CHAININFO:
                return begin
            codes = self.img[unwind + 2]
            parent = unwind + 4 + 2 * (codes + (codes & 1))
            begin, _end, unwind = self.u32x3(parent)
        return begin

    def u32x3(self, rva: int) -> tuple[int, int, int]:
        """Read three little-endian 32-bit values (one ``RUNTIME_FUNCTION``).

        Args:
            rva: Address.

        Returns:
            The three values.
        """
        a, b, c = struct.unpack_from("<III", self.img, rva)
        return int(a), int(b), int(c)

    # ---- primitives ------------------------------------------------------------------------
    def function_of(self, rva: int) -> int:
        """Return the primary function that contains an RVA.

        Args:
            rva: Address inside a function.

        Returns:
            Begin RVA of the function.

        Raises:
            AnchorError: If no ``.pdata`` entry covers the address.
        """
        i = bisect.bisect_right(self.chunk_begin, rva) - 1
        if i >= 0 and rva < self.chunk_end[i]:
            return self.chunk_root[i]
        # A leaf function needs no unwind data. MSVC aligns functions to 16 bytes and pads the
        # gap with int3, so the start is the last aligned address after padding.
        lo = self.chunk_end[i] if i >= 0 else self.text_lo
        if not self.text_lo <= rva < self.text_hi:
            msg = f"{rva:#x} is not in .text"
            raise AnchorError(msg)
        p = rva & ~0xF
        while p > lo and self.img[p - 1] != INT3:
            p -= 0x10
        return max(p, lo)

    def references(self, target: int) -> list[Reference]:
        """Return every direct call/jmp and RIP-relative operand in ``.text`` that hits a target.

        Args:
            target: Referenced RVA.

        Returns:
            References sorted by instruction address.
        """
        out: dict[int, Reference] = {}
        for extra in RIP_EXTRA_SIZES:
            want = target - self.text_lo - 4 - extra
            for q in np.nonzero(self.rel == want)[0].tolist():
                ref = self._classify(int(q), extra, target)
                if ref is not None:
                    out[ref.insn] = ref
        return sorted(out.values(), key=lambda r: r.insn)

    def _classify(self, q: int, extra: int, target: int) -> Reference | None:
        """Decode the instruction around a displacement candidate and confirm the reference.

        Args:
            q: Offset of the 32-bit field in ``.text``.
            extra: Immediate bytes assumed after the field.
            target: Referenced RVA.

        Returns:
            The reference, or ``None`` when no instruction decodes to it.
        """
        op = int(self.raw[q - 1])
        if extra == 0 and op in {CALL_OPCODE, JMP_OPCODE}:
            kind = "call" if op == CALL_OPCODE else "jmp"
            return Reference(self.text_lo + q - 1, kind, target)
        if (op & MODRM_RIP_MASK) != MODRM_RIP:
            return None
        field_rva = self.text_lo + q
        # Prefer the function's own decoding: a start a few bytes too late can decode to a
        # shorter instruction with the same displacement and end (a REX prefix dropped).
        try:
            func = self.function_of(field_rva)
        except AnchorError:
            func = None
        if func is not None:
            for ins in self.instructions(func):
                if ins.address <= field_rva < ins.address + ins.size:
                    if ins.rip == target:
                        return Reference(ins.address, "rip", target)
                    return None
        for back in range(6, 1, -1):
            start = field_rva - back
            ins = self.decode_one(start)
            if ins is not None and ins.rip == target and ins.size == back + 4 + extra:
                return Reference(start, "rip", target)
        return None

    def decode_one(self, rva: int) -> Instruction | None:
        """Decode one instruction.

        Args:
            rva: Address of the instruction.

        Returns:
            The instruction, or ``None`` when the bytes do not decode.
        """
        for ins in self.md.disasm(self.img[rva : rva + 16], rva, 1):
            return self._facts(ins)
        return None

    def _facts(self, ins: Any) -> Instruction:  # noqa: ANN401 - capstone has no type hints
        """Extract branch, RIP and displacement facts from a capstone instruction.

        Args:
            ins: Decoded instruction (with detail).

        Returns:
            The extracted facts.
        """
        branch = None
        rip = None
        disps: list[int] = []
        imms: list[int] = []
        for operand in ins.operands:
            if operand.type == capstone.x86.X86_OP_IMM and ins.mnemonic in {"call", "jmp"}:
                branch = int(operand.imm)
            elif operand.type == capstone.x86.X86_OP_IMM:
                imms.append(int(operand.imm) & 0xFFFFFFFFFFFFFFFF)
            elif operand.type == capstone.x86.X86_OP_MEM:
                if operand.mem.base == capstone.x86.X86_REG_RIP:
                    rip = int(ins.address + ins.size + operand.mem.disp)
                else:
                    disps.append(int(operand.mem.disp))
        return Instruction(
            int(ins.address), int(ins.size), ins.mnemonic, ins.op_str, branch, rip, disps, imms
        )

    def instructions(self, func: int) -> list[Instruction]:
        """Decode every chunk of a function, chunks in address order.

        Args:
            func: Begin RVA of the primary function.

        Returns:
            Instructions of all chunks.
        """
        if func in self._insns:
            return self._insns[func]
        out: list[Instruction] = []
        chunks = self.chunks_of.get(func)
        if chunks is None:
            # Leaf function without unwind data: decode up to the int3 padding.
            i = bisect.bisect_right(self.chunk_begin, func)
            stop = self.chunk_begin[i] if i < len(self.chunk_begin) else self.text_hi
            for ins in self.md.disasm(self.img[func : min(stop, func + MAX_DECODE)], func):
                if ins.mnemonic == "int3":
                    break
                out.append(self._facts(ins))
            chunks = []
        for begin, end in sorted(chunks):
            stop = min(end, begin + MAX_DECODE)
            pos = begin
            while pos < stop:
                for ins in self.md.disasm(self.img[pos:stop], pos):
                    out.append(self._facts(ins))
                    pos = ins.address + ins.size
                pos += 1  # capstone stopped at an undecodable byte (data in code): step over it
        self._insns[func] = out
        return out

    def strings(self, text: str) -> list[int]:
        """Return the RVAs of a NUL-terminated string literal that starts after a NUL.

        Args:
            text: Literal (Python escapes already applied).

        Returns:
            RVAs, ascending.
        """
        needle = text.encode("latin1") + b"\0"
        out = []
        i = self.img.find(needle)
        while i != -1:
            if self.img[i - 1] == 0:
                out.append(i)
            i = self.img.find(needle, i + 1)
        return out

    def qword(self, rva: int) -> int:
        """Read a little-endian 64-bit value.

        Args:
            rva: Address.

        Returns:
            The value.
        """
        return int(struct.unpack_from("<Q", self.img, rva)[0])

    def rtti_vtables(self, name: str) -> dict[int, int]:
        """Return the vtables of one MSVC class, keyed by their subobject offset.

        Args:
            name: Class name as in ``.?AV<name>@@``.

        Returns:
            ``{offset: vtable RVA}``.
        """
        out: dict[int, int] = {}
        for prefix in (b".?AV", b".?AU"):
            needle = prefix + name.encode() + b"@@\0"
            i = self.img.find(needle)
            while i != -1:
                td = i - 0x10
                for hit in self._u32_hits(td, self.rdata_lo, self.rdata_hi):
                    col = hit - 0xC
                    sig, offset = struct.unpack_from("<II", self.img, col)
                    selfp = struct.unpack_from("<I", self.img, col + 0x14)[0]
                    if sig != RTTI_SIGNATURE or selfp != col:
                        continue
                    for slot in self._u64_hits(self.base + col):
                        out[offset] = slot + 8
                i = self.img.find(needle, i + 1)
        return out

    def _u32_hits(self, value: int, lo: int, hi: int) -> list[int]:
        """Return 4-aligned RVAs in ``[lo, hi)`` that hold a 32-bit value.

        Args:
            value: Value to find.
            lo: First RVA.
            hi: End RVA.

        Returns:
            Matching RVAs.
        """
        words = np.frombuffer(self.img, "<u4", (hi - lo) // 4, lo)
        return [lo + 4 * int(k) for k in np.nonzero(words == value)[0]]

    def _u64_hits(self, value: int) -> list[int]:
        """Return 8-aligned RVAs in ``.rdata`` that hold a 64-bit value.

        Args:
            value: Value to find.

        Returns:
            Matching RVAs.
        """
        lo = self.rdata_lo
        words = np.frombuffer(self.img, "<u8", (self.rdata_hi - lo) // 8, lo)
        return [lo + 8 * int(k) for k in np.nonzero(words == value)[0]]


# ---- anchor expressions ---------------------------------------------------------------------
class Resolver:
    """Evaluate the anchor expressions of ``anchors.json`` against one image.

    Args:
        image: The executable.
        entries: Anchor entries by name.
    """

    def __init__(self, image: Image, entries: dict[str, Expr]) -> None:
        """Remember the image and the entries; nothing is resolved yet.

        Args:
            image: The executable.
            entries: Anchor entries by name.
        """
        self.image = image
        self.entries = entries
        self.done: dict[str, int | list[int]] = {}
        self._busy: set[str] = set()

    def value(self, ref: str | int | Expr) -> int:
        """Resolve a reference to another entry (``name`` or ``name[i]``) or a nested expression.

        Args:
            ref: Entry name, indexed entry name, nested expression or literal integer.

        Returns:
            The RVA.

        Raises:
            AnchorError: If the reference is a list without an index.
        """
        if isinstance(ref, int):
            return ref
        if isinstance(ref, dict):
            got = self.expr(ref)
            return only(got, str(ref)) if isinstance(got, list) else got
        m = ARRAY_INDEX.match(ref)
        if m:
            got = self.resolve(m[1])
            if not isinstance(got, list):
                msg = f"{m[1]} is not a list"
                raise AnchorError(msg)
            return pick(got, int(m[2]), ref)
        got = self.resolve(ref)
        if isinstance(got, list):
            msg = f"{ref} is a list; name an element"
            raise AnchorError(msg)
        return got

    def resolve(self, name: str) -> int | list[int]:
        """Resolve one entry by name (memoised).

        Args:
            name: Entry name.

        Returns:
            The RVA, or a list of RVAs for a ``many`` entry.

        Raises:
            AnchorError: On an unknown name or a cycle.
        """
        if name in self.done:
            return self.done[name]
        if name not in self.entries:
            msg = f"unknown anchor {name}"
            raise AnchorError(msg)
        if name in self._busy:
            msg = f"cycle at {name}"
            raise AnchorError(msg)
        self._busy.add(name)
        try:
            entry = self.entries[name]
            got = self.expr(entry["find"])
            if isinstance(got, list) and not entry.get("many"):
                got = only(got, name)
            if isinstance(got, int):
                self.check(got, entry.get("check", {}), name)
                for other in entry.get("confirm", []):
                    again = self.expr(other)
                    again = only(again, f"{name} confirm") if isinstance(again, list) else again
                    if again != got:
                        msg = f"{name}: {got:#x}, but its confirmation {other} gives {again:#x}"
                        raise AnchorError(msg)
            self.done[name] = got
            return got
        finally:
            self._busy.discard(name)

    def expr(self, e: Expr) -> int | list[int]:  # noqa: C901, PLR0911, PLR0912 - one branch per kind
        """Evaluate one expression.

        Args:
            e: Expression object (exactly one kind key, plus its modifiers).

        Returns:
            One RVA, or several (the caller then requires exactly one unless ``nth`` picks).

        Raises:
            AnchorError: On an unknown expression kind.
        """
        img = self.image
        if "rtti" in e:
            vts = img.rtti_vtables(e["rtti"])
            offset = int(e.get("offset", 0))
            if offset not in vts:
                msg = f"no vtable of {e['rtti']} at offset {offset:#x}"
                raise AnchorError(msg)
            return vts[offset]
        if "slot" in e:
            vt = self.value(e["vtable"])
            return img.qword(vt + int(e["slot"], 16)) - img.base
        if "string" in e:
            refs = [r for s in img.strings(e["string"]) for r in img.references(s)]
            funcs = sorted({img.function_of(r.insn) for r in refs})
            return self.filtered(funcs, e)
        if "refs" in e:
            target = self.value(e["refs"])
            funcs = sorted({img.function_of(r.insn) for r in img.references(target)})
            return self.filtered(funcs, e)
        if "callers" in e:
            target = self.value(e["callers"])
            refs = [r for r in img.references(target) if r.kind in {"call", "jmp"}]
            funcs = sorted({img.function_of(r.insn) for r in refs})
            return self.filtered(funcs, e)
        if "callees" in e:
            func = self.value(e["callees"])
            seen: list[int] = []
            kinds = {"call", "jmp"} if e.get("jumps") else {"call"}
            for ins in img.instructions(func):
                target = ins.branch
                if ins.mnemonic not in kinds or target is None or target in seen:
                    continue
                if img.text_lo <= target < img.text_hi and img.function_of(target) != func:
                    seen.append(target)
            return self.filtered(seen, e)
        if "ctor" in e:
            return self.filtered(self.constructors(self.value(e["ctor"])), e)
        if "rip_in" in e:
            return self.filtered(self.rip_targets(e), e)
        if "near_string" in e or "near_call" in e:
            return self.near(e)
        if "intersect" in e:
            return self.intersect(e)
        if "add" in e:
            return self.value(e["base"]) + int(e["add"], 16)
        msg = f"unknown expression {sorted(e)}"
        raise AnchorError(msg)

    def rip_targets(self, e: Expr) -> list[int]:
        """RIP-relative operand targets in a function, in instruction order.

        ``mnemonic`` keeps only that instruction kind. ``then_disp`` keeps only a pointer load
        (``mov reg, qword ptr [rip+d]``) whose register is used as the base of an operand with
        that displacement within the next eight instructions (``[reg + 0x310]`` after
        loading the game state, for example).

        Args:
            e: Expression with ``rip_in``.

        Returns:
            Distinct targets.
        """
        insns = self.image.instructions(self.value(e["rip_in"]))
        seen: list[int] = []
        for k, ins in enumerate(insns):
            if ins.rip is None or ins.rip in seen:
                continue
            if e.get("mnemonic") and ins.mnemonic != e["mnemonic"]:
                continue
            if "then_disp" in e:
                reg = ins.op_str.split(",")[0]
                if ins.mnemonic != "mov" or "qword ptr [rip" not in ins.op_str:
                    continue
                use = f"[{reg} + {int(e['then_disp'], 16):#x}]"
                if not any(use in later.op_str for later in insns[k + 1 : k + 9]):
                    continue
            seen.append(ins.rip)
        return seen

    def constructors(self, vtable: int) -> list[int]:
        """Functions that load a vtable's address, without its destructor.

        The scalar deleting destructor in slot 0 and the functions it calls or jumps to also
        store the vtable (to run the base destructors); what remains are the constructors.

        Args:
            vtable: Vtable RVA.

        Returns:
            Constructor candidates, ascending.
        """
        img = self.image
        users = {img.function_of(r.insn) for r in img.references(vtable) if r.kind == "rip"}
        dtors = {img.qword(vtable) - img.base}
        for _ in range(2):  # the deleting destructor, the destructor, its thunk
            dtors |= {
                i.branch
                for d in list(dtors)
                for i in img.instructions(d)
                if i.branch is not None and img.text_lo <= i.branch < img.text_hi
            }
        return sorted(users - dtors)

    def near(self, e: Expr) -> list[int]:
        """Targets of the calls (or RIP operands) next to each site of an anchor.

        The site is a reference to a string literal (``near_string``) or a call of another
        anchor (``near_call``). ``what`` is ``call`` or ``rip``; ``dir`` is ``after`` (default)
        or ``before``; ``skip`` counts matching instructions to pass over first. With ``in``
        only sites inside that function count.

        Args:
            e: Expression with ``near_string`` or ``near_call``.

        Returns:
            Distinct targets, in site order.
        """
        img = self.image
        what = e.get("what", "call")
        after = e.get("dir", "after") == "after"
        skip = int(e.get("skip", 0))
        within = self.value(e["in"]) if "in" in e else None
        if "near_string" in e:
            anchors = img.strings(e["near_string"])
            sites = [r for a in anchors for r in img.references(a)]
        else:
            anchors = [self.value(e["near_call"])]
            sites = [r for r in img.references(anchors[0]) if r.kind == "call"]
        out: list[int] = []
        for ref in sites:
            func = img.function_of(ref.insn)
            if within is not None and func != within:
                continue
            insns = img.instructions(func)
            pos = next((i for i, ins in enumerate(insns) if ins.address == ref.insn), None)
            if pos is None:
                continue
            seq = insns[pos + 1 :] if after else list(reversed(insns[:pos]))
            hits = [
                (ins.branch if what == "call" else ins.rip)
                for ins in seq
                if (what == "call" and ins.mnemonic == "call" and ins.branch is not None)
                or (what == "rip" and ins.rip is not None and ins.rip not in anchors)
            ]
            if len(hits) > skip:
                got = hits[skip]
                if got is not None and got not in out:
                    out.append(got)
        return self.filtered(out, e)

    def intersect(self, e: Expr) -> list[int]:
        """Candidates common to every sub-expression, in the order of the first one.

        Args:
            e: Expression with ``intersect`` (a list of expressions).

        Returns:
            The common candidates after ``where`` and ``nth``.
        """
        sets = []
        for sub in e["intersect"]:
            got = self.expr(sub)
            sets.append(got if isinstance(got, list) else [got])
        common = [x for x in sets[0] if all(x in other for other in sets[1:])]
        return self.filtered(common, e)

    def filtered(self, items: list[int], e: Expr) -> list[int]:
        """Apply the ``where`` predicates, then ``nth``.

        Args:
            items: Candidate RVAs.
            e: Expression with optional ``where`` and ``nth``.

        Returns:
            The surviving candidates (one element when ``nth`` is given).
        """
        where = e.get("where")
        if where:
            items = [i for i in items if self.holds(i, where)]
        if "nth" in e:
            return [pick(items, int(e["nth"]), str(e))]
        return items

    def holds(  # noqa: C901, PLR0911, PLR0912 - one test per predicate
        self, func: int, where: Expr
    ) -> bool:
        """Test the predicates of ``where`` on one function.

        ``disp``: the body uses all these memory displacements; ``imm``: all these immediates;
        ``calls`` / ``jumps``: it calls (``call``) / jumps to (``jmp``) all these anchors;
        ``string``: it references all these literals; ``refs``: it has a RIP operand that points
        at each of these anchors; ``calls_any``: one of its direct callees satisfies the nested
        predicates; ``not``: the nested predicates fail; ``max_insns``: it has at most that many
        instructions; ``getter``: the body is ``mov rax, [rcx+disp]; ret``; ``first``: its first
        instruction has this mnemonic and operand text.

        Args:
            func: Function RVA.
            where: Predicates.

        Returns:
            ``True`` when every predicate holds.
        """
        img = self.image
        insns = img.instructions(func)
        if "disp" in where:
            used = {d for ins in insns for d in ins.disps}
            if not {int(d, 16) for d in where["disp"]} <= used:
                return False
        for key, mnemonic in (("calls", "call"), ("jumps", "jmp")):
            if key in where:
                targets = {
                    i.branch for i in insns if i.mnemonic == mnemonic and i.branch is not None
                }
                if not {self.value(c) for c in where[key]} <= targets:
                    return False
        if "imm" in where:
            used = {v for ins in insns for v in ins.imms}
            if not {int(v, 16) for v in where["imm"]} <= used:
                return False
        if "string" in where:
            rips = {ins.rip for ins in insns if ins.rip is not None}
            for text in where["string"]:
                if not rips & set(img.strings(text)):
                    return False
        if "refs" in where:
            rips = {ins.rip for ins in insns if ins.rip is not None}
            if not {self.value(r) for r in where["refs"]} <= rips:
                return False
        if "calls_any" in where:
            callees = {
                i.branch for i in insns if i.mnemonic in {"call", "jmp"} and i.branch is not None
            }
            inner = where["calls_any"]
            if not any(self.holds(c, inner) for c in callees if img.text_lo <= c < img.text_hi):
                return False
        if "not" in where and self.holds(func, where["not"]):
            return False
        if "max_insns" in where and len(insns) > int(where["max_insns"]):
            return False
        if where.get("getter") and not is_getter(insns):
            return False
        if "first" in where:
            first = insns[0] if insns else None
            if first is None or f"{first.mnemonic} {first.op_str}" != where["first"]:
                return False
        return True

    def check(self, rva: int, check: Expr, name: str) -> None:
        """Apply the ``check`` predicates to a resolved function.

        Args:
            rva: Resolved RVA.
            check: Same predicates as ``where``.
            name: Entry name, for the message.

        Raises:
            AnchorError: If a predicate fails.
        """
        if check and not self.holds(rva, check):
            msg = f"{name}: {rva:#x} fails its check {check}"
            raise AnchorError(msg)


def is_getter(insns: list[Instruction]) -> bool:
    """Tell whether a body is exactly ``mov rax, qword ptr [rcx + disp]; ret``.

    Args:
        insns: Decoded body.

    Returns:
        ``True`` for a member getter.
    """
    return (
        len(insns) == GETTER_INSNS
        and insns[0].mnemonic == "mov"
        and insns[0].op_str.startswith("rax, qword ptr [rcx")
        and insns[1].mnemonic == "ret"
    )


def only(items: list[int], what: str) -> int:
    """Return the single element of a list.

    Args:
        items: Candidates.
        what: Name for the message.

    Returns:
        The element.

    Raises:
        AnchorError: If there is not exactly one candidate.
    """
    if len(items) != 1:
        shown = ", ".join(f"{i:#x}" for i in items[:8])
        msg = f"{what}: {len(items)} candidates [{shown}]"
        raise AnchorError(msg)
    return items[0]


def pick(items: list[int], index: int, what: str) -> int:
    """Return one element of a list by index.

    Args:
        items: Candidates.
        index: Position.
        what: Name for the message.

    Returns:
        The element.

    Raises:
        AnchorError: If the index is out of range.
    """
    if not 0 <= index < len(items):
        msg = f"{what}: index {index} of {len(items)} candidates"
        raise AnchorError(msg)
    return items[index]


def layout_constants(path: pathlib.Path) -> dict[str, int]:
    """Read the hexadecimal RVAs of ``namespace rva`` in ``game_layout.hpp``.

    Args:
        path: The header.

    Returns:
        ``{name: value}``.
    """
    text = path.read_text(encoding="utf-8")
    start = text.index("namespace rva {")
    end = text.index("}  // namespace rva", start)
    return {m[1]: int(m[2], 16) for m in LAYOUT_CONSTANT.finditer(text, start, end)}


def main() -> int:
    """Resolve every anchor and print the table.

    Returns:
        0 when every anchor resolved (and, with ``--check``, matched the layout), else 1.
    """
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("exe", type=pathlib.Path)
    parser.add_argument("anchors", type=pathlib.Path)
    parser.add_argument("--check", type=pathlib.Path, help="game_layout.hpp to compare with")
    parser.add_argument("--json", type=pathlib.Path, help="write {name: rva} here")
    args = parser.parse_args()
    doc = json.loads(args.anchors.read_text(encoding="utf-8"))
    entries = {e["name"]: e for e in doc["anchors"]}
    resolver = Resolver(Image(args.exe), entries)
    layout = layout_constants(args.check) if args.check else {}
    failures = 0
    found: dict[str, int] = {}
    for name, entry in entries.items():
        if entry.get("many"):
            continue
        try:
            got = resolver.value(name)
        except AnchorError as err:
            print(f"{name:32} {'-':>9} {'':>9} FAIL {err}")
            failures += 1
            continue
        found[name] = got
        if not entry.get("layout", True):
            print(f"{name:32} {got:9X} {'':>9} helper")
            continue
        old = layout.get(name)
        if not args.check:
            print(f"{name:32} {got:9X}")
            continue
        status = "ok" if old == got else ("MISMATCH" if old is not None else "not in layout")
        failures += status != "ok"
        print(f"{name:32} {got:9X} {(old or 0):9X} {status}")
    if args.check:
        missing = sorted(set(layout) - set(found))
        for name in missing:
            print(f"{name:32} {'-':>9} {layout[name]:9X} NO ANCHOR")
        failures += len(missing)
    if args.json:
        args.json.write_text(json.dumps({k: f"{v:#x}" for k, v in found.items()}, indent=1))
    print(f"{len(found)} resolved, {failures} problem(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
