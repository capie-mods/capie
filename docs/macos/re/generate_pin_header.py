"""Generate typed descriptors using reviewed source roles and existing evidence validation."""

from __future__ import annotations

import hashlib
import importlib
import json
from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from collections.abc import Callable
from pathlib import Path

from build_pin_evidence import BASE, DIGEST, Exporter, check_dependencies, index_manifest, number

KINDS = {
    None,
    "bytes",
    "literal_bytes",
    "relative32",
    "pointer",
    "runtime_u32",
    "relative_jump_table_entry",
    "signature",
    "instruction",
    "rip_target",
    "vtable_slot",
}


class TypedExporter(Exporter):
    """Retain descriptor roles while the existing exporter validates every byte and target."""

    def __init__(
        self, data: bytes, offset: Callable[[int], int], executable: Callable[[int, int], bool]
    ) -> None:
        """Retain the exact binary mapping and bounded executable-range predicate."""
        super().__init__(data, offset)
        self.executable = executable
        self.typed = []
        self.relatives = []
        self.active = None
        self.owner = ""
        self.pointer_target = None

    def add(self, name: str, rva: str | int, encoded: str) -> None:
        """Validate bytes through Exporter, then record their semantic runtime representation.

        Raises:
            ValueError: Evidence, role or bounds fail validation.
        """
        super().add(name, rva, encoded)
        address = BASE + number(rva)
        if self.pointer_target is not None:
            target, code = self.pointer_target
            self.typed.append(
                {"id": name, "role": "pointer", "address": address, "target": target, "code": code}
            )
        else:
            data_kind = self.active and self.active.get("kind") in {
                "literal_bytes",
                "relative_jump_table_entry",
            }
            code = not data_kind
            if code and not self.executable(address, len(bytes.fromhex(encoded))):
                msg = f"{name}: executable byte span lies outside executable range"
                raise ValueError(msg)
            self.typed.append(
                {
                    "id": name,
                    "role": "bytes",
                    "address": address,
                    "hex": encoded,
                    "code": bool(code),
                }
            )

    def pointer(self, name: str, pin: dict) -> None:
        """Select pointer semantics from source fields, never identifier substrings.

        Raises:
            ValueError: Evidence, role or bounds fail validation.
        """
        if not any(key in pin for key in ("disk_unslid_qword", "file_hex", "file_slot_hex")):
            return
        target = BASE + number(pin["target_rva"])
        code = pin.get("target_code", True)
        if type(code) is not bool or (code and not self.executable(target, 1)):
            msg = f"{name}: invalid pointer target role"
            raise ValueError(msg)
        self.pointer_target = (target, code)
        try:
            super().pointer(name, pin)
        finally:
            self.pointer_target = None

    def relative(self, pin: dict) -> None:
        """Reuse bounded signed-rel32 validation and retain its explicit instruction end.

        Raises:
            ValueError: Evidence, role or bounds fail validation.
        """
        super().relative(pin)
        if "displacement_rva" not in pin:
            return
        target = BASE + number(pin["target_rva"])
        code = pin.get("target_code", self.executable(target, 1))
        if type(code) is not bool or (code and not self.executable(target, 1)):
            msg = f"{pin['id']}: invalid relative target role"
            raise ValueError(msg)
        self.relatives.append(
            {
                "id": self.owner or pin["id"],
                "displacement": BASE + number(pin["displacement_rva"]),
                "next_instruction": BASE + number(pin["next_instruction_rva"]),
                "target": target,
                "code": code,
            }
        )

    def record(self, name: str, pin: dict) -> None:
        """Reject unknown source kinds before invoking the validating exporter.

        Raises:
            ValueError: Evidence, role or bounds fail validation.
        """
        if pin.get("kind") not in KINDS:
            msg = f"{name}: unknown kind"
            raise ValueError(msg)
        self.active = pin
        self.owner = name
        try:
            super().record(name, pin)
        finally:
            self.active = None
            self.owner = ""


def collect(
    data: bytes,
    offset: Callable[[int], int],
    executable: Callable[[int, int], bool],
    sources: dict,
    expected: list[dict],
) -> TypedExporter:
    """Reconcile typed records one-to-one with the checked aggregate evidence.

    Hash identity is enforced by main before calling this reusable fixture-friendly layer.
    Existing source indexing, references, companions, bytes, pointer and rel32 checks are reused.

    Returns:
        Validated typed exporter.

    Raises:
        ValueError: Source records or aggregate coverage conflict.
    """
    indexes = {name: index_manifest(document) for name, document in sources.items()}
    exporter = TypedExporter(data, offset, executable)
    for source, records in indexes.items():
        for pin in records.values():
            check_dependencies(indexes, source, pin)
            exporter.record(Path(source).stem + "/" + pin["id"], pin)

    def indexed(records: list[dict]) -> dict:
        result = {row["id"]: row for row in records}
        if len(result) != len(records):
            msg = "Duplicate aggregate evidence id"
            raise ValueError(msg)
        return result

    if indexed(exporter.pins) != indexed(expected):
        msg = "Typed export does not match every aggregate range"
        raise ValueError(msg)
    return exporter


