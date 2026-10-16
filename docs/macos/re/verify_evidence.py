"""Check recorded file bytes against one exact build, without loading executable code.

This verifies evidence integrity only. A successful result does not certify a
function's inferred meaning, hook safety, coverage, or runtime compatibility.
"""

import argparse
import hashlib
import json
from pathlib import Path


def verify(data: bytes, manifest: dict) -> list[str]:
    """Return failures for the pinned digest and every nonempty, bounded byte pin."""
    if hashlib.sha256(data).hexdigest() != manifest["sha256"]:
        return ["sha256 mismatch"]
    if not manifest["pins"]:
        return ["no evidence pins"]
    failures, seen = [], set()
    for pin in manifest["pins"]:
        name = pin["id"]
        if name in seen:
            failures.append(f"{name}: duplicate id")
            continue
        seen.add(name)
        try:
            expected = bytes.fromhex(pin["hex"])
        except ValueError:
            failures.append(f"{name}: invalid hex")
            continue
        offset = pin["file_offset"]
        if (
            not expected
            or not isinstance(offset, int)
            or offset < 0
            or offset + len(expected) > len(data)
        ):
            failures.append(f"{name}: invalid byte range")
        elif data[offset : offset + len(expected)] != expected:
            failures.append(f"{name}: bytes mismatch")
    return failures


def main() -> None:
    """Read an explicit binary and evidence manifest; report failure with exit 1."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument(
        "--manifest", type=Path, default=Path(__file__).with_name("localisation-evidence.json")
    )
    args = parser.parse_args()
    try:
        manifest = json.loads(args.manifest.read_text())
        failures = verify(args.binary.read_bytes(), manifest)
    except (OSError, ValueError, KeyError, TypeError) as exc:
        parser.exit(1, f"Evidence check failed: {exc}\n")
    if failures:
        parser.exit(1, "\n".join(failures) + "\n")
    print(
        f"Verified SHA-256 and {len(manifest['pins'])} evidence pins. "
        "Runtime compatibility is NOT established."
    )


if __name__ == "__main__":
    main()
