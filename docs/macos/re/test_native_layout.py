"""Check compiled numeric map against the independently recorded native inventory."""

# stdlib unittest keeps evidence checks independent of project dependencies.
# ruff: noqa: PT009
import json
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
"""Repository root containing both independent evidence and native source."""


class NativeLayoutTest(unittest.TestCase):
    """Prevent native constants drifting from the evidence inventory."""

    def test_inventory_values_and_adapter_exclusions(self) -> None:
        """Compare every numeric entry and exclude fields requiring ABI adapters."""
        inventory = json.loads((ROOT / "docs/macos/address-inventory.json").read_text())
        source = (ROOT / "plugin/capie/src/core/macos_game_layout.hpp").read_text()
        constants = {}
        namespace = None
        for line in source.splitlines():
            match = re.match(r"namespace capie::core::macos::(\w+) \{", line)
            if match:
                namespace = match[1]
            match = re.match(r"inline constexpr [\w: ]+ (\w+) = (0x[0-9a-fA-F]+|[0-9]+);", line)
            if match:
                key = f"core::{namespace}::{match[1]}"
                self.assertNotIn(key, constants)
                constants[key] = int(match[2], 0)
        mapped = 0
        for entry in inventory["constants"]:
            with self.subTest(constant=entry["id"]):
                if entry["status"] in ("proven", "static"):
                    self.assertEqual(constants.get(entry["id"]), int(entry["macos"]["value"], 0))
                    mapped += 1
                else:
                    self.assertNotIn(
                        entry["id"], constants, "adapter/policy cannot inherit a Windows constant"
                    )
        self.assertEqual(mapped, 157)


if __name__ == "__main__":
    unittest.main()
