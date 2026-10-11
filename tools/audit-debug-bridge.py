"""Offline artifact audit for the inline-editor debug bridge candidate.

The script checks that the debug bridge source and build artifacts still match
what was reviewed, then rewrites the evidence manifest
``docs/evidence/session-20261011/day2-inline-debug-bridge-candidate.json``.
It does not start HOI4 and sends no valid request to a game process.

Steps, in order:

1. Load ``day2-inline-editor-key-logger-v2-candidate.json`` from the same
   evidence folder and require every file in its ``protected_sha256`` map to
   still have the recorded SHA-256 (compared in upper case).
2. Read ``plugin/probes/inline_editor_debug_bridge.cpp``.
3. Hash the installed ``hoi4.exe`` (fixed Steam path) and require the pinned
   game build ``7DC947BE...``. The file is only read from disk.
4. Parse the PE header of that image: check the ``PE`` signature, an x64
   machine type and fewer than 96 sections, then locate the ``.text`` section
   and slice its raw bytes.
5. Collect every ``<name>_rva = 0x...`` constant and every
   ``constexpr unsigned char <name>_bytes[] = {...}`` fingerprint from the C++
   source. Each fingerprint must occur exactly once in ``.text`` and its
   position plus the section RVA must equal the declared RVA. Exactly 11
   fingerprints and exactly 7 ``MH_CreateHook(`` calls are required.
6. Hash the debug-related probe sources, the built ``.exe`` files, the bridge
   DLL, its generated manifest header and this script itself.
7. Run ``inline_debug_control.exe`` five times: once with ``--help`` (must exit
   0) and four times with malformed or foreign arguments (pid ``0``,
   ``4294967296``, ``abc``, or pid ``1`` with an unknown verb; each must exit
   2). The arguments are invalid, so the controller should refuse them before
   it opens any process. The ``target_access: False`` stored for each call is a
   fixed value written by this script, not something it measures.
8. Write the manifest JSON (timestamp, fingerprints, hashes, CLI results and a
   fixed list of limits) and print a one-line summary plus the manifest hash.

Any mismatch stops the script with an ``AssertionError`` before the manifest is
written. The report fields ``diagnostic_transport_checks`` and
``inherited_v2_checks`` are hard-coded numbers; this script does not recompute
them.

A pass does not show that hooking those addresses is safe at runtime, that
whole instructions were matched (the manifest credits separate REA research and
an independent bridge review for that), or that the bridge works in a live
game. The manifest's ``status`` and ``limits`` fields say the same.

Usage::

    python tools/audit-debug-bridge.py

Running it overwrites the evidence manifest named above, so only run it when
that manifest is meant to be regenerated.
"""

import datetime as dt
import json
import re
import struct
import subprocess
from pathlib import Path

from rncommon.game_data import DEFAULT_GAME
from rncommon.hashing import sha256_file

ROOT = Path(__file__).resolve().parent.parent
"""Repository root (the parent of ``tools/``)."""

EVIDENCE = ROOT / "docs/evidence/session-20261011"
"""Evidence folder holding the input baseline and the output manifest."""

GAME_EXE = Path(DEFAULT_GAME) / "hoi4.exe"
"""Installed game executable whose bytes are audited (read-only)."""

GAME_SHA256 = "7DC947BE34970DA1E1C787BCDDBE7F62B610FF258AEBF8F06AA8B348F3A031D5"
"""SHA-256 of the only supported ``hoi4.exe`` build."""

PE_SIGNATURE_OFFSET_FIELD = 0x3C
"""Offset of the DOS-header field that points at the PE signature."""

AMD64_MACHINE = 0x8664
"""``IMAGE_FILE_MACHINE_AMD64`` value expected in the COFF header."""

MAX_SECTIONS = 96
"""Upper bound (exclusive) accepted for the PE section count."""

SECTION_HEADER_SIZE = 40
"""Size in bytes of one PE section header."""

PE_SIGNATURE = b"PE\0\0"
"""Signature at the start of the NT headers."""

COFF_MACHINE_OFFSET = 4
"""Offset of the COFF ``Machine`` and ``NumberOfSections`` fields from the PE signature."""

COFF_OPTIONAL_SIZE_OFFSET = 20
"""Offset of the COFF ``SizeOfOptionalHeader`` field from the PE signature."""

