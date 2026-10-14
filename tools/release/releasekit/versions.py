"""Version numbers and where each one comes from.

Single sources of truth:

* CAPIE (the GitHub release, its tag and the ZIP): ``capie_release`` in
  ``plugin/capie/src/core/game_build.hpp``. It is compiled into the DLLs and answered
  for ``CAPIE_BUILD``; ``capie_version`` (shown to players) and ``capie_build_number``
  (the multiplayer handshake, ADR-0012) must agree with it, which :func:`capie_problems`
  checks. The supported ``hoi4.exe`` hash for the manifest comes from the same file.
* Each Workshop item: ``version`` in its ``descriptor.mod``.

Nothing here writes a file.
"""

from __future__ import annotations

import re
from dataclasses import dataclass

RELEASE_VERSION = re.compile(r"^\d+\.\d+\.\d+$")
"""Release version format (``x.y.z``; the bootstrap also accepts suffixes, releases do not)."""

SHA256_HEX = re.compile(r"^[0-9a-fA-F]{64}$")
"""A SHA-256 written as 64 hexadecimal digits."""

BUILD_NUMBER_MAJOR = 10000
"""Weight of the major version in ``capie_build_number``."""

BUILD_NUMBER_MINOR = 100
"""Weight of the minor version in ``capie_build_number``."""

_HEADER_FIELDS = {
    "release": re.compile(r'capie_release\[\]\s*=\s*"([^"]*)"'),
    "short": re.compile(r'capie_version\[\]\s*=\s*"([^"]*)"'),
    "build_number": re.compile(r"capie_build_number\s*=\s*(\d+)"),
    "api_level": re.compile(r"capie_api_level\s*=\s*(\d+)"),
    "game_sha256": re.compile(r'game_exe_sha256\[\]\s*=\s*"([^"]*)"'),
    "game_version": re.compile(r'game_version\[\]\s*=\s*"([^"]*)"'),
}
"""Patterns of the values read from ``game_build.hpp``."""

_DESCRIPTOR_LINE = re.compile(r'^\s*([A-Za-z_]+)\s*=\s*"([^"]*)"\s*$', re.MULTILINE)
"""One ``key="value"`` line of a ``.mod`` descriptor (blocks such as ``tags`` are skipped)."""

_LAUNCHER_VERSION = re.compile(r"\$script:LauncherVersion\s*=\s*'([^']*)'")
"""Launcher version constant in ``build_launcher.ps1``."""

_WORKSHOP_ID = re.compile(r'capie_mod_workshop_id\[\]\s*=\s*"([^"]*)"')
"""Compiled-in Workshop ID of the switch mod in the launcher configuration."""

_DESCRIPTION_VERSION = re.compile(r"\bCAPIE (\d+\.\d+)\b")
"""A CAPIE version quoted in player-facing text ("CAPIE 1.4")."""


class VersionError(ValueError):
    """A version source cannot be read."""


@dataclass(frozen=True)
class CapieVersions:
    """Values of ``game_build.hpp``."""

    release: str
    short: str
    build_number: int
    api_level: int
    game_sha256: str
    game_version: str


def read_capie_versions(header: str) -> CapieVersions:
    """Read the CAPIE version constants.

    Args:
        header: Text of ``game_build.hpp``.

    Returns:
        The values.

    Raises:
        VersionError: A constant is missing.
    """
    found: dict[str, str] = {}
    for name, pattern in _HEADER_FIELDS.items():
        match = pattern.search(header)
        if match is None:
            msg = f"game_build.hpp has no {name} constant"
            raise VersionError(msg)
        found[name] = match.group(1)
    return CapieVersions(
        release=found["release"],
        short=found["short"],
        build_number=int(found["build_number"]),
        api_level=int(found["api_level"]),
        game_sha256=found["game_sha256"].upper(),
        game_version=found["game_version"],
    )


def capie_problems(versions: CapieVersions) -> list[str]:
    """Check that the CAPIE constants agree with ``capie_release``.

    Args:
        versions: Values of ``game_build.hpp``.

    Returns:
        One message per disagreement (empty when consistent).
    """
    if not RELEASE_VERSION.match(versions.release):
        return [f"capie_release {versions.release!r} is not x.y.z"]
    major, minor, patch = (int(part) for part in versions.release.split("."))
    problems = []
    if versions.short != f"{major}.{minor}":
        problems.append(f"capie_version {versions.short!r} != {major}.{minor} (from capie_release)")
    expected = major * BUILD_NUMBER_MAJOR + minor * BUILD_NUMBER_MINOR + patch
    if versions.build_number != expected:
        problems.append(f"capie_build_number {versions.build_number} != {expected}")
    if not SHA256_HEX.match(versions.game_sha256):
        problems.append("game_exe_sha256 is not a SHA-256")
    return problems


def descriptor_fields(text: str) -> dict[str, str]:
    """Read the ``key="value"`` lines of a ``.mod`` descriptor.

    Args:
        text: Descriptor text.

    Returns:
        Field values by key (the first occurrence wins).
    """
    fields: dict[str, str] = {}
    for key, value in _DESCRIPTOR_LINE.findall(text.removeprefix("﻿")):
        fields.setdefault(key, value)
    return fields


def descriptor_version(text: str) -> str:
    """Read and validate the ``version`` of a descriptor.

    Args:
        text: Descriptor text.

    Returns:
        The version.

    Raises:
        VersionError: Missing or not ``x.y.z``.
    """
    version = descriptor_fields(text).get("version", "")
    if not RELEASE_VERSION.match(version):
        msg = f"descriptor version {version!r} is not x.y.z"
        raise VersionError(msg)
    return version


def launcher_version(script: str) -> str | None:
    """Read the launcher version from ``build_launcher.ps1``.

    Args:
        script: Script text.

    Returns:
        The version, or ``None`` when not found.
    """
    match = _LAUNCHER_VERSION.search(script)
    return str(match.group(1)) if match else None


def launcher_workshop_id(config_header: str) -> str | None:
    """Read the switch mod's Workshop ID compiled into the launcher.

    Args:
        config_header: Text of ``launcher/config.hpp``.

    Returns:
        The ID (empty string = not published yet), or ``None`` when not found.
    """
    match = _WORKSHOP_ID.search(config_header)
    return str(match.group(1)) if match else None


def stale_description_versions(text: str, short: str) -> list[str]:
    """Find CAPIE versions in player-facing text that differ from the current one.

    Args:
        text: Description or localisation text.
        short: Current ``capie_version`` (``major.minor``).

    Returns:
        The differing versions, sorted and without duplicates.
    """
    return sorted({v for v in _DESCRIPTION_VERSION.findall(text) if v != short})
