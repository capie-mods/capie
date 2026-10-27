"""Compare the CAPIE signature table with the accepted 1.19.4.0 table.

The accepted table is ``tools/port/accepted-1.19.4.0.json``, pinned by SHA-256 (26AB6BC6...).
It was generated from ``hoi4.exe`` 1.19.4.0 (SHA-256 B6650C85...) by
``tools/port/gen_tables.py`` during the port documented in
``docs/research/port-1.19.4.0.md``: whole instructions, the same RIP/rel32 fields masked as in
the 1.19.3.0 table, unique in ``.text`` and free of DIR64 relocations. Every entry's id, RVA,
length, bytes and wildcard positions are compared with
``plugin/capie/src/core/signature_table.hpp`` and ``game_layout.hpp``, and the named slot pins,
tokens, RIP pin and hook RVAs with ``game_layout.hpp``. Any difference fails.
``signature_exe_test.exe`` checks the same table against the executable itself.

Up to CAPIE 1.5.0 (HOI4 1.19.3.0) this script compared the table with the runtime-accepted
prototype sources (``plugin/src/country.cpp`` and the caption refresh candidate); that version
is in the git history.

Usage::

    python -I check_signature_table.py
"""

import hashlib
import json
import pathlib
import re
import sys

HERE = pathlib.Path(__file__).resolve().parent
"""Folder of this script (``plugin/capie/tests``)."""

REPO = HERE.parents[2]
"""Repository root."""

ACCEPTED = "tools/port/accepted-1.19.4.0.json"
"""Accepted table, repository-relative."""

ACCEPTED_SHA256 = "26ab6bc6ecb3f5bdd049fce9721c44fbac5ffc932e1f032fcfb66207e9c96655"
"""Pinned SHA-256 of the accepted table."""

GAME_EXE_SHA256 = "b6650c85002374218e60598bd04aaa26cbad996fedce77126fcf92fc6bd6d888"
"""The executable the accepted table was generated from (must match game_build.hpp)."""

TABLE = REPO / "plugin/capie/src/core/signature_table.hpp"
"""The CAPIE signature table."""

LAYOUT = REPO / "plugin/capie/src/core/game_layout.hpp"
"""The CAPIE layout constants."""

BUILD = REPO / "plugin/capie/src/core/game_build.hpp"
"""The pinned game build."""

TABLE_SIZE = 47
"""Expected number of entries in the table (3 + 11 + 25 + 8)."""


class Results:
    """Count checks and failures and print each failure."""

    def __init__(self) -> None:
        """Start with no checks."""
        self.checks = 0
        self.failures = 0

    def check(self, label: str, *, ok: bool) -> None:
        """Record one check.

        Args:
            label: Description printed on failure.
            ok: Result of the check.
        """
        self.checks += 1
        if not ok:
            self.failures += 1
            print("FAIL", label)


def accepted(results: Results) -> dict:
    """Return the accepted table after verifying its SHA-256.

    Args:
        results: Check recorder.

    Returns:
        Parsed accepted table.
    """
    path = REPO / ACCEPTED
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    results.check(f"{ACCEPTED} is the accepted version ({digest[:8]})", ok=digest == ACCEPTED_SHA256)
    return json.loads(path.read_text(encoding="utf-8"))


def layout_values() -> dict[str, int]:
    """Return every numeric constant of game_layout.hpp keyed by ``namespace::name``.

    Returns:
        Mapping of qualified constant name to integer value (aliases resolved).
    """
    values: dict[str, int] = {}
    namespace = ""
    for line in LAYOUT.read_text(encoding="utf-8").splitlines():
        found = re.match(r"namespace (\w+) \{", line)
        if found:
            namespace = found.group(1)
        if line.startswith("}"):
            namespace = ""
        constant = re.search(r"inline constexpr [\w:]+ (\w+) = (0x[0-9A-Fa-f]+|\d+);", line)
        if constant:
            values[f"{namespace}::{constant.group(1)}"] = int(constant.group(2), 0)
        alias = re.search(r"inline constexpr [\w:]+ (\w+) = ([a-z_]+);", line)
        if alias and f"{namespace}::{alias.group(2)}" in values:
            values[f"{namespace}::{alias.group(1)}"] = values[f"{namespace}::{alias.group(2)}"]
    return values


def table_entries(layout: dict[str, int]) -> dict[str, tuple[int, str]]:
    """Return the signature entries keyed by id.

    Args:
        layout: Constants of game_layout.hpp.

    Returns:
        Mapping of id to (RVA, lower-case pattern with ``??`` wildcards).
    """
    text = TABLE.read_text(encoding="utf-8")
    pattern = r'\{"([\w.]+)", SignatureGroup::\w+, (rva::\w+|0x[0-9A-Fa-f]+),\s*"([0-9a-fA-F?]+)"'
    entries: dict[str, tuple[int, str]] = {}
    for identifier, rva, hexbytes in (match.groups() for match in re.finditer(pattern, text)):
        value = layout[rva] if rva.startswith("rva::") else int(rva, 16)
        entries[identifier] = (value, hexbytes.lower())
    return entries


def main() -> int:
    """Run the comparison.

    Returns:
        Process exit code (0 when equal).
    """
    results = Results()
    reference = accepted(results)
    layout = layout_values()
    entries = table_entries(layout)
    results.check(f"{TABLE_SIZE} entries in the table (found {len(entries)})", ok=len(entries) == TABLE_SIZE)
    expected = reference["entries"]
    results.check(f"{TABLE_SIZE} accepted entries (found {len(expected)})", ok=len(expected) == TABLE_SIZE)
    for identifier, want in expected.items():
        entry = entries.get(identifier)
        ok = entry is not None and entry[0] == int(want["rva"], 16) and entry[1] == want["hex"]
        results.check(f"{identifier} rva/bytes/length/mask", ok=ok)
    for identifier in entries.keys() - expected.keys():
        results.check(f"{identifier} is not in the accepted table", ok=False)
    for name, value in reference["constants"].items():
        results.check(f"{name} == {value}", ok=layout.get(name) == int(value, 16))
    results.check("accepted table names the pinned executable", ok=reference["game_exe_sha256"] == GAME_EXE_SHA256)
    build = BUILD.read_text(encoding="utf-8")
    results.check("game_build.hpp pins the same executable", ok=f'"{GAME_EXE_SHA256}"' in build)
    print(f"signature table equivalence: {results.checks} checks, {results.failures} failures")
    return 1 if results.failures else 0


if __name__ == "__main__":
    sys.exit(main())