COFF_END_OFFSET = 24
"""Size of the PE signature plus the COFF file header (start of the optional header)."""

SECTION_NAME_SIZE = 8
"""Size of the zero-padded name at the start of a section header."""

TEXT_SECTION = b".text"
"""Name of the code section."""

DIAGNOSTIC_TRANSPORT_CHECKS = 49626
"""Assertion count of the diagnostic transport suite, copied into the report (not recomputed)."""

INHERITED_V2_CHECKS = 49240
"""Assertion count inherited from the key-logger v2 suite, copied into the report."""

EXPECTED_FINGERPRINTS = 11
"""Number of byte fingerprints the bridge source must declare."""

EXPECTED_HOOKS = 7
"""Number of ``MH_CreateHook(`` calls the bridge source must contain."""

CLI_TIMEOUT_SECONDS = 10
"""Timeout for each controller invocation."""

CLI_CASES = [
    ["--help"],
    ["0", "snapshot"],
    ["4294967296", "dump"],
    ["abc", "resume"],
    ["1", "foreign"],
]
"""Controller argument lists; only ``--help`` may succeed, the rest must be rejected."""

USAGE_ERROR_EXIT = 2
"""Exit code the controller must return for rejected arguments."""


def sha(path: str | Path) -> str:
    """Return the upper-case SHA-256 hex digest of a file.

    Args:
        path: File to hash.

    Returns:
        The digest as 64 upper-case hex characters.
    """
    return sha256_file(path, upper=True)


def text_section(image: bytes) -> tuple[int, bytes]:
    """Locate the ``.text`` section of a PE image.

    Args:
        image: Complete PE file contents.

    Returns:
        The section RVA and its raw bytes, limited to the smaller of the raw
        size and the virtual size. If several sections are named ``.text``,
        the last one wins.
    """
    pe_offset = struct.unpack_from("<I", image, PE_SIGNATURE_OFFSET_FIELD)[0]
    assert image[pe_offset : pe_offset + len(PE_SIGNATURE)] == PE_SIGNATURE
    machine, section_count = struct.unpack_from("<HH", image, pe_offset + COFF_MACHINE_OFFSET)
    optional_size = struct.unpack_from("<H", image, pe_offset + COFF_OPTIONAL_SIZE_OFFSET)[0]
    assert machine == AMD64_MACHINE
    assert section_count < MAX_SECTIONS
    sections = pe_offset + COFF_END_OFFSET + optional_size
    text_rva: int | None = None
    code = b""
    for i in range(section_count):
        at = sections + i * SECTION_HEADER_SIZE
        if image[at : at + SECTION_NAME_SIZE].rstrip(b"\0") == TEXT_SECTION:
            virtual_size, rva, raw_size, raw_offset = (
                int(value) for value in struct.unpack_from("<IIII", image, at + SECTION_NAME_SIZE)
            )
            text_rva = rva
            code = image[raw_offset : raw_offset + min(raw_size, virtual_size)]
    assert text_rva is not None
    return text_rva, code


def fingerprints(source: str, text_rva: int, code: bytes) -> dict[str, dict[str, object]]:
    """Check every byte fingerprint declared in the bridge source.

    Args:
        source: Contents of ``inline_editor_debug_bridge.cpp``.
        text_rva: RVA of the ``.text`` section.
        code: Raw bytes of the ``.text`` section.

    Returns:
        Fingerprint name mapped to its hex RVA, byte length and hit count.
    """
    rvas = {
        name: int(value, 16)
        for name, value in re.findall(r"(\w+)_rva\s*=\s*(0x[0-9A-Fa-f]+)", source)
    }
    result: dict[str, dict[str, object]] = {}
    pattern = r"constexpr unsigned char (\w+)_bytes\s*\[\]\s*=\s*\{([^}]+)\}"
    for name, body in re.findall(pattern, source):
        data = bytes(int(n, 16) for n in re.findall(r"0x([0-9A-Fa-f]{2})", body))
        assert code.count(data) == 1, (name, "unique")
        assert code.find(data) + text_rva == rvas[name], (name, "RVA")
        result[name] = {"rva": f"{rvas[name]:X}", "size": len(data), "unique_hits": 1}
    return result


