"""Compare relocation-test prefixes with the manifest and optionally the exact game file."""

from __future__ import annotations

import hashlib
import json
import os
import re
import struct
from pathlib import Path

PREFIX_SIZE = 14
LC_SEGMENT_64 = 0x19


def file_segments(data: bytes | None) -> list[tuple[int, int, int]]:
    """Read file-backed segments only after the caller verified the exact file hash.

    Args:
        data: Pinned Mach-O file contents, or None when the optional file is absent.

    Returns:
        Tuples of unslid VA, file offset and file-backed size.
    """
    segments = []
    if data is not None:
        offset = 32
        for _ in range(struct.unpack_from("<I", data, 16)[0]):
            command, size = struct.unpack_from("<II", data, offset)
            if command == LC_SEGMENT_64:
                vm, _, file_offset, file_size = struct.unpack_from("<QQQQ", data, offset + 24)
                segments.append((vm, file_offset, file_size))
            offset += size
    return segments


def main() -> None:
    """Verify three full-instruction prefixes without loading or executing game code.

    Raises:
        SystemExit: If an explicitly requested file is missing or a comparison fails.
    """
    root = Path(__file__).resolve().parents[1]
    manifest = json.loads((root / "docs/macos/core-pins.json").read_text())
    source = (root / "plugin/capie/tests/macos_patch_test.cpp").read_text()
    default = (
        Path.home()
        / "Library/Application Support/Steam/steamapps/common/Hearts of Iron IV"
        / "hoi4.app/Contents/MacOS/hoi4"
    )
    binary = Path(os.environ.get("HOI4_MACOS_BINARY", str(default)))
    if "HOI4_MACOS_BINARY" in os.environ and not binary.is_file():
        message = "Explicit HOI4_MACOS_BINARY is missing"
        raise SystemExit(message)
    data = binary.read_bytes() if binary.is_file() else None
    if data is not None and hashlib.sha256(data).hexdigest() != manifest["target_sha256"]:
        message = "Game SHA-256 differs from the pinned manifest"
        raise SystemExit(message)
    segments = file_segments(data)
    for identifier, array in [
        ("country.lookup_n", "lookup_n"),
        ("country.lookup_z", "lookup_z"),
        ("country.exists", "exists_prefix"),
    ]:
        record = next(entry for entry in manifest["coverage"] if entry["id"] == identifier)
        expected = bytes.fromhex(record["hex"])[:PREFIX_SIZE]
        match = re.search(rf"const unsigned char {array}\[\] = \{{([^}}]+)\}}", source)
        actual = bytes(int(value.strip(), 16) for value in match[1].split(",")) if match else b""
        if len(expected) != PREFIX_SIZE or expected != actual:
            message = f"Fixture prefix differs from manifest: {identifier}"
            raise SystemExit(message)
        if data is not None:
            address = 0x100000000 + int(record["rva"], 16)
            region = next(
                (
                    data[fo + address - vm : fo + address - vm + PREFIX_SIZE]
                    for vm, fo, size in segments
                    if vm <= address and address + PREFIX_SIZE <= vm + size
                ),
                None,
            )
            if region != expected:
                message = f"Binary prefix differs from manifest: {identifier}"
                raise SystemExit(message)
        print(
            f"PASS: {identifier}: {expected.hex()} (fixture/manifest"
            + ("/binary)" if data is not None else ")")
        )
    if data is None:
        print("SKIP: game binary absent; only fixture/manifest comparison performed")


if __name__ == "__main__":
    main()
