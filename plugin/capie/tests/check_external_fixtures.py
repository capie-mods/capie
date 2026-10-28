"""Verify the files vendored from other repositories (``tests/fixtures/external``).

The offline checks of ``build.ps1`` and ``toggle-mod/check_mod.py`` compare CAPIE with files
that live in other repositories: the demo mod's ``.gui`` and the SDK character table and
localisation (``hoi4-rename-anything``), and Rename Anything's character table, generated
localisation and test-session script (``rename-anything``). Copies of exactly those files sit
in ``fixtures/external/<repo>/<path>``, and ``fixtures/external/lock.json`` records for each
one the source repository, the path there, the commit it was copied from and its SHA-256.

Without options the script checks that every listed file is present with its recorded
SHA-256 and that the folder holds no unlisted file. ``--refresh`` copies the files again from
sibling checkouts (``<siblings>/<repo>``, default: the folder that holds this repository),
taking the committed version at the checkout's ``HEAD`` (never uncommitted changes), and
rewrites the lock. A repository that is not checked out there is reported and kept as it is.

Usage::

    python -I check_external_fixtures.py [--refresh] [--siblings DIR]
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import subprocess
import sys
from dataclasses import asdict, dataclass
from pathlib import Path, PurePosixPath

HERE = Path(__file__).resolve().parent
"""Folder of this script (``plugin/capie/tests``)."""

REPO = HERE.parents[2]
"""Repository root."""

FIXTURES = HERE / "fixtures" / "external"
"""Folder of the vendored files, one sub-folder per source repository."""

LOCK = FIXTURES / "lock.json"
"""Lock file: source repository, path, commit and SHA-256 of every vendored file."""

IGNORED = frozenset({"lock.json", "__pycache__"})
"""Entries of the fixture tree that are not vendored files (``__pycache__``: imports)."""

SHA256_PATTERN = re.compile(r"^[0-9a-f]{64}$")
"""A recorded SHA-256 (lower-case hex)."""

COMMIT_PATTERN = re.compile(r"^[0-9a-f]{40}$")
"""A recorded git commit (full SHA-1)."""


@dataclass
class Entry:
    """One vendored file as the lock records it."""

    repo: str
    path: str
    commit: str
    sha256: str

    @property
    def local(self) -> Path:
        """Where the copy lives in this repository.

        Returns:
            ``fixtures/external/<repo>/<path>``.
        """
        return FIXTURES / self.repo / PurePosixPath(self.path)


def read_lock() -> list[Entry]:
    """Read ``lock.json``.

    Returns:
        The entries in file order.
    """
    data = json.loads(LOCK.read_text(encoding="utf-8"))
    return [Entry(**item) for item in data["files"]]


def write_lock(entries: list[Entry]) -> None:
    """Write ``lock.json`` (LF, trailing newline).

    Args:
        entries: Entries in the order to keep.
    """
    text = json.dumps({"files": [asdict(e) for e in entries]}, indent=2) + "\n"
    LOCK.write_text(text, encoding="utf-8", newline="\n")


def sha256_file(path: Path) -> str:
    """SHA-256 of a file.

    Args:
        path: File.

    Returns:
        Lower-case hex digest.
    """
    return hashlib.sha256(path.read_bytes()).hexdigest()


def verify(entries: list[Entry]) -> int:
    """Check every vendored file against the lock.

    Args:
        entries: Lock entries.

    Returns:
        Number of failures (each one is printed).
    """
    failures = 0
    listed = set()
    for entry in entries:
        listed.add(entry.local)
        label = f"{entry.repo}/{entry.path}"
        if not COMMIT_PATTERN.match(entry.commit) or not SHA256_PATTERN.match(entry.sha256):
            print(f"FAIL {label}: the lock needs a full commit and a lower-case SHA-256")
            failures += 1
        elif not entry.local.is_file():
            print(f"FAIL {label}: vendored copy missing")
            failures += 1
        elif (actual := sha256_file(entry.local)) != entry.sha256:
            print(f"FAIL {label}: SHA-256 {actual} != locked {entry.sha256}")
            failures += 1
    for path in sorted(FIXTURES.rglob("*")):
        relative = path.relative_to(FIXTURES)
        if path.is_file() and path not in listed and not IGNORED & set(relative.parts):
            print(f"FAIL {relative.as_posix()}: not listed in lock.json")
            failures += 1
    print(f"external fixtures: {len(entries)} files, {failures} failures")
    return failures


def git(checkout: Path, *args: str) -> bytes:
    """Run git in a sibling checkout.

    Args:
        checkout: Repository folder.
        *args: git arguments.

    Returns:
        Standard output.

    Raises:
        FileNotFoundError: git is not on PATH.
    """
    program = shutil.which("git")
    if program is None:
        msg = "git not found on PATH"
        raise FileNotFoundError(msg)
    return subprocess.run(  # noqa: S603 - fixed git CLI, arguments from the lock, no shell
        [program, "-C", str(checkout), *args], capture_output=True, check=True
    ).stdout


def refresh(entries: list[Entry], siblings: Path) -> None:
    """Copy each file again from its sibling checkout and update the lock entries.

    Args:
        entries: Lock entries (updated in place).
        siblings: Folder that holds the sibling checkouts.
    """
    for entry in entries:
        checkout = siblings / entry.repo
        label = f"{entry.repo}/{entry.path}"
        if not (checkout / ".git").exists():
            print(f"SKIP {label}: no checkout at {checkout} (copy kept)")
            continue
        commit = git(checkout, "rev-parse", "HEAD").decode("ascii").strip()
        if git(checkout, "status", "--porcelain", "--", entry.path).strip():
            print(f"NOTE {label}: uncommitted changes in {checkout}; the committed file is used")
        data = git(checkout, "show", f"{commit}:{entry.path}")
        entry.local.parent.mkdir(parents=True, exist_ok=True)
        entry.local.write_bytes(data)
        digest = hashlib.sha256(data).hexdigest()
        changed = "changed" if digest != entry.sha256 else "unchanged"
        entry.commit, entry.sha256 = commit, digest
        print(f"OK   {label}: {changed}, {commit[:12]}")
    write_lock(entries)


def main() -> int:
    """Verify, after an optional refresh.

    Returns:
        Process exit code.
    """
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "--refresh", action="store_true", help="copy the files again from sibling checkouts"
    )
    parser.add_argument(
        "--siblings",
        type=Path,
        default=REPO.parent,
        help="folder that holds the sibling checkouts (default: %(default)s)",
    )
    args = parser.parse_args()
    entries = read_lock()
    if args.refresh:
        refresh(entries, args.siblings)
    return 1 if verify(entries) else 0


if __name__ == "__main__":
    sys.exit(main())
