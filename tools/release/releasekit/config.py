"""Typed view of ``release.toml``.

All repository-specific values (paths, the GitHub repository, the bundle layout, the
Workshop items and their published IDs) live in the configuration file, never in code.
Paths in the file are relative to ``root``, which is itself relative to the file, so
after the repository split each repository keeps its own trimmed copy of the file.

The loader validates types and fails with :class:`ConfigError` naming the key. The
only write-back is :func:`set_published_file_id`, which edits one line of the file so
comments and layout stay as they are.
"""

from __future__ import annotations

import re
import tomllib
from dataclasses import dataclass
from pathlib import Path
from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from collections.abc import Mapping

DEFAULT_CONFIG = Path(__file__).resolve().parent.parent / "release.toml"
"""Configuration file used when ``--config`` is not given (next to ``release.py``)."""

VISIBILITY_VALUES = {"public": 0, "friends": 1, "hidden": 2, "unlisted": 3}
"""Steam ``ERemoteStoragePublishedFileVisibility`` values by the name the CLI accepts.

``hidden`` is ``k_ERemoteStoragePublishedFileVisibilityPrivate`` (only the creator sees
the item), shown as "Hidden" on the Workshop page.
"""


class ConfigError(ValueError):
    """The configuration file is missing a key or has a value of the wrong type."""


@dataclass(frozen=True)
class CommandSpec:
    """One external command of a step (a check, a build script).

    ``argv`` items may contain placeholders such as ``{python}`` or ``{game_dir}``; see
    :func:`releasekit.runner.expand_command`.
    """

    name: str
    argv: tuple[str, ...]
    env: tuple[tuple[str, str], ...] = ()
    clean_after: tuple[str, ...] = ()
    slow: bool = False


@dataclass(frozen=True)
class BundleFile:
    """One file of the release ZIP taken from the build output."""

    zip_path: str
    source: str
    optional: bool = False
    reviewed: bool = False


@dataclass(frozen=True)
class SwitchModConfig:
    """Where the "CAPIE" switch mod comes from and where it goes in the ZIP."""

    source_dir: str
    zip_dir: str
    descriptor_zip_path: str
    descriptor_path_value: str
    descriptor_name: str


@dataclass(frozen=True)
class BuildConfig:
    """Build step: reviewed CAPIE binaries, the optional rebuild and the bundle builds.

    The folder of the reviewed binaries is not configured here: it is the one folder that the
    ``source`` entries of ``reviewed_hashes`` (``bundle-inputs.json``) name.
    """

    rebuild_output: str
    reviewed_hashes: str
    reviewed_files: tuple[str, ...]
    rebuild_capie: CommandSpec
    commands: tuple[CommandSpec, ...]


@dataclass(frozen=True)
class BundleConfig:
    """Release ZIP layout (the format ``tools/bootstrap`` installs)."""

    zip_name: str
    launcher_zip_path: str
    files: tuple[BundleFile, ...]
    switch_mod: SwitchModConfig


@dataclass(frozen=True)
class SigningConfig:
    """Manifest signing and the installer that pins the release."""

    sign_script: str
    installer_configuration: str
    installer_build_script: str
    installer_script: str


@dataclass(frozen=True)
class GitHubConfig:
    """GitHub release target."""

    repo: str
    tag_prefix: str
    title: str
    changelog: str


@dataclass(frozen=True)
class VirusTotalConfig:
    """VirusTotal API use (the key itself is only read from the environment)."""

    api_key_env: str
    poll_seconds: float
    max_wait_seconds: float
    request_gap_seconds: float


@dataclass(frozen=True)
class WorkshopItem:
    """One Steam Workshop item."""

    key: str
    title: str
    content_dir: str
    preview_file: str
    description_file: str
    changelog: str
    published_file_id: int


@dataclass(frozen=True)
class WorkshopConfig:
    """Steam Workshop upload through SteamCMD."""

    app_id: int
    steamcmd: str
    steam_user: str
    exclude: tuple[str, ...]
    items: tuple[WorkshopItem, ...]


