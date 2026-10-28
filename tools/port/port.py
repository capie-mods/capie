"""One entry point for porting CAPIE to a new hoi4.exe (see README.md in this folder).

Usage::

    python -I tools/port/port.py locate [--exe PATH] [--check]  # step 0: RVAs from RTTI/strings
    python -I tools/port/port.py triage [--exe PATH]    # day 1: what breaks on the installed game?
    python -I tools/port/port.py --map map_<version>.json generate [--exe PATH]
    python -I tools/port/port.py apply                  # write tables, constants, test literals
    python -I tools/port/port.py verify                 # signature checker on the sources

``locate`` resolves the rebuild-proof anchors of ``anchors.json`` (RTTI class names, string
literals and the call relations around them) and prints the RVA of every function, vtable and
global of ``game_layout.hpp`` in the given executable; ``--check`` compares them with the
header and fails on a difference (the self-check on the pinned build). ``triage`` needs no
map: it scans every pin of the current sources in the new executable and writes
``out/triage.md`` (unchanged / moved / bytes changed / not found), the input for the analysis.
``generate`` builds the new patterns from a finished map (``map_<version>.json``), ``apply``
writes them into the sources and moves the test literals, ``verify`` runs
``check_signature_table.py``. The native build (``plugin/capie/build.ps1``) is the final gate.

Every helper runs as ``python -I`` with the dependencies given to ``uv run --with``.
"""

import argparse
import hashlib
import json
import os
import pathlib
import re
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
"""This folder (``tools/port``)."""

REPO = HERE.parents[1]
"""Repository root."""

OUT = HERE / "out"
"""Scratch output (git-ignored)."""

DEFAULT_EXE = pathlib.Path(
    r"C:\Program Files (x86)\Steam\steamapps\common\Hearts of Iron IV\hoi4.exe"
)
"""Steam install path of the game."""

LAYOUT = REPO / "plugin/capie/src/core/game_layout.hpp"
"""The header whose ``namespace rva`` the anchors resolve."""

MAP_NAME = "map_1.19.4.0.json"
"""Old -> new map the helpers read (``--map``; exported to them as ``CAPIE_PORT_MAP``)."""

DEPS = ["--with-requirements", str(HERE / "requirements.txt")]
"""Python packages of the helpers (pinned in requirements.txt)."""

SHORT_PIN_BYTES = 16
"""Pins of at least this many bytes may be accepted on one unique hit; shorter pins are not."""


def run(script: str, *args: str) -> str:
    """Run one helper of this folder and return its standard output.

    Args:
        script: File name in this folder.
        *args: Arguments.

    Returns:
        Captured standard output (also echoed).

    Raises:
        SystemExit: If the helper exits with a non-zero status.
    """
    cmd = ["uv", "run", "--no-project", *DEPS, "python", "-I", str(HERE / script), *args]
    env = {**os.environ, "CAPIE_PORT_MAP": MAP_NAME}
    done = subprocess.run(  # noqa: S603 - fixed uv command built from this folder's own helpers
        cmd, capture_output=True, text=True, check=False, env=env
    )
    sys.stdout.write(done.stdout)
    if done.returncode != 0:
        sys.stderr.write(done.stderr)
        msg = f"{script} failed (exit {done.returncode})"
        raise SystemExit(msg)
    return done.stdout


def pinned_build() -> tuple[str, str]:
    """Return the SHA-256 and version CAPIE currently pins (``game_build.hpp``).

    Returns:
        (sha256, version).
    """
    text = (REPO / "plugin/capie/src/core/game_build.hpp").read_text(encoding="utf-8")
    sha = re.search(r'game_exe_sha256\[\] = "([0-9a-f]{64})"', text)
    ver = re.search(r'game_version\[\] = "([\d.]+)"', text)
    return (str(sha[1]) if sha else "", str(ver[1]) if ver else "")


