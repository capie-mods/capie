"""Export and validate all static pin evidence; runtime token values stay separate."""

from __future__ import annotations

import importlib
import json
from pathlib import Path
from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from collections.abc import Callable

from verify_evidence import verify

ROOT = Path(__file__).resolve().parent.parent
BASE = 0x100000000
REL32_SIZE = 4
MAX_INSTRUCTION = 15
DIGEST = "09419b087f8b4656c00df337bf838ff34c9b1d816b76cef5383ae6a4ac15b1fb"
GROUPS = (
    "entries",
    "signatures",
    "instruction_pins",
    "target_pins",
    "vtable_pins",
    "supplemental",
    "pins",
    "coverage",
)


def number(value: str | int) -> int:
    """Return an unsigned RVA/integer, rejecting negative or oversized values.

    Raises:
        ValueError: The input is not an unsigned 64-bit integer.
    """
    result = int(value, 0) if isinstance(value, str) else value
    if type(result) is not int or not 0 <= result < 2**64:
        msg = f"Invalid unsigned number: {value!r}"
        raise ValueError(msg)
    return result


def index_manifest(data: dict) -> dict:
    """Index all concrete records, excluding descriptive coverage dictionaries.

    Returns:
        Records by unique identifier.

    Raises:
        ValueError: The digest differs or a record ID is duplicated.
    """
    if data.get("sha256", data.get("target_sha256")) != DIGEST:
        msg = "Unexpected build digest"
        raise ValueError(msg)
    result = {}
    for key in GROUPS:
        group = data.get(key, [])
        if not isinstance(group, list):
            continue
        for pin in group:
            if pin["id"] in result:
                msg = f"Duplicate source id: {pin['id']}"
                raise ValueError(msg)
            result[pin["id"]] = pin
    return result


def resolve_reference(indexes: dict, source: str, name: str) -> dict:
    """Resolve a reference chain to a concrete record.

    Returns:
        The concrete destination record.

    Raises:
        ValueError: A reference is missing or cyclic.
    """
    seen = set()
    while True:
        key = (source, name)
        if key in seen or source not in indexes or name not in indexes[source]:
            msg = f"Missing or cyclic reference: {source}/{name}"
            raise ValueError(msg)
        seen.add(key)
        pin = indexes[source][name]
        if "manifest_reference" not in pin:
            return pin
        ref = pin["manifest_reference"]
        source, name = ref["file"], ref["id"]


def check_dependencies(indexes: dict, source: str, pin: dict) -> None:
    """Validate both companion forms and cross-manifest references.

    Raises:
        ValueError: A companion target contradicts the declared compound pin.
    """
    resolved = resolve_reference(indexes, source, pin["id"])
    if "manifest_reference" in pin and (
        resolved["id"] != pin["id"] or resolved.get("kind") == "runtime_u32"
    ):
        msg = f"{pin['id']}: reference changes role or points to runtime-only evidence"
        raise ValueError(msg)
    companions = pin.get("requires_companion", []) + pin.get("required_identity_companions", [])
    for name in companions:
        resolve_reference(indexes, source, name)
    if "compound_checks" in pin:
        check = pin["compound_checks"]
        body = resolve_reference(indexes, source, check["required_companion_id"])
        if number(body["rva"]) != number(check["relative_target_rva"]):
            msg = f"{pin['id']}: companion body target mismatch"
            raise ValueError(msg)