@dataclass(frozen=True)
class VersionSources:
    """Files that hold version numbers."""

    capie_header: str
    launcher_build_script: str
    launcher_config: str


@dataclass(frozen=True)
class ReleaseConfig:
    """The whole configuration with ``root`` resolved to an absolute path."""

    path: Path
    root: Path
    branch: str
    out_dir: str
    game_dir: str
    versions: VersionSources
    preflight: tuple[CommandSpec, ...]
    build: BuildConfig
    bundle: BundleConfig
    signing: SigningConfig
    github: GitHubConfig
    virustotal: VirusTotalConfig
    workshop: WorkshopConfig

    def resolve(self, relative: str) -> Path:
        """Turn a configured path into an absolute one.

        Args:
            relative: Path relative to the repository root (absolute paths stay as they are).

        Returns:
            The absolute path.
        """
        path = Path(relative)
        return path if path.is_absolute() else self.root / path

    def item(self, key: str) -> WorkshopItem:
        """Find a Workshop item by its key.

        Args:
            key: Item key from the configuration.

        Returns:
            The item.

        Raises:
            ConfigError: No item has that key.
        """
        for item in self.workshop.items:
            if item.key == key:
                return item
        known = ", ".join(i.key for i in self.workshop.items)
        msg = f"unknown Workshop item {key!r} (known: {known})"
        raise ConfigError(msg)


def _table(data: Mapping[str, object], key: str) -> dict[str, object]:
    """Read a sub-table.

    Args:
        data: Parent table.
        key: Key of the sub-table.

    Returns:
        The sub-table.

    Raises:
        ConfigError: Missing or not a table.
    """
    value = data.get(key)
    if not isinstance(value, dict):
        msg = f"[{key}] must be a table"
        raise ConfigError(msg)
    return {str(k): v for k, v in value.items()}


def _tables(data: Mapping[str, object], key: str) -> list[dict[str, object]]:
    """Read an array of tables (``[[key]]``); missing means empty.

    Args:
        data: Parent table.
        key: Key of the array.

    Returns:
        The tables in file order.

    Raises:
        ConfigError: Not an array of tables.
    """
    value = data.get(key, [])
    if not isinstance(value, list) or not all(isinstance(v, dict) for v in value):
        msg = f"[[{key}]] must be an array of tables"
        raise ConfigError(msg)
    return [{str(k): v for k, v in entry.items()} for entry in value]


def _string(data: Mapping[str, object], key: str, default: str | None = None) -> str:
    """Read a string value.

    Args:
        data: Table.
        key: Key.
        default: Value when the key is missing; ``None`` makes the key required.

    Returns:
        The string.

    Raises:
        ConfigError: Missing (without default) or not a string.
    """
    value = data.get(key, default)
    if not isinstance(value, str):
        msg = f"{key!r} must be a string"
        raise ConfigError(msg)
    return value


def _number(data: Mapping[str, object], key: str, default: float) -> float:
    """Read a number.

    Args:
        data: Table.
        key: Key.
        default: Value when the key is missing.

    Returns:
        The number.

    Raises:
        ConfigError: Not a number.
    """
    value = data.get(key, default)
    if isinstance(value, bool) or not isinstance(value, int | float):
        msg = f"{key!r} must be a number"
        raise ConfigError(msg)
    return float(value)


def _integer(data: Mapping[str, object], key: str, default: int) -> int:
    """Read a non-negative integer.

    Args:
        data: Table.
        key: Key.
        default: Value when the key is missing.

    Returns:
        The integer.

    Raises:
        ConfigError: Not a non-negative integer.
    """
    value = data.get(key, default)
    if isinstance(value, bool) or not isinstance(value, int) or value < 0:
        msg = f"{key!r} must be a non-negative integer"
        raise ConfigError(msg)
    return value


