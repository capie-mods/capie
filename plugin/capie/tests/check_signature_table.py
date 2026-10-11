"""Compare the CAPIE signature table with the values in the accepted sources.

The accepted sources are pinned by SHA-256 (country 689924A8..., caption bridge rev7
CD440557..., caption core 25529648..., focus 5D622B58...). Their byte arrays, hex
fingerprints, hook RVAs, slot pins, tokens and the RIP pin are extracted with regular
expressions and compared field by field (RVA, length, bytes, wildcard positions) with
``plugin/capie/src/core/signature_table.hpp`` and ``game_layout.hpp``. Any difference
fails.

Usage::

    python -I check_signature_table.py
"""

import hashlib
import pathlib
import re
import sys

HERE = pathlib.Path(__file__).resolve().parent
"""Folder of this script (``plugin/capie/tests``)."""

REPO = HERE.parents[2]
"""Repository root."""

ACCEPTED = {
    "plugin/src/country.cpp": "689924a8faa5ec24e3f66c1bdf119e358f9eefa4da2ca815f5b7b3cb27e71ccf",
    "plugin/probes/caption_refresh_candidate/caption_refresh_bridge.cpp": (
        "cd440557ce198717cf28cbac1fa5335f2951aeed69f6a783a6ea5f8387baac7c"
    ),
    "plugin/probes/caption_refresh_candidate/caption_refresh_core.hpp": (
        "25529648962bebfcf4e294c6094faa54d59b45d89f3c9a18df7603c8fc8aab5f"
    ),
    "plugin/probes/caption_refresh_candidate/focus_release.hpp": (
        "5d622b5870b2a80e437a922613448b0c009ae28d393fae2dd09ec9eaf49813c0"
    ),
}
"""Accepted source files and their pinned SHA-256."""

TABLE = REPO / "plugin/capie/src/core/signature_table.hpp"
"""The CAPIE signature table."""

LAYOUT = REPO / "plugin/capie/src/core/game_layout.hpp"
"""The CAPIE layout constants."""

CAPTION_SOURCE = "plugin/probes/caption_refresh_candidate/caption_refresh_core.hpp"
"""Accepted caption core."""

FOCUS_SOURCE = "plugin/probes/caption_refresh_candidate/focus_release.hpp"
"""Accepted focus release."""

BRIDGE_SOURCE = "plugin/probes/caption_refresh_candidate/caption_refresh_bridge.cpp"
"""Accepted rev7 bridge."""

CAPTION_COUNT = 25
"""Number of caption fingerprints in the accepted core."""

FOCUS_COUNT = 8
"""Number of focus fingerprints in the accepted focus release."""

TABLE_SIZE = 47
"""Expected number of entries in the new table (3 + 11 + 25 + 8)."""

MIN_COMPARED = 10
"""Fewest named constants a source must contribute (guards against a broken regex)."""

EXPECTED_LAYOUT = {
    "rva::view_is_visible": 0x1237520,
    "rva::observer_frame": 0x1582820,
    "rva::observer_refresh": 0x15863F0,
    "rva::idler_gui_getter": 0xDCCF30,
    "rva::vt_politics_view": 0x29D9648,
    "rva::vt_politics_observer": 0x29D96F0,
    "rva::vt_ingame_idler": 0x2968FF0,
    "rva::edit_unfocus": 0x22F4770,
    "rva::edit_focus": 0x22F3FB0,
    "rva::vt_edit": 0x2B4A940,
    "rva::token_politics_tab": 0x294E4CC,
    "rva::token_player_context": 0x2AECB3C,
    "rva::manager_global": 0x3453230,
    "rva::focus_manager_rip_displacement": 0x22FBEC6,
    "rva::focus_manager_rip_next": 0x22FBECA,
    "rva::request_view_refresh": 0xB6E210,
    "rva::focus_switch": 0x225E320,
    "off::slot_view_visible": 0x28,
    "off::slot_observer_frame": 0x38,
    "off::slot_observer_refresh": 0x48,
    "off::slot_idler_gui": 0xB8,
    "off::slot_edit_focus": 0xC0,
    "off::slot_edit_unfocus": 0xC8,
    "value::token_politics_tab": 0x3AB2,
    "value::token_player_context": 0x3A70,
}
"""Slot pins, tokens and the RIP pin as written in the accepted core, focus and bridge sources."""

RENAMED = {"off::slot_focus": "off::slot_edit_focus", "off::slot_unfocus": "off::slot_edit_unfocus"}
"""Accepted constant names that were renamed in game_layout.hpp."""

EDITOR_RVAS = {
    "tick": "rva::window_tick",
    "bind": "rva::window_bind",
    "dispatch": "rva::window_dispatch",
    "ctor": "rva::effect_ctor",
    "execute": "rva::effect_execute",
    "key": "rva::edit_key_handler",
    "wrapper": "rva::edit_key_wrapper",
    "find_edit": "rva::find_edit",
    "find_instant": "rva::find_instant",
    "set_text": "rva::text_buffer_set",
    "refresh_edit": "rva::edit_refresh",
}
"""Accepted ``<name>_rva`` constants of the bridge and their game_layout.hpp names."""


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


def read(results: Results, relative: str) -> str:
    """Return a pinned accepted source after verifying its SHA-256.

    Args:
        results: Check recorder.
        relative: Repository-relative path.

    Returns:
        File text.
    """
    path = REPO / relative
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    results.check(
        f"{relative} is the accepted version ({digest[:8]})", ok=digest == ACCEPTED[relative]
    )
    return path.read_text(encoding="utf-8")


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


