#!/usr/bin/env python3
"""Fail on a game address in a C++ comment that is not a constant of the pinned build.

CAPIE's injected DLL code (``plugin/capie/src/{core,engine,features,dll}``) names engine
addresses through ``core/game_layout.hpp`` and the pin tables. A comment that spells an RVA
goes stale silently when the game updates: after the 1.19.3.0 to 1.19.4.0 port about 190
comments still named old addresses. The rule enforced here: a comment names functions and
symbols (``rva::update_names``, "UpdateNames"); a hex number of 5 to 7 digits may appear in a
comment only when it equals a value the code itself pins for the current build, that is a
hex literal in the code (not in a comment) of one of the address files:

- ``core/game_layout.hpp`` and ``core/signature_table.hpp``;
- the console and host layouts (``console_layout.hpp``, ``console_wiring.hpp``,
  ``host_probe.hpp``) and every ``*_pins.hpp``.

A candidate is a run of 5 to 7 hex digits (optionally ``0x``-prefixed) that is not part of a
longer word and contains at least one decimal digit, so words such as ``facade`` and hashes
of 8 digits and more are ignored, and so are Unicode code points (``U+10FFFF``) and the
quantities in ``NOT_ADDRESSES``. String literals are code, not comments, and are not checked.

Each finding is printed as ``path:line: 0xVALUE in a comment is not a pinned value``, followed
by a summary line. The exit code is 1 when something was found and 0 otherwise.

Usage::

    uv run python tools/lint/check_comment_addresses.py [FILE ...]
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
"""Repository root."""

SOURCE = ROOT / "plugin" / "capie" / "src"
"""CAPIE source folder."""

SCANNED = ("core", "engine", "features", "dll")
"""Sub-folders of ``SOURCE`` whose comments are checked (the injected DLL code)."""

SUFFIXES = frozenset({".hpp", ".cpp", ".h"})
"""File suffixes scanned."""

ADDRESS_FILES = (
    "core/game_layout.hpp",
    "core/signature_table.hpp",
    "features/console/console_layout.hpp",
    "features/console/console_wiring.hpp",
    "features/host/host_probe.hpp",
)
"""Files whose code literals define the allowed values (plus every ``*_pins.hpp``)."""

NOT_ADDRESSES = frozenset({"10000", "65536", "100000"})
"""Decimal numbers that comments use as quantities (scales, bucket counts), never addresses."""

CANDIDATE = re.compile(r"(?<![0-9A-Za-z_])(?:0[xX])?([0-9A-Fa-f]{5,7})(?![0-9A-Za-z_])")
"""A bare 5 to 7 digit hex number."""

LITERAL = re.compile(r"(?<![0-9A-Za-z_])0[xX]([0-9A-Fa-f]+)")
"""A hex literal in code."""


def split_source(text: str) -> tuple[str, list[tuple[int, str]]]:
    """Separate C++ code from its comments.

    Args:
        text: Source text.

    Returns:
        The code with comments and string literals blanked out, and every comment as
        (line number, text) pairs, one per source line a comment covers.
    """
    code: list[str] = []
    comments: list[tuple[int, str]] = []
    line = 1
    index = 0
    length = len(text)
    while index < length:
        char = text[index]
        pair = text[index : index + 2]
        if pair == "//":
            end = text.find("\n", index)
            end = length if end < 0 else end
            comments.append((line, text[index:end]))
            index = end
        elif pair == "/*":
            end = text.find("*/", index + 2)
            end = length if end < 0 else end + 2
            for offset, part in enumerate(text[index:end].split("\n")):
                comments.append((line + offset, part))
            line += text.count("\n", index, end)
            code.append("\n" * text.count("\n", index, end))
            index = end
        elif pair == 'R"' and not (index and (text[index - 1].isalnum() or text[index - 1] == "_")):
            opening = text.find("(", index + 2)
            delimiter = ")" + text[index + 2 : opening] + '"'
            end = text.find(delimiter, opening)
            end = length if end < 0 else end + len(delimiter)
            newlines = text.count("\n", index, end)
            line += newlines
            code.append(" " + "\n" * newlines)
            index = end
        elif char in "\"'":
            end = index + 1
            while end < length and text[end] != char and text[end] != "\n":
                end += 2 if text[end] == "\\" else 1
            code.append(" ")
            index = end + 1
        else:
            if char == "\n":
                line += 1
            code.append(char)
            index += 1
    return "".join(code), comments


def allowed_values(source: Path) -> set[int]:
    """Collect the hex literals the address files use in code.

    Args:
        source: CAPIE source folder.

    Returns:
        Every hex literal value written in the code of the address files.
    """
    files = [source / name for name in ADDRESS_FILES]
    files += sorted(source.rglob("*_pins.hpp"))
    values: set[int] = set()
    for path in files:
        code, _ = split_source(path.read_text(encoding="utf-8"))
        values.update(int(match.group(1), 16) for match in LITERAL.finditer(code))
    return values


def scanned_files(source: Path) -> list[Path]:
    """List the files whose comments are checked.

    Args:
        source: CAPIE source folder.

    Returns:
        Sorted C++ files under the scanned sub-folders.
    """
    return sorted(
        path
        for folder in SCANNED
        for path in (source / folder).rglob("*")
        if path.suffix in SUFFIXES and path.is_file()
    )


def findings(path: Path, allowed: set[int]) -> list[tuple[int, int]]:
    """Find unpinned hex numbers in the comments of one file.

    Args:
        path: C++ file.
        allowed: Values from :func:`allowed_values`.

    Returns:
        (line number, value) pairs in source order.
    """
    _, comments = split_source(path.read_text(encoding="utf-8"))
    found: list[tuple[int, int]] = []
    for line, comment in comments:
        for match in CANDIDATE.finditer(comment):
            digits = match.group(1)
            if not any(char.isdigit() for char in digits) or digits in NOT_ADDRESSES:
                continue
            if comment[: match.start()].endswith("U+"):
                continue
            value = int(digits, 16)
            if value not in allowed:
                found.append((line, value))
    return found


def main() -> int:
    """Run the check.

    Returns:
        Process exit code (1 when an unpinned address was found).
    """
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "files", nargs="*", type=Path, help="files to check (default: the DLL sources)"
    )
    arguments = parser.parse_args()
    allowed = allowed_values(SOURCE)
    files = [path.resolve() for path in arguments.files] or scanned_files(SOURCE)
    total = 0
    for path in files:
        for line, value in findings(path, allowed):
            total += 1
            shown = path.relative_to(ROOT).as_posix() if path.is_relative_to(ROOT) else str(path)
            print(f"{shown}:{line}: 0x{value:X} in a comment is not a pinned value")
    print(f"comment addresses: {len(files)} files, {total} unpinned")
    return 1 if total else 0


if __name__ == "__main__":
    sys.exit(main())
