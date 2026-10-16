"""Deterministic native-launcher component packaging, deliberately outside publication.

This is not the complete macOS CAPIE asset. It carries no runtime dylib or supported
installer and cannot replace the required macOS asset in a joint Windows/macOS release.
"""

from __future__ import annotations

import json
import struct
import zipfile
from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from pathlib import Path

from releasekit.package import ZIP_TIMESTAMP, BundleEntry, entry_problems, sha256_bytes
from releasekit.versions import capie_problems, read_capie_versions

MACHO_HEADER_BYTES = 32
"""Size of the fixed mach_header_64 prefix checked before packaging."""

EXECUTABLES = ("capie-parent", "capie-child")
"""Only native launcher binaries are accepted; diagnostic libraries are never substituted."""
MACHO_X86_64_EXECUTABLE = (0xFEEDFACF, 0x01000007, 2)
"""Mach-O magic, x86_64 CPU type and MH_EXECUTE file type."""
INSTRUCTIONS = b"""# macOS launcher component only

This archive is not a CAPIE runtime or supported installation package.
It does not provide API 7 and must not be copied into the HOI4 app bundle.
It contains a native parent and child for controlled developer testing only.
No Python interpreter is needed to execute these binaries.

Parent arguments: GAME CHILD DYLIB SHA256 NEW_STAGING_DIRECTORY
The parent stages settings outside game/user data and execs original dowser.
The child preserves argv and checks final playset/game/library identity before exec.
A validated runtime dylib, trusted manifest, installation ownership, concurrency
acceptance and in-game API/feature tests are still required before distribution.

Windows uses its existing win64 asset and install.ps1. The completed macOS runtime
must share the same release version/tag, with separate platform instructions.
"""
"""Instructions embedded in the component so it cannot masquerade as an installer."""


def package_component(build: Path, header: Path, destination: Path) -> Path:
    """Package launcher binaries under the shared release version, retaining Unix modes.

    Returns:
        New ZIP path; its manifest explicitly states runtime_ready=false.

    Raises:
        ValueError: If versions or executable inputs fail validation.
    """
    versions = read_capie_versions(header.read_text(encoding="utf-8"))
    problems = capie_problems(versions)
    if problems:
        raise ValueError("; ".join(problems))
    entries = []
    for name in EXECUTABLES:
        source = build / name
        if source.is_symlink() or not source.is_file():
            message = f"Missing regular native executable: {name}"
            raise ValueError(message)
        data = source.read_bytes()
        if len(data) < MACHO_HEADER_BYTES:
            message = f"Truncated Mach-O executable: {name}"
            raise ValueError(message)
        fields = struct.unpack_from("<8I", data)
        if (fields[0], fields[1], fields[3]) != MACHO_X86_64_EXECUTABLE:
            message = f"Expected thin x86_64 Mach-O executable: {name}"
            raise ValueError(message)
        entries.append(BundleEntry(f"bin/{name}", data))
    manifest = {
        "schema_version": 1,
        "kind": "native-launcher-component",
        "platform": "macos-x86_64",
        "version": versions.release,
        "target_api_level": versions.api_level,
        "provided_api_level": None,
        "runtime_ready": False,
        "signed": False,
        "files": {
            entry.name: {"sha256": entry.sha256, "size": len(entry.data)} for entry in entries
        },
    }
    entries.extend(
        [
            BundleEntry(
                "launcher-component.json",
                (json.dumps(manifest, sort_keys=True, indent=2) + "\n").encode(),
            ),
            BundleEntry("INSTALL-macos.md", INSTRUCTIONS),
        ]
    )
    problems = entry_problems(entries, "bin/capie-parent")
    if problems:
        raise ValueError("; ".join(problems))
    destination.mkdir(mode=0o700)
    path = destination / f"CAPIE-{versions.release}-macos-x86_64-launcher-component.zip"
    with zipfile.ZipFile(path, "x", compression=zipfile.ZIP_DEFLATED) as archive:
        for entry in sorted(entries, key=lambda value: value.name):
            info = zipfile.ZipInfo(entry.name, date_time=ZIP_TIMESTAMP)
            info.create_system = 3
            info.compress_type = zipfile.ZIP_DEFLATED
            mode = 0o100755 if entry.name.startswith("bin/") else 0o100644
            info.external_attr = mode << 16
            archive.writestr(info, entry.data)
    (destination / "SHA256SUMS.txt").write_text(
        f"{sha256_bytes(path.read_bytes())}  {path.name}\n", encoding="utf-8"
    )
    return path
