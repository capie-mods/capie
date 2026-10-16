"""Offline packaging checks; synthetic Mach-O headers are not runtime implementations."""

# Standard-library unittest is used for this standalone packaging suite.
# ruff: noqa: PT009, PT027
import json
import struct
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from releasekit.macos_component import package_component


class ComponentTests(unittest.TestCase):
    """Verify version coupling, honest status, archive modes and strict inputs."""

    def test_package_has_shared_version_and_no_runtime_claim(self) -> None:
        """Equal input bytes produce equal ZIP bytes with executable launcher modes."""
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            build = root / "build"
            build.mkdir()
            binary = struct.pack("<8I", 0xFEEDFACF, 0x01000007, 3, 2, 0, 0, 0, 0)
            for name in ("capie-parent", "capie-child"):
                (build / name).write_bytes(binary)
            header = Path(__file__).resolve().parents[3] / "plugin/capie/src/core/game_build.hpp"
            first = package_component(build, header, root / "first")
            second = package_component(build, header, root / "second")
            self.assertEqual(first.read_bytes(), second.read_bytes())
            with zipfile.ZipFile(first) as archive:
                self.assertEqual(
                    set(archive.namelist()),
                    {
                        "bin/capie-parent",
                        "bin/capie-child",
                        "launcher-component.json",
                        "INSTALL-macos.md",
                    },
                )
                manifest = json.loads(archive.read("launcher-component.json"))
                self.assertFalse(manifest["runtime_ready"])
                self.assertIsNone(manifest["provided_api_level"])
                self.assertEqual(manifest["version"], first.name.split("-")[1])
                for name in ("bin/capie-parent", "bin/capie-child"):
                    self.assertEqual((archive.getinfo(name).external_attr >> 16) & 0o777, 0o755)
                self.assertEqual(manifest["files"]["bin/capie-parent"]["size"], len(binary))

    def test_missing_or_wrong_architecture_refused_before_output(self) -> None:
        """No partial component is emitted for absent or foreign executables."""
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            build = root / "build"
            build.mkdir()
            header = Path(__file__).resolve().parents[3] / "plugin/capie/src/core/game_build.hpp"
            for binary in (
                None,
                b"not executable",
                struct.pack("<8I", 0xFEEDFACF, 0x0100000C, 0, 2, 0, 0, 0, 0),
            ):
                if binary is not None:
                    for name in ("capie-parent", "capie-child"):
                        (build / name).write_bytes(binary)
                with self.assertRaises(ValueError):
                    package_component(build, header, root / "out")
                self.assertFalse((root / "out").exists())

    def test_symlink_and_existing_output_are_rejected(self) -> None:
        """Reject indirect inputs and never replace an existing output directory."""
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            build = root / "build"
            build.mkdir()
            binary = struct.pack("<8I", 0xFEEDFACF, 0x01000007, 3, 2, 0, 0, 0, 0)
            real = build / "capie-parent"
            real.write_bytes(binary)
            child = build / "capie-child"
            child.symlink_to(real)
            header = Path(__file__).resolve().parents[3] / "plugin/capie/src/core/game_build.hpp"
            with self.assertRaises(ValueError):
                package_component(build, header, root / "out")
            self.assertFalse((root / "out").exists())
            child.unlink()  # Only this test's own temporary symlink.
            child.write_bytes(binary)
            output = root / "out"
            output.mkdir()
            marker = output / "keep"
            marker.write_bytes(b"untouched")
            with self.assertRaises(FileExistsError):
                package_component(build, header, output)
            self.assertEqual(marker.read_bytes(), b"untouched")


if __name__ == "__main__":
    unittest.main()
