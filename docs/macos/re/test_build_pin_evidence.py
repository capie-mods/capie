"""Negative tests for evidence targets, references and fail-closed record coverage."""

# Keep the existing evidence suite runnable with stdlib unittest and no project environment.
# These two framework-style rules conflict with that deliberate test-runner choice.
# ruff: noqa: PT009, PT027
import unittest

from build_pin_evidence import (
    BASE,
    DIGEST,
    Exporter,
    check_dependencies,
    index_manifest,
    resolve_reference,
)


def exporter() -> Exporter:
    """Return a small mapped image with a rel32, JMP and pointer fixture."""
    data = bytearray(128)
    data[8:12] = (4).to_bytes(4, "little", signed=True)
    data[31] = 0xE9
    data[32:36] = (28).to_bytes(4, "little", signed=True)
    data[48:56] = (BASE + 64).to_bytes(8, "little")
    return Exporter(bytes(data), lambda va: va - BASE)


class EvidenceTests(unittest.TestCase):
    """Reject corruption that raw-byte-only comparison would not notice."""

    def test_relative_target_and_instruction_end(self) -> None:
        """Check signed displacement and explicit RIP end independently."""
        pin = {"id": "rel", "displacement_rva": 8, "next_instruction_rva": 12, "target_rva": 16}
        exporter().relative(pin)
        for changed in (
            {"target_rva": 17},
            {"next_instruction_rva": 11},
            {"next_instruction_rva": 40},
        ):
            with self.subTest(changed=changed), self.assertRaises(ValueError):
                exporter().relative({**pin, **changed})

    def test_negative_relative_and_trailing_immediate(self) -> None:
        """Support negative rel32 and instruction bytes after its displacement."""
        data = bytearray(32)
        data[8:12] = (-9).to_bytes(4, "little", signed=True)
        subject = Exporter(bytes(data), lambda va: va - BASE)
        subject.relative(
            {"id": "rip", "displacement_rva": 8, "next_instruction_rva": 13, "target_rva": 4}
        )

    def test_pointer_representations(self) -> None:
        """Reject contradictory qword, encoded bytes and slot addresses."""
        pin = {
            "rva": 48,
            "target_rva": 64,
            "disk_unslid_qword": BASE + 64,
            "vtable_rva": 40,
            "slot": 8,
        }
        subject = exporter()
        subject.pointer("pointer", pin)
        self.assertEqual(len(subject.pins), 1)
        for changed in ({"disk_unslid_qword": BASE + 65}, {"file_hex": "00" * 8}, {"slot": 16}):
            with self.subTest(changed=changed), self.assertRaises(ValueError):
                exporter().pointer("pointer", {**pin, **changed})

    def test_compound_jump(self) -> None:
        """Require the actual E9 opcode and its recorded destination."""
        pin = {
            "id": "ctor",
            "rva": 31,
            "compound_checks": {
                "fixed_rva": 31,
                "jump_instruction_rva": 31,
                "relative_target_rva": 64,
            },
        }
        exporter().compound(pin)
        for changed in (
            {"fixed_rva": 32},
            {"jump_instruction_rva": 30},
            {"relative_target_rva": 65},
        ):
            with self.subTest(changed=changed), self.assertRaises(ValueError):
                exporter().compound(
                    {**pin, "compound_checks": {**pin["compound_checks"], **changed}}
                )

    def test_reference_resolution(self) -> None:
        """Resolve valid external evidence and reject missing files or IDs."""
        indexes = {
            "a": {"pin": {"id": "pin", "manifest_reference": {"file": "b", "id": "pin"}}},
            "b": {"pin": {"id": "pin", "hex": "00", "rva": 0}},
        }
        check_dependencies(indexes, "a", indexes["a"]["pin"])
        self.assertEqual(resolve_reference(indexes, "a", "pin"), indexes["b"]["pin"])
        for source, name in (("missing", "pin"), ("b", "missing")):
            with self.subTest(source=source, name=name), self.assertRaises(ValueError):
                resolve_reference(indexes, source, name)
        indexes["b"]["pin"] = {"id": "pin", "manifest_reference": {"file": "a", "id": "pin"}}
        with self.assertRaises(ValueError):
            resolve_reference(indexes, "a", "pin")

    def test_reference_cannot_substitute_role(self) -> None:
        """A valid but unrelated record cannot silently replace an editor pin."""
        indexes = {
            "a": {"pin": {"id": "pin", "manifest_reference": {"file": "b", "id": "other"}}},
            "b": {"other": {"id": "other", "hex": "00", "rva": 0}},
        }
        with self.assertRaises(ValueError):
            check_dependencies(indexes, "a", indexes["a"]["pin"])

    def test_both_companion_spellings(self) -> None:
        """Neither companion key may bypass existence checking."""
        for field in ("requires_companion", "required_identity_companions"):
            pin = {"id": "pin", field: ["missing"]}
            with self.subTest(field=field), self.assertRaises(ValueError):
                check_dependencies({"a": {"pin": pin}}, "a", pin)

    def test_companion_destination(self) -> None:
        """The named constructor companion must occupy the recorded jump target."""
        pin = {
            "id": "ctor",
            "compound_checks": {"required_companion_id": "body", "relative_target_rva": 64},
        }
        indexes = {"a": {"ctor": pin, "body": {"id": "body", "rva": 65}}}
        with self.assertRaises(ValueError):
            check_dependencies(indexes, "a", pin)

    def test_raw_ranges_and_omitted_records(self) -> None:
        """Reject truncation, negative offsets, mismatched bytes and unknown records."""
        for rva, size in ((127, 2), (-1, 1), (0, 0)):
            with self.subTest(rva=rva, size=size), self.assertRaises(ValueError):
                exporter().raw(rva, size)
        with self.assertRaises(ValueError):
            exporter().add("bad", 0, "ff")
        with self.assertRaises(ValueError):
            exporter().record("unknown", {"id": "unknown", "kind": "future_kind"})

    def test_runtime_token_exclusion(self) -> None:
        """Expected initialized values must never become file-byte checks."""
        subject = exporter()
        subject.record(
            "token", {"id": "token", "kind": "runtime_u32", "rva": 10000, "hex": "b23a0000"}
        )
        self.assertEqual(subject.pins, [])

    def test_jump_table_target(self) -> None:
        """Compare signed jump-table entry with its table base, not RIP."""
        pin = {
            "id": "case",
            "kind": "relative_jump_table_entry",
            "rva": 8,
            "hex": "04000000",
            "base_rva": 12,
            "target_rva": 16,
        }
        exporter().record("case", pin)
        with self.assertRaises(ValueError):
            exporter().record("case", {**pin, "target_rva": 17})

    def test_duplicate_source_ids_and_digest(self) -> None:
        """No dictionary overwrite may hide a source pin."""
        pin = {"id": "same"}
        with self.assertRaises(ValueError):
            index_manifest({"sha256": DIGEST, "entries": [pin, pin]})
        with self.assertRaises(ValueError):
            index_manifest({"sha256": "0" * 64, "entries": [pin]})


if __name__ == "__main__":
    unittest.main()
