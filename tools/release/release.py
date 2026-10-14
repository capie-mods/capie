#!/usr/bin/env python3
"""Local release of CAPIE and its mods: preflight, build, GitHub release, VirusTotal, Workshop.

Usage::

    uv run tools/release/release.py all --dry-run
    uv run tools/release/release.py preflight --minhook-dir C:/src/minhook
    uv run tools/release/release.py build --signing-key D:/keys/capie-release.private.xml
    uv run tools/release/release.py release            # draft pre-release
    uv run tools/release/release.py virustotal --update-notes
    uv run tools/release/release.py workshop --steam-user <account name>

Every step reads ``tools/release/release.toml``. Nothing is published without a draft
first or a typed confirmation, and Steam passwords are typed into SteamCMD only. See
``docs/contributing/release-script.md``.
"""

from __future__ import annotations

import sys

from releasekit.cli import main

if __name__ == "__main__":
    sys.exit(main())