def render(exporter: TypedExporter) -> str:
    """Emit deterministic constexpr descriptors, parallel stable IDs and a profile digest.

    Returns:
        Complete C++ header text.
    """
    lines = [
        "/** @file macos_profile.generated.hpp",
        " * @brief Generated read-only pin descriptors; do not edit. No activation or ABI claim.",
        " * Source: docs/macos/re/generate_pin_header.py.",
        " * Jump-table target relation is checked offline only.",
        " */",
        "#pragma once",
        '#include "macos_pins.hpp"',
        "#include <array>",
        "namespace capie::core::macos::profile {",
        "/// @brief Exact file identity required before using this table.",
        f'inline constexpr char target_sha256[] = "{DIGEST}";',
    ]
    groups = {
        "bytes": [p for p in exporter.typed if p["role"] == "bytes"],
        "pointers": [p for p in exporter.typed if p["role"] == "pointer"],
        "relatives": exporter.relatives,
    }
    for i, pin in enumerate(groups["bytes"]):
        payload = bytes.fromhex(pin["hex"])
        lines += [
            "/// @brief Exact file-backed bytes of one reviewed range.",
            f"inline constexpr unsigned char payload_{i}[] = {{",
        ]
        for start in range(0, len(payload), 16):
            lines.append(
                "    " + ", ".join(f"0x{v:02x}" for v in payload[start : start + 16]) + ","
            )
        lines.append("};")

    def boolean(value: object) -> str:
        return "true" if value else "false"

    for group, records in groups.items():
        typename = {"bytes": "BytesPin", "pointers": "PointerPin", "relatives": "RelativePin"}[
            group
        ]
        lines += [
            "/// @brief All required descriptors in stable source order.",
            f"inline constexpr std::array<pins::{typename}, {len(records)}> {group} = {{{{",
        ]
        for i, p in enumerate(records):
            if group == "bytes":
                fields = f"{p['address']:#x}, payload_{i}, {boolean(p['code'])}"
            elif group == "pointers":
                fields = f"{p['address']:#x}, {p['target']:#x}, {boolean(p['code'])}"
            else:
                fields = (
                    f"{p['displacement']:#x}, {p['next_instruction']:#x}, "
                    f"{p['target']:#x}, {boolean(p['code'])}"
                )
            lines.append("    {" + fields + "},")
        lines += [
            "}};",
            "/// @brief Diagnostic identifiers corresponding one-to-one to descriptors.",
            f"inline constexpr std::array<const char*, {len(records)}> {group}_ids = {{{{",
        ]
        lines += ["    " + json.dumps(p["id"]) + "," for p in records]
        lines += ["}};"]
    lines += ["}  // namespace capie::core::macos::profile", ""]
    return "\n".join(lines)


def main() -> None:
    """Verify the exact local binary and all source evidence before writing a generated header.

    Raises:
        ValueError: File identity or evidence fails validation.
    """
    root = Path(__file__).resolve().parents[3]
    folder = Path(__file__).resolve().parent.parent
    macho = importlib.import_module("macho")
    if hashlib.sha256(macho.DATA).hexdigest() != DIGEST:
        msg = "Exact binary SHA mismatch"
        raise ValueError(msg)
    sources = {
        f"{name}-pins.json": json.loads((folder / f"{name}-pins.json").read_text())
        for name in ("editor", "layers", "core", "host")
    }
    aggregate = json.loads((folder / "re/port-pin-evidence.json").read_text())
    if aggregate["sha256"] != DIGEST:
        msg = "Aggregate SHA mismatch"
        raise ValueError(msg)

    def executable(address: int, size: int) -> bool:
        return any(
            vm <= address and address + size <= vm + length
            for (segment, section), (vm, length, _) in macho.SECTS.items()
            if (segment, section) == ("__TEXT", "__text")
        )

    exporter = collect(macho.DATA, macho.fo, executable, sources, aggregate["pins"])
    counts = (
        sum(p["role"] == "bytes" for p in exporter.typed),
        sum(p["role"] == "pointer" for p in exporter.typed),
        len(exporter.relatives),
    )
    if counts != (203, 15, 22):
        msg = f"Unexpected descriptor counts: {counts}"
        raise ValueError(msg)
    target = root / "plugin/capie/src/core/macos_profile.generated.hpp"
    target.write_text(render(exporter))
    print(
        f"Generated {target}: 203 BytesPin + 15 PointerPin + 22 RelativePin; "
        "all 218 ranges reconciled"
    )


if __name__ == "__main__":
    main()