def triage(exe: pathlib.Path) -> None:
    """Scan every pin of the sources in ``exe`` and write ``out/triage.md``.

    Args:
        exe: The new executable.
    """
    OUT.mkdir(exist_ok=True)
    digest = hashlib.sha256(exe.read_bytes()).hexdigest()
    pinned, version = pinned_build()
    if digest == pinned:
        print(f"{exe} is the pinned build {version}; nothing to port.")
        return
    run("port_scan.py", str(REPO), str(exe), str(OUT / "scan.json"))
    run("fuzzy.py", str(OUT / "scan.json"), str(exe), str(OUT / "fuzzy.json"))
    scan = json.loads((OUT / "scan.json").read_text())
    fuzzy = {f["id"]: f for f in json.loads((OUT / "fuzzy.json").read_text())}
    rows = []
    for e in scan:
        old = e["old_rva"]
        if len(e["hits"]) == 1 and e["n"] >= SHORT_PIN_BYTES:
            new = e["hits"][0]
            state = "unchanged" if new == old else "moved"
            rows.append((state, e["id"], e["file"], old, new, ""))
            continue
        best = fuzzy.get(e["id"], {}).get("cands", [{}])[0]
        if best.get("score") == best.get("of") and best:
            rows.append(("moved (short, check context)", e["id"], e["file"], old, best["rva"], ""))
        elif best:
            note = f"{best['score']}/{best['of']} bytes"
            rows.append(("bytes changed", e["id"], e["file"], old, best["rva"], note))
        else:
            rows.append(("not found", e["id"], e["file"], old, None, ""))
    counts: dict[str, int] = {}
    for r in rows:
        counts[r[0]] = counts.get(r[0], 0) + 1
    lines = [
        f"# Triage: CAPIE pins in {exe.name} (SHA-256 {digest[:8]}...)",
        "",
        f"Pinned build: {version} ({pinned[:8]}...). Entries: {len(rows)}. "
        + ", ".join(f"{k}: {v}" for k, v in sorted(counts.items())),
        "",
        "`bytes changed` usually means a layout change (a displacement differs): disassemble old",
        "against new (`dis.py`) before trusting the address. `short` pins are never accepted on a",
        "global unique hit; place them inside their function (`shortpins.py`).",
        "",
        "| State | Pin | File | Old RVA | Candidate | Note |",
        "|---|---|---|---|---|---|",
    ]
    for state, ident, file, old, new, note in sorted(rows, key=lambda r: (r[0], r[2], r[1])):
        old_txt = old and f"{old:X}"
        new_txt = (new and f"{new:X}") or "-"
        lines.append(f"| {state} | `{ident}` | {file} | {old_txt} | {new_txt} | {note} |")
    (OUT / "triage.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"wrote {OUT / 'triage.md'}")


def locate(exe: pathlib.Path, *, check: bool) -> int:
    """Resolve every anchor of ``anchors.json`` in ``exe`` and write ``out/locate.json``.

    Args:
        exe: The executable (the new build, or the pinned one for the self-check).
        check: Compare with ``game_layout.hpp`` and fail on any difference.

    Returns:
        Exit code of ``locate.py`` (0 when every anchor resolved and, with ``check``, matched).
    """
    OUT.mkdir(exist_ok=True)
    args = [str(exe), str(HERE / "anchors.json"), "--json", str(OUT / "locate.json")]
    if check:
        args += ["--check", str(LAYOUT)]
    try:
        run("locate.py", *args)
    except SystemExit as failed:
        print(failed, file=sys.stderr)
        return 1
    return 0


def main() -> int:
    """Parse the command line and run one step.

    Returns:
        Process exit code.

    Raises:
        SystemExit: If the map file needed by ``generate`` or ``apply`` is missing.
    """
    global MAP_NAME  # noqa: PLW0603 - one setting shared by every helper call
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    sub = parser.add_subparsers(dest="step", required=True)
    loc = sub.add_parser("locate")
    loc.add_argument("--exe", type=pathlib.Path, default=DEFAULT_EXE)
    loc.add_argument(
        "--check", action="store_true", help="compare with game_layout.hpp; fail on a difference"
    )
    t = sub.add_parser("triage")
    t.add_argument("--exe", type=pathlib.Path, default=DEFAULT_EXE)
    g = sub.add_parser("generate")
    g.add_argument("--exe", type=pathlib.Path, default=DEFAULT_EXE)
    sub.add_parser("apply")
    sub.add_parser("verify")
    parser.add_argument(
        "--map", default=MAP_NAME, help="map file in tools/port (map_<version>.json)"
    )
    args = parser.parse_args()
    MAP_NAME = args.map
    if args.step in ("generate", "apply") and not (HERE / MAP_NAME).is_file():
        msg = f"map file not found: tools/port/{MAP_NAME}"
        raise SystemExit(msg)
    if args.step == "locate":
        return locate(args.exe, check=args.check)
    if args.step == "triage":
        triage(args.exe)
    elif args.step == "generate":
        run("gen_tables.py", str(OUT / "scan.json"), str(args.exe), str(OUT / "new_tables.json"))
    elif args.step == "apply":
        run("apply.py", str(REPO), str(OUT / "new_tables.json"))
        run("retest.py", str(REPO))
    elif args.step == "verify":
        checker = str(REPO / "plugin/capie/tests/check_signature_table.py")
        cmd = ["uv", "run", "--no-project", "python", "-I", checker]
        return subprocess.run(cmd, check=False).returncode  # noqa: S603 - fixed uv command, no shell
    return 0


if __name__ == "__main__":
    sys.exit(main())