def _flag(data: Mapping[str, object], key: str) -> bool:
    """Read an optional boolean (default false).

    Args:
        data: Table.
        key: Key.

    Returns:
        The boolean.

    Raises:
        ConfigError: Not a boolean.
    """
    value = data.get(key, False)
    if not isinstance(value, bool):
        msg = f"{key!r} must be true or false"
        raise ConfigError(msg)
    return value


def _strings(data: Mapping[str, object], key: str) -> tuple[str, ...]:
    """Read an optional list of strings (default empty).

    Args:
        data: Table.
        key: Key.

    Returns:
        The strings.

    Raises:
        ConfigError: Not a list of strings.
    """
    value = data.get(key, [])
    if not isinstance(value, list) or not all(isinstance(v, str) for v in value):
        msg = f"{key!r} must be a list of strings"
        raise ConfigError(msg)
    return tuple(str(v) for v in value)


def _command(data: Mapping[str, object]) -> CommandSpec:
    """Build a command from its table.

    Args:
        data: Table with ``name``, ``argv`` and optional ``env``, ``clean_after``, ``slow``.

    Returns:
        The command.

    Raises:
        ConfigError: Bad value.
    """
    env_table = data.get("env", {})
    if not isinstance(env_table, dict) or not all(isinstance(v, str) for v in env_table.values()):
        msg = "'env' must be a table of strings"
        raise ConfigError(msg)
    argv = _strings(data, "argv")
    if not argv:
        msg = f"command {data.get('name')!r} has an empty argv"
        raise ConfigError(msg)
    return CommandSpec(
        name=_string(data, "name"),
        argv=argv,
        env=tuple((str(k), str(v)) for k, v in env_table.items()),
        clean_after=_strings(data, "clean_after"),
        slow=_flag(data, "slow"),
    )


def _workshop_item(data: Mapping[str, object]) -> WorkshopItem:
    """Build a Workshop item from its table.

    Args:
        data: One ``[[workshop.items]]`` table.

    Returns:
        The item.
    """
    return WorkshopItem(
        key=_string(data, "key"),
        title=_string(data, "title"),
        content_dir=_string(data, "content_dir"),
        preview_file=_string(data, "preview_file", ""),
        description_file=_string(data, "description_file", ""),
        changelog=_string(data, "changelog"),
        published_file_id=_integer(data, "published_file_id", 0),
    )


