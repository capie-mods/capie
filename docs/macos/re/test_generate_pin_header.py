"""Typed exporter refusals and descriptor semantics, without game execution."""

# ruff: noqa: PT009, PT027
import unittest

from build_pin_evidence import BASE, DIGEST
from generate_pin_header import TypedExporter, collect


def subject() -> TypedExporter:
    """Create source bytes containing a vtable pointer and relative displacement.

    Returns:
        A typed exporter with bounded synthetic memory.
    """
    data = bytearray(128)
    data[8:12] = (4).to_bytes(4, "little", signed=True)
    data[48:56] = (BASE + 64).to_bytes(8, "little")
    return TypedExporter(bytes(data), lambda va: va - BASE, lambda va, _size: va >= BASE + 64)


class TypedTests(unittest.TestCase):
    """Require typed evidence instead of classifying identifiers by spelling."""

    def test_pointer_is_slid_descriptor(self) -> None:
        """Pointer role follows typed fields even without a pointer-looking identifier."""
        e = subject()
        e.record(
            "arbitrary",
            {"id": "arbitrary", "rva": 48, "target_rva": 64, "disk_unslid_qword": BASE + 64},
        )
        self.assertEqual(e.typed[0]["role"], "pointer")
        self.assertEqual(e.typed[0]["target"], BASE + 64)
        self.assertEqual(e.typed[0]["address"], BASE + 48)
        self.assertTrue(e.typed[0]["code"])

    def test_jump_table_is_data(self) -> None:
        """Relative jump-table bytes are data even inside executable storage."""
        e = subject()
        e.record(
            "table",
            {
                "id": "table",
                "kind": "relative_jump_table_entry",
                "rva": 64,
                "hex": "00000000",
                "base_rva": 64,
                "target_rva": 64,
            },
        )
        self.assertEqual(e.typed[0]["role"], "bytes")
        self.assertFalse(e.typed[0]["code"])

    def test_unknown_and_malformed(self) -> None:
        """Unknown roles and malformed bytes never become unchecked descriptors."""
        for pin in [
            {"id": "x", "kind": "future_kind", "rva": 64, "hex": "00"},
            {"id": "x", "rva": 64, "hex": "zz"},
            {"id": "x", "rva": 64, "hex": ""},
        ]:
            with self.assertRaises(ValueError):
                subject().record("x", pin)

    def test_relative_bounds(self) -> None:
        """Reuse explicit next-instruction validation and retain target role."""
        e = subject()
        e.relative({"id": "r", "displacement_rva": 8, "next_instruction_rva": 12, "target_rva": 16})
        self.assertFalse(e.relatives[0]["code"])
        with self.assertRaises(ValueError):
            e.relative(
                {"id": "bad", "displacement_rva": 8, "next_instruction_rva": 40, "target_rva": 44}
            )

    def test_reference_and_coverage_conflicts(self) -> None:
        """Missing references, duplicate IDs and an unmatched aggregate fail closed."""
        manifest = {"target_sha256": DIGEST, "entries": [{"id": "x", "rva": 64, "hex": "00"}]}
        e = subject()
        expected = [{"id": "sample/x", "file_offset": 64, "hex": "00", "va": hex(BASE + 64)}]
        data = e.data
        result = collect(
            data, lambda va: va - BASE, lambda _va, _size: True, {"sample.json": manifest}, expected
        )
        self.assertEqual(len(result.typed), 1)
        for records, ranges in [
            ([{"id": "x", "manifest_reference": {"file": "missing", "id": "x"}}], expected),
            (manifest["entries"] * 2, expected),
            (manifest["entries"], []),
        ]:
            with self.assertRaises(ValueError):
                collect(
                    data,
                    lambda va: va - BASE,
                    lambda _va, _size: True,
                    {"sample.json": {**manifest, "entries": records}},
                    ranges,
                )


if __name__ == "__main__":
    unittest.main()
