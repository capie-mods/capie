"""Offline negative tests for the evidence checker; never opens or runs the game."""

# stdlib unittest keeps these checks independent of the project environment.
# ruff: noqa: PT009
import hashlib
import unittest

from verify_evidence import verify


class EvidenceTest(unittest.TestCase):
    """Exercise positive evidence and fail-closed rejection paths."""

    def setUp(self) -> None:
        """Prepare a fresh synthetic image."""
        self.data = bytes.fromhex("554889e5c390")
        self.manifest = {
            "sha256": hashlib.sha256(self.data).hexdigest(),
            "pins": [{"id": "entry", "file_offset": 0, "hex": "554889e5"}],
        }

    def test_matching_evidence(self) -> None:
        """Check matching evidence."""
        self.assertEqual(verify(self.data, self.manifest), [])

    def test_unknown_build(self) -> None:
        """Check unknown build."""
        self.assertEqual(verify(self.data + b"x", self.manifest), ["sha256 mismatch"])

    def test_wrong_pin_even_on_matching_build(self) -> None:
        """Check wrong pin even on matching build."""
        self.manifest["pins"][0]["hex"] = "554888e5"
        self.assertEqual(verify(self.data, self.manifest), ["entry: bytes mismatch"])

    def test_empty_manifest_is_not_success(self) -> None:
        """Check empty manifest is not success."""
        self.manifest["pins"] = []
        self.assertEqual(verify(self.data, self.manifest), ["no evidence pins"])

    def test_invalid_pin_is_rejected(self) -> None:
        """Check invalid pin is rejected."""
        for offset, value in [(-1, "55"), (6, "55"), (5, "9090"), (0, ""), (0, "zz")]:
            with self.subTest(offset=offset, value=value):
                self.manifest["pins"][0].update(file_offset=offset, hex=value)
                self.assertTrue(verify(self.data, self.manifest))

    def test_duplicate_id_is_rejected(self) -> None:
        """Check duplicate id is rejected."""
        self.manifest["pins"] *= 2
        self.assertEqual(verify(self.data, self.manifest), ["entry: duplicate id"])


if __name__ == "__main__":
    unittest.main()