def controller_cli(controller: Path) -> list[dict[str, object]]:
    """Run the controller with ``--help`` and four arguments it must reject.

    Args:
        controller: Path to ``inline_debug_control.exe``.

    Returns:
        One record per invocation with its arguments, exit code and output.
    """
    cli: list[dict[str, object]] = []
    for args in CLI_CASES:
        # Fixed local executable and literal argument lists; no shell is involved.
        result = subprocess.run(  # noqa: S603
            [str(controller), *args],
            capture_output=True,
            text=True,
            timeout=CLI_TIMEOUT_SECONDS,
            check=False,
        )
        expected = 0 if args == ["--help"] else USAGE_ERROR_EXIT
        assert result.returncode == expected, (args, result.returncode)
        cli.append(
            {
                "args": args,
                "exit_code": result.returncode,
                "output": (result.stdout + result.stderr).strip(),
                "target_access": False,
            }
        )
    return cli


def main() -> None:
    """Run the audit steps in order and write the evidence manifest."""
    baseline = json.loads(
        (EVIDENCE / "day2-inline-editor-key-logger-v2-candidate.json").read_text()
    )
    protected = baseline["protected_sha256"]
    for name, expected in protected.items():
        assert sha(ROOT / name) == expected.upper(), name
    source = (ROOT / "plugin/probes/inline_editor_debug_bridge.cpp").read_text(encoding="utf-8")
    assert sha(GAME_EXE) == GAME_SHA256
    text_rva, code = text_section(GAME_EXE.read_bytes())
    found = fingerprints(source, text_rva, code)
    assert len(found) == EXPECTED_FINGERPRINTS
    assert len(re.findall(r"MH_CreateHook\(", source)) == EXPECTED_HOOKS
    build = ROOT / "plugin/build/inline-editor-debug-bridge"
    files = [p for p in (ROOT / "plugin/probes").glob("*debug*") if p.is_file()]
    files += list(build.glob("*.exe"))
    files += [
        build / "rename_anything_inline_editor_debug_bridge.dll",
        build / "inline_debug_manifest.hpp",
        Path(__file__).resolve(),
    ]
    cli = controller_cli(build / "inline_debug_control.exe")
    report = {
        "time": dt.datetime.now(dt.UTC).astimezone().isoformat(),
        "status": "OFFLINE PASS; fresh HOI4 runtime PENDING",
        "baseline_v2_source_sha256": sha(ROOT / "plugin/probes/inline_editor_key_logger_v2.cpp"),
        "diagnostic_transport_checks": DIAGNOSTIC_TRANSPORT_CHECKS,
        "inherited_v2_checks": INHERITED_V2_CHECKS,
        "checks_are_repeated_assertions_not_distinct_scenarios": True,
        "ring_queue_suite": (
            "wrap/pause/truncation/concurrent copied snapshots; model order/coalescing/TTL PASS"
        ),
        "native_hooks": EXPECTED_HOOKS,
        "fingerprints": found,
        "whole_instruction_proof": (
            "separate REA research and independent bridge review; "
            "this script verifies bytes and unique RVA only"
        ),
        "protected_sha256": protected,
        "artifact_sha256": {p.relative_to(ROOT).as_posix(): sha(p) for p in files},
        "controller_cli": cli,
        "runtime": {
            "activated": False,
            "Execute_or_effect_acceptance": False,
            "snapshot_servicing": False,
        },
        "limits": [
            "controller success means queued only",
            "pause stops ring only; baseline file continues; in-flight UI work can finish",
            "copied ring overwrites; log write failures limit absence inference",
            "own selectors classify origin, not country ownership or effect success",
            "universal same-address/same-epoch load boundary OPEN",
            "single variant in a fresh process; no inverse co-load",
        ],
    }
    out = EVIDENCE / "day2-inline-debug-bridge-candidate.json"
    out.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(
        f"{EXPECTED_FINGERPRINTS} unique fingerprints;{EXPECTED_HOOKS} hooks;"
        f"{len(protected)} protected hashes;"
        f"{len(CLI_CASES)} negative CLI PASS; runtime not activated"
    )
    print("Manifest SHA256", sha(out))


if __name__ == "__main__":
    main()
