"""Check that the port tools read and rewrite a pattern written as adjacent string literals.

A signature may be written as several literals on one line (``"48895C2408" "4889742418" "57"``,
as in ``catalog_source_pins.hpp``) or split over lines. ``port_scan.py`` must join them before it
scans, and ``apply.py`` must replace the whole pattern, not only its first literal (which would
leave the old tail behind and corrupt the signature). This script runs ``apply.py`` on a small
fake source tree and checks ``port_scan.py``'s join expression on the same text. It needs only
the standard library; ``plugin/capie/build.ps1`` runs it.

Usage::

    python -I tools/port/check_literal_join.py
"""

import ast
import json
import os
import pathlib
import re
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
"""This folder (``tools/port``)."""

PINS = """inline constexpr core::Signature sigs[] = {
    {"demo.one_line", core::SignatureGroup::country_hook, core::rva::demo_fn,
     "48895C2408" "4889742418" "57",
     "one-line literals"},
    {"demo.split", core::SignatureGroup::country_hook, 0x3000,
     "4883EC28"
     "488BD9",
     "split literals"},
};
inline constexpr core::InstructionPin pins[] = {
    {"demo.pin", 0x146808, "4889442440"},  // mov [rsp+40],rax
};
"""
"""Fake pins header: a one-line multi-literal signature, a split one and an instruction pin."""

LAYOUT = """namespace rva {
inline constexpr std::uintptr_t demo_fn = 0x1000;
}  // namespace rva
namespace off {
}  // namespace off
"""
"""Fake ``game_layout.hpp`` holding the named constant of the first signature."""

TABLES = {
    "demo.one_line": {"rva": "0x2000", "hex": "48895c24104889742418cc"},
    "demo.split": {"rva": "0x3100", "hex": "4883ec38488bd9"},
    "demo.pin": {"rva": "0x146900", "hex": "4889442440"},
}
"""Generated tables, as ``gen_tables.py`` writes them."""

MAP = {"rip_pins": {}, "rva": {}, "off": {}, "host": {}, "console": {}}
"""Empty port map: only the table entries are rewritten."""


def literal_gap() -> str:
    """Read ``LITERAL_GAP`` from ``port_scan.py`` without running it (it needs an executable).

    Returns:
        The regular expression.

    Raises:
        LookupError: If ``port_scan.py`` no longer defines it as a string.
    """
    tree = ast.parse((HERE / "port_scan.py").read_text(encoding="utf-8"))
    for node in tree.body:
        if isinstance(node, ast.Assign) and any(
            isinstance(t, ast.Name) and t.id == "LITERAL_GAP" for t in node.targets
        ):
            value = ast.literal_eval(node.value)
            if isinstance(value, str):
                return value
    msg = "port_scan.py defines no LITERAL_GAP string"
    raise LookupError(msg)


def main() -> int:
    """Run both checks.

    Returns:
        0 when both pass, 1 otherwise.
    """
    failures = []
    joined = re.sub(literal_gap(), "", PINS)
    hexes = re.findall(r'"([0-9a-fA-F?]{4,})"', joined)
    if hexes[:2] != ["48895C2408488974241857", "4883EC28488BD9"]:
        failures.append(f"port_scan.py joins to {hexes[:2]}")

    with tempfile.TemporaryDirectory() as tmp:
        root = pathlib.Path(tmp)
        src = root / "plugin/capie/src"
        files = {
            "core/signature_table.hpp": "",
            "core/game_layout.hpp": LAYOUT,
            "features/demo/demo_pins.hpp": PINS,
            "features/console/console_wiring.hpp": "",
            "features/console/console_layout.hpp": "",
            "features/host/host_probe.hpp": "",
        }
        for rel, text in files.items():
            (src / rel).parent.mkdir(parents=True, exist_ok=True)
            (src / rel).write_text(text, encoding="utf-8")
        (root / "tables.json").write_text(json.dumps(TABLES), encoding="utf-8")
        (root / "map.json").write_text(json.dumps(MAP), encoding="utf-8")
        env = {**os.environ, "CAPIE_PORT_MAP": str(root / "map.json")}
        done = subprocess.run(  # noqa: S603 - this folder's own helper, fixed arguments
            [sys.executable, "-I", str(HERE / "apply.py"), str(root), str(root / "tables.json")],
            capture_output=True,
            text=True,
            check=False,
            env=env,
        )
        if done.returncode != 0:
            failures.append(f"apply.py exit {done.returncode}: {done.stderr.strip()}")
        else:
            pins = (src / "features/demo/demo_pins.hpp").read_text(encoding="utf-8")
            layout = (src / "core/game_layout.hpp").read_text(encoding="utf-8")
            if '     "48895C24104889742418CC",\n     "one-line literals"' not in pins:
                failures.append("one-line literals not replaced as a whole")
            if '0x3100,\n     "4883EC38488BD9",\n     "split literals"' not in pins:
                failures.append("split literals not replaced as a whole")
            if '{"demo.pin", 0x146900, "4889442440"},  // mov [rsp+40],rax' not in pins:
                failures.append("instruction pin not rewritten")
            if "demo_fn = 0x2000;" not in layout:
                failures.append("named constant of the signature not rewritten")

    for failure in failures:
        print(f"FAIL {failure}")
    print("check_literal_join: " + ("FAILED" if failures else "pass"))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