def parse_config(data: Mapping[str, object], path: Path) -> ReleaseConfig:
    """Validate parsed TOML and build the configuration.

    Args:
        data: Parsed TOML document.
        path: File the document came from (``root`` is relative to its folder).

    Returns:
        The configuration.
    """
    build = _table(data, "build")
    bundle = _table(data, "bundle")
    switch = _table(bundle, "switch_mod")
    signing = _table(data, "signing")
    github = _table(data, "github")
    virustotal = _table(data, "virustotal")
    workshop = _table(data, "workshop")
    versions = _table(data, "versions")
    return ReleaseConfig(
        path=path,
        root=(path.parent / _string(data, "root", ".")).resolve(),
        branch=_string(data, "branch", "main"),
        out_dir=_string(data, "out_dir", "dist/release"),
        game_dir=_string(data, "game_dir", ""),
        versions=VersionSources(
            capie_header=_string(versions, "capie_header"),
            launcher_build_script=_string(versions, "launcher_build_script"),
            launcher_config=_string(versions, "launcher_config"),
        ),
        preflight=tuple(_command(c) for c in _tables(_table(data, "preflight"), "commands")),
        build=BuildConfig(
            rebuild_output=_string(build, "rebuild_output"),
            reviewed_hashes=_string(build, "reviewed_hashes"),
            reviewed_files=_strings(build, "reviewed_files"),
            rebuild_capie=_command(_table(build, "rebuild_capie")),
            commands=tuple(_command(c) for c in _tables(build, "commands")),
        ),
        bundle=BundleConfig(
            zip_name=_string(bundle, "zip_name"),
            launcher_zip_path=_string(bundle, "launcher_zip_path"),
            files=tuple(
                BundleFile(
                    zip_path=_string(f, "zip"),
                    source=_string(f, "source"),
                    optional=_flag(f, "optional"),
                    reviewed=_flag(f, "reviewed"),
                )
                for f in _tables(bundle, "files")
            ),
            switch_mod=SwitchModConfig(
                source_dir=_string(switch, "source_dir"),
                zip_dir=_string(switch, "zip_dir"),
                descriptor_zip_path=_string(switch, "descriptor_zip_path"),
                descriptor_path_value=_string(switch, "descriptor_path_value"),
                descriptor_name=_string(switch, "descriptor_name"),
            ),
        ),
        signing=SigningConfig(
            sign_script=_string(signing, "sign_script"),
            installer_configuration=_string(signing, "installer_configuration"),
            installer_build_script=_string(signing, "installer_build_script"),
            installer_script=_string(signing, "installer_script"),
        ),
        github=GitHubConfig(
            repo=_string(github, "repo"),
            tag_prefix=_string(github, "tag_prefix", "v"),
            title=_string(github, "title"),
            changelog=_string(github, "changelog"),
        ),
        virustotal=VirusTotalConfig(
            api_key_env=_string(virustotal, "api_key_env", "VT_API_KEY"),
            poll_seconds=_number(virustotal, "poll_seconds", 20),
            max_wait_seconds=_number(virustotal, "max_wait_seconds", 600),
            request_gap_seconds=_number(virustotal, "request_gap_seconds", 15),
        ),
        workshop=WorkshopConfig(
            app_id=_integer(workshop, "app_id", 394360),
            steamcmd=_string(workshop, "steamcmd", ""),
            steam_user=_string(workshop, "steam_user", ""),
            exclude=_strings(workshop, "exclude"),
            items=tuple(_workshop_item(i) for i in _tables(workshop, "items")),
        ),
    )


def load_config(path: Path) -> ReleaseConfig:
    """Read and validate a configuration file.

    Args:
        path: ``release.toml``.

    Returns:
        The configuration.

    Raises:
        ConfigError: The file cannot be read or is invalid.
    """
    try:
        data = tomllib.loads(path.read_text(encoding="utf-8"))
    except (OSError, tomllib.TOMLDecodeError) as error:
        msg = f"cannot read {path}: {error}"
        raise ConfigError(msg) from error
    return parse_config(data, path.resolve())


_ITEM_HEADER = re.compile(r"^\[\[workshop\.items\]\]\s*$", re.MULTILINE)
"""Start of one Workshop item table in the configuration text."""

_ANY_HEADER = re.compile(r"^\[", re.MULTILINE)
"""Start of any table header (ends the current item table)."""

_ID_LINE = re.compile(r"^(published_file_id\s*=\s*)\d+", re.MULTILINE)
"""The ``published_file_id`` line of an item table."""


def set_published_file_id(text: str, key: str, published_file_id: int) -> str:
    """Store a Workshop item ID in the configuration text, keeping everything else.

    Args:
        text: Current contents of ``release.toml``.
        key: Item key.
        published_file_id: ID returned by Steam.

    Returns:
        The new contents.

    Raises:
        ConfigError: The item or its ``published_file_id`` line is not in the text.
    """
    for header in _ITEM_HEADER.finditer(text):
        start = header.end()
        following = _ANY_HEADER.search(text, start)
        end = following.start() if following else len(text)
        block = text[start:end]
        if not re.search(rf'^key\s*=\s*"{re.escape(key)}"\s*$', block, flags=re.MULTILINE):
            continue
        if not _ID_LINE.search(block):
            msg = f"item {key!r} has no published_file_id line"
            raise ConfigError(msg)
        new_block = _ID_LINE.sub(rf"\g<1>{published_file_id}", block, count=1)
        return text[:start] + new_block + text[end:]
    msg = f"item {key!r} not found in the configuration"
    raise ConfigError(msg)