def new_table(layout: dict[str, int]) -> dict[str, tuple[int, str]]:
    """Return the new signature entries keyed by id.

    Args:
        layout: Constants of game_layout.hpp.

    Returns:
        Mapping of id to (RVA, lower-case pattern with ``??`` wildcards).
    """
    text = TABLE.read_text(encoding="utf-8")
    pattern = r'\{"([\w.]+)", SignatureGroup::\w+, (rva::\w+|0x[0-9A-Fa-f]+),\s*"([0-9a-f?]+)"'
    entries: dict[str, tuple[int, str]] = {}
    for identifier, rva, hexbytes in (match.groups() for match in re.finditer(pattern, text)):
        value = layout[rva] if rva.startswith("rva::") else int(rva, 16)
        entries[identifier] = (value, hexbytes)
    return entries


def check_country(results: Results, entries: dict[str, tuple[int, str]]) -> None:
    """Compare the three masked country patterns.

    Args:
        results: Check recorder.
        entries: New table.
    """
    country = read(results, "plugin/src/country.cpp")
    for rva, body in re.findall(r"\{(0x239\w+),\s*\{(.*?)\}", country, re.DOTALL):
        tokens = [t.strip() for t in body.split(",")]
        expected = "".join("??" if t == "-1" else f"{int(t, 16):02x}" for t in tokens)
        found = [k for k, v in entries.items() if v[0] == int(rva, 16)]
        ok = len(found) == 1 and entries[found[0]][1] == expected
        results.check(f"country hook {rva} bytes/mask/length", ok=ok)


def check_editor(
    results: Results, entries: dict[str, tuple[int, str]], layout: dict[str, int]
) -> None:
    """Compare the eleven editor byte arrays and hook/callee RVAs.

    Args:
        results: Check recorder.
        entries: New table.
        layout: Constants of game_layout.hpp.
    """
    bridge = read(results, BRIDGE_SOURCE)
    rvas = {
        name: int(value, 16)
        for name, value in re.findall(r"(\w+)_rva\s*=\s*(0x[0-9A-Fa-f]+)", bridge)
    }
    arrays = re.findall(
        r"constexpr unsigned char (\w+)_bytes\[\]\s*=\s*\{(.*?)\};", bridge, re.DOTALL
    )
    for name, body in arrays:
        expected = "".join(f"{int(b, 16):02x}" for b in re.findall(r"0x([0-9a-fA-F]{2})", body))
        entry = entries.get(f"editor.{name}")
        ok = entry is not None and entry[0] == rvas[name] and entry[1] == expected
        results.check(f"editor {name} rva/bytes/length", ok=ok)
    for old, new in EDITOR_RVAS.items():
        results.check(f"{new} == accepted {old}_rva", ok=layout.get(new) == rvas[old])


def check_fingerprints(results: Results, entries: dict[str, tuple[int, str]]) -> None:
    """Compare the 25 caption and 8 focus fingerprints.

    Args:
        results: Check recorder.
        entries: New table.
    """
    for relative, prefix, array, count in (
        (CAPTION_SOURCE, "caption", "Fingerprint fingerprints[]", CAPTION_COUNT),
        (FOCUS_SOURCE, "focus", "focus_fingerprints[]", FOCUS_COUNT),
    ):
        source = read(results, relative)
        block = source[source.index(array) :]
        block = block[: block.index("};")]
        old = re.findall(r'\{(0x[0-9A-Fa-f]+),\s*"([0-9a-f]+)"', block)
        results.check(f"{prefix} accepted count", ok=len(old) == count)
        for rva, hexbytes in old:
            entry = entries.get(f"{prefix}.{rva[2:].upper()}")
            ok = entry is not None and entry[0] == int(rva, 16) and entry[1] == hexbytes
            results.check(f"{prefix} {rva}", ok=ok)


def check_named_constants(results: Results, layout: dict[str, int]) -> None:
    """Require every named RVA/offset of the accepted core and focus to exist with the same value.

    Args:
        results: Check recorder.
        layout: Constants of game_layout.hpp.
    """
    for name, value in EXPECTED_LAYOUT.items():
        results.check(f"{name} == {value:#x}", ok=layout.get(name) == value)
    for relative in (CAPTION_SOURCE, FOCUS_SOURCE):
        namespace = ""
        compared = 0
        for line in read(results, relative).splitlines():
            found = re.match(r"namespace (\w+) \{", line)
            if found:
                namespace = found.group(1)
            if line.startswith("}"):
                namespace = ""
            if namespace not in {"rva", "off"}:
                continue
            for name, value in re.findall(r"(\w+) = (0x[0-9A-Fa-f]+)", line):
                key = RENAMED.get(f"{namespace}::{name}", f"{namespace}::{name}")
                file_name = relative.rsplit("/", 1)[-1]
                results.check(f"{file_name} {key} == {value}", ok=layout.get(key) == int(value, 16))
                compared += 1
        results.check(f"{relative} constants compared ({compared})", ok=compared >= MIN_COMPARED)


def main() -> int:
    """Run the comparison.

    Returns:
        Process exit code (0 when equal).
    """
    results = Results()
    layout = layout_values()
    entries = new_table(layout)
    results.check(
        f"{TABLE_SIZE} entries in the new table (found {len(entries)})",
        ok=len(entries) == TABLE_SIZE,
    )
    check_country(results, entries)
    check_editor(results, entries, layout)
    check_fingerprints(results, entries)
    check_named_constants(results, layout)
    print(f"signature table equivalence: {results.checks} checks, {results.failures} failures")
    return 1 if results.failures else 0


if __name__ == "__main__":
    sys.exit(main())
