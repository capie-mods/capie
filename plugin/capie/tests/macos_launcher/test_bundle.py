"""Developer bundle trust-boundary checks; no game or compiler is executed."""

# Standard unittest keeps these tests independent of project dependencies.
# ruff: noqa: PT009, PT027
import importlib.util
import json
import struct
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
"""Repository containing the developer build workflow."""
sys.path.insert(0, str(ROOT / "tools"))
SPEC = importlib.util.spec_from_file_location("bundle", ROOT / "tools/build-macos-bundle.py")
"""Explicit local module under test."""
bundle = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(bundle)


class BundleTests(unittest.TestCase):
    """Reject unsupported paths, ordering and stale lifecycle authorization."""

    def test_inert_policy_is_default(self) -> None:
        """No audit means no enabled policy even when valid library paths are available."""
        paths = [Path("/private/tmp/bootstrap.dylib"), Path("/private/tmp/runtime.dylib")]
        self.assertEqual(
            bundle.policy(paths, ["", "a" * 64], ready=False),
            "#define CAPIE_BOOTSTRAP_POLICY_READY 0\n",
        )
        self.assertEqual(bundle.reviewed(None, ROOT, []), (False, []))

    def test_invalid_paths_and_ambiguous_order(self) -> None:
        """Reject relative/control paths and duplicate owned libraries."""
        for value in ("relative", "/private/tmp/a:b", "/private/tmp/a\nb"):
            with self.subTest(path=value), self.assertRaises(ValueError):
                bundle.canonical(Path(value), exists=False)
        with self.assertRaises(ValueError):
            bundle.policy([Path("/same"), Path("/same")], ["", "a" * 64], ready=True)

    def test_symlink_delimiter(self) -> None:
        """Validate resolved paths, including a safe-looking alias to a colon target."""
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            target = root / "bad:name"
            target.write_text("fixture")
            alias = root / "safe"
            alias.symlink_to(target)
            with self.assertRaises(ValueError):
                bundle.canonical(alias)

    def test_bootstrap_structure(self) -> None:
        """Admit only thin x86_64 dylibs with libSystem and one initializer pointer."""
        name = b"/usr/lib/libSystem.B.dylib\0"
        library = struct.pack("<6I", 12, 56, 24, 0, 0, 0) + name
        library += bytes(56 - len(library))
        section = struct.pack(
            "<16s16sQQIIIIIIII", b"__mod_init_func", b"__DATA", 0, 8, 0, 0, 0, 0, 9, 0, 0, 0
        )
        segment = struct.pack("<II16sQQQQIIII", 25, 152, b"__DATA", 0, 8, 0, 8, 3, 3, 1, 0)
        commands = library + segment + section
        header = struct.pack("<8I", 0xFEEDFACF, 0x1000007, 3, 6, 2, len(commands), 0, 0)
        good = header + commands
        bundle.verify_bootstrap_bytes(good)
        with self.assertRaises(ValueError):
            bundle.verify_bootstrap_bytes(good, ready=False)
        inert = struct.pack("<8I", 0xFEEDFACF, 0x1000007, 3, 6, 1, len(library), 0, 0) + library
        bundle.verify_bootstrap_bytes(inert, ready=False)
        with self.assertRaises(ValueError):
            bundle.verify_bootstrap_bytes(inert)
        for malformed in (
            good[:20],
            good[:-1],
            good.replace(name, b"X" + name[1:]),
            good.replace(section, section[:40] + struct.pack("<Q", 16) + section[48:]),
        ):
            with self.subTest(size=len(malformed)), self.assertRaises(ValueError):
                bundle.verify_bootstrap_bytes(malformed)

    def test_stale_sources_and_dependency_mutation(self) -> None:
        """A matching audit is invalidated by either a source or dependency change."""
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            sources = root / "plugin/capie/src"
            (sources / "bootstrap").mkdir(parents=True)
            (sources / "dll").mkdir()
            bootstrap = sources / "bootstrap/macos_bootstrap.cpp"
            runtime = sources / "dll/macos_runtime.cpp"
            bootstrap.write_text("bootstrap")
            runtime.write_text("runtime")
            (root / "tools").mkdir()
            for name in (
                "build-macos-runtime.sh",
                "build-macos-launcher.sh",
                "build-macos-bundle.py",
            ):
                (root / "tools" / name).write_text(name)
            header = sources / "identity.hpp"
            header.write_text("original header")
            dependencies = []
            for name in ("libsteam_api.dylib", "libPDXSDK.dylib", "libtbb.dylib"):
                path = root / name
                path.write_text(name)
                dependencies.append({"path": str(path), "sha256": bundle.digest(path)})
            record = {
                "target_sha256": bundle.TARGET_SHA,
                "lifecycle_ready": True,
                "bootstrap_source_sha256": bundle.digest(bootstrap),
                "runtime_source_sha256": bundle.digest(runtime),
                "source_closure_sha256": bundle.source_fingerprint(root),
                "external_libraries": [],
                "dependencies": dependencies,
            }
            evidence = root / "audit.json"
            evidence.write_text(json.dumps(record))
            self.assertEqual(bundle.reviewed(evidence, root, []), (True, dependencies))
            header.write_text("changed header")
            with self.assertRaises(ValueError):
                bundle.reviewed(evidence, root, [])
            header.write_text("original header")
            runtime.write_text("changed source")
            with self.assertRaises(ValueError):
                bundle.reviewed(evidence, root, [])
            runtime.write_text("runtime")
            Path(dependencies[0]["path"]).write_text("changed dependency")
            with self.assertRaises(ValueError):
                bundle.reviewed(evidence, root, [])


if __name__ == "__main__":
    unittest.main()