class Exporter:
    """Validate bytes through an injected file mapping and collect bounded evidence."""

    def __init__(self, data: bytes, offset: Callable[[int], int]) -> None:
        """Retain immutable binary bytes and its VA-to-file-offset mapping."""
        self.data = data
        self.offset = offset
        self.pins: list[dict] = []

    def raw(self, rva: str | int, size: int) -> bytes:
        """Return a nonempty contiguous mapped file range.

        Raises:
            ValueError: The range overflows, is truncated, or crosses file mappings.
        """
        va = BASE + number(rva)
        start = self.offset(va)
        if (
            size <= 0
            or va + size > 2**64
            or start < 0
            or start + size > len(self.data)
            or self.offset(va + size - 1) != start + size - 1
        ):
            msg = "Invalid mapped byte range"
            raise ValueError(msg)
        return self.data[start : start + size]

    def add(self, name: str, rva: str | int, encoded: str) -> None:
        """Check one exact range before appending it.

        Raises:
            ValueError: The recorded bytes differ from the file.
        """
        expected = bytes.fromhex(encoded)
        if self.raw(rva, len(expected)) != expected:
            msg = f"{name}: recorded bytes mismatch"
            raise ValueError(msg)
        va = BASE + number(rva)
        self.pins.append(
            {"id": name, "file_offset": self.offset(va), "hex": expected.hex(), "va": hex(va)}
        )

    def relative(self, pin: dict) -> None:
        """Check signed rel32 against the explicitly recorded next instruction.

        Raises:
            ValueError: The displacement bounds or computed target differ.
        """
        if "displacement_rva" not in pin:
            return
        field = number(pin["displacement_rva"])
        next_rva = number(pin["next_instruction_rva"])
        if not REL32_SIZE <= next_rva - field <= MAX_INSTRUCTION:
            msg = f"{pin['id']}: invalid rel32 instruction end"
            raise ValueError(msg)
        delta = int.from_bytes(self.raw(field, 4), "little", signed=True)
        if next_rva + delta != number(pin["target_rva"]):
            msg = f"{pin['id']}: relative target mismatch"
            raise ValueError(msg)

    def compound(self, pin: dict) -> None:
        """Validate the constructor thunk's fixed address, JMP opcode and target.

        Raises:
            ValueError: The thunk's recorded address or opcode differs.
        """
        if "compound_checks" not in pin:
            return
        check = pin["compound_checks"]
        jump = number(check["jump_instruction_rva"])
        if number(pin["rva"]) != number(check["fixed_rva"]) or self.raw(jump, 1) != b"\xe9":
            msg = f"{pin['id']}: invalid compound thunk"
            raise ValueError(msg)
        self.relative(
            {
                "id": pin["id"],
                "displacement_rva": jump + 1,
                "next_instruction_rva": jump + 5,
                "target_rva": check["relative_target_rva"],
            }
        )

    def pointer(self, name: str, pin: dict) -> None:
        """Check recorded pointer bytes, slot location and unslid target.

        Raises:
            ValueError: Any pointer representation or slot address contradicts its target.
        """
        if not any(key in pin for key in ("disk_unslid_qword", "file_hex", "file_slot_hex")):
            return
        target = BASE + number(pin["target_rva"])
        expected = target.to_bytes(8, "little")
        rva = pin.get("entry_rva", pin.get("rva"))
        if "vtable_rva" in pin:
            slot = number(pin["vtable_rva"]) + number(pin["slot"])
            if "file_slot_hex" in pin:
                rva = slot
            elif number(rva) != slot:
                msg = f"{name}: slot address mismatch"
                raise ValueError(msg)
        for key in ("file_hex", "file_slot_hex"):
            if key in pin and bytes.fromhex(pin[key]) != expected:
                msg = f"{name}: pointer target mismatch"
                raise ValueError(msg)
        if "disk_unslid_qword" in pin and number(pin["disk_unslid_qword"]) != target:
            msg = f"{name}: recorded pointer value mismatch"
            raise ValueError(msg)
        self.add(name + "/pointer", rva, expected.hex())

    def record(self, name: str, pin: dict) -> None:
        """Export every explicit byte representation and validate all target kinds.

        Raises:
            ValueError: A jump-table target differs or a record would be silently omitted.
        """
        before = len(self.pins)
        self.relative(pin)
        self.compound(pin)
        if pin.get("kind") == "runtime_u32" or "manifest_reference" in pin:
            return
        encoded = pin.get("hex", pin.get("bytes"))
        if encoded is not None:
            self.add(name, pin.get("rva", pin.get("instruction_rva")), encoded)
        instruction = pin.get("instruction")
        if isinstance(instruction, dict):
            self.add(name + "/instruction", instruction["rva"], instruction["hex"])
        for i, item in enumerate(pin.get("evidence", [])):
            self.add(name + f"/evidence-{i}", item["rva"], item["hex"])
        if pin.get("kind") == "relative_jump_table_entry":
            delta = int.from_bytes(bytes.fromhex(pin["hex"]), "little", signed=True)
            if number(pin["base_rva"]) + delta != number(pin["target_rva"]):
                msg = f"{name}: jump-table target mismatch"
                raise ValueError(msg)
        self.pointer(name, pin)
        if len(self.pins) == before:
            msg = f"{name}: no exported evidence for record"
            raise ValueError(msg)


def build() -> dict:
    """Load all reviewed manifests and verify the normalized export against HOI4.

    Returns:
        Exact-build evidence manifest, with explicit runtime exclusions.

    Raises:
        ValueError: Source metadata, bytes, targets or final verification fail.
    """
    macho = importlib.import_module("macho")
    sources = [f"{name}-pins.json" for name in ("editor", "layers", "core", "host")]
    indexes = {name: index_manifest(json.loads((ROOT / name).read_text())) for name in sources}
    exporter = Exporter(macho.DATA, macho.fo)
    excluded = []
    for source, records in indexes.items():
        for pin in records.values():
            check_dependencies(indexes, source, pin)
            name = Path(source).stem + "/" + pin["id"]
            exporter.record(name, pin)
            if pin.get("kind") == "runtime_u32":
                excluded.append(name)
    result = {
        "sha256": DIGEST,
        "scope": (
            "Static file integrity and recorded targets only; no runtime ABI or hook safety. "
            "Runtime BSS token values excluded; initializer evidence retained."
        ),
        "source_manifests": ["../" + name for name in sources],
        "runtime_value_exclusions": excluded,
        "source_record_count": sum(map(len, indexes.values())),
        "validated_reference_count": sum(
            "manifest_reference" in pin for records in indexes.values() for pin in records.values()
        ),
        "pins": exporter.pins,
    }
    failures = verify(macho.DATA, result)
    if failures:
        raise ValueError(failures)
    return result


if __name__ == "__main__":
    manifest = build()
    Path(__file__).with_name("port-pin-evidence.json").write_text(
        json.dumps(manifest, indent=2) + "\n"
    )
    print(f"Verified and exported {len(manifest['pins'])} static evidence ranges.")
