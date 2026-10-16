"""Prepare external HOI4 launcher settings; never start a process or change source settings."""

from __future__ import annotations

import argparse
import copy
import json
import os
import re
from pathlib import Path


def absolute(path: str | Path) -> Path:
    """Require an absolute filesystem path without loader/control delimiters.

    Returns:
        The validated and normalized result.

    Raises:
        ValueError: If external configuration cannot be handled safely.
    """
    value = str(path)
    if not value.startswith("/") or any(c in value for c in "\x00\r\n:"):
        msg = "expected an absolute path without control characters or colon"
        raise ValueError(msg)
    return Path(os.path.normpath(value))


def resource(value: object, game: Path) -> Path:
    """Resolve one game-relative resource without interpreting shell or user expansions.

    Returns:
        The validated and normalized result.

    Raises:
        ValueError: If external configuration cannot be handled safely.
    """
    if not isinstance(value, str) or not value or value.startswith("~"):
        msg = "unsupported game resource path"
        raise ValueError(msg)
    return absolute(value if value.startswith("/") else game / value)


def runtime_arguments(runtime: tuple[Path, str] | None, library: Path) -> list[str]:
    """Validate optional paired runtime settings.

    Returns:
        The optional child configuration arguments.

    Raises:
        ValueError: If the runtime path or digest is invalid.
    """
    pair_args = []
    if runtime is not None:
        runtime_path = absolute(runtime[0])
        if runtime_path == library or not re.fullmatch("[0-9a-f]{64}", runtime[1]):
            msg = "runtime must be distinct with a valid lowercase SHA-256"
            raise ValueError(msg)
        pair_args = ["--capie-runtime", str(runtime_path), runtime[1]]
    return pair_args


def prepare(  # noqa: PLR0913, PLR0917 -- explicit staging inputs
    settings: dict,
    game: Path,
    home: Path,
    child: Path,
    library: Path,
    digest: str,
    runtime: tuple[Path, str] | None = None,
) -> dict:
    """Preserve settings and map each supported game launch through the same child gate.

    Returns:
        The validated and normalized result.

    Raises:
        ValueError: If external configuration cannot be handled safely.
    """
    game, home, child, library = map(absolute, (game, home, child, library))
    if not re.fullmatch("[0-9a-f]{64}", digest):
        msg = "library digest must be lowercase SHA-256"
        raise ValueError(msg)
    if not isinstance(settings, dict) or any(
        settings.get(k) != v
        for k, v in {
            "gameId": "hoi4",
            "distPlatform": "steam",
            "rawVersion": "1.19.3.0",
        }.items()
    ):
        msg = "unsupported game settings identity/version"
        raise ValueError(msg)
    pair_args = runtime_arguments(runtime, library)
    out = copy.deepcopy(settings)
    data = out.get("gameDataPath")
    if not isinstance(data, str):
        msg = "missing gameDataPath"
        raise ValueError(msg)  # noqa: TRY004 -- invalid external configuration is a value error
    data = absolute(home / data[2:] if data.startswith("~/") else data)
    out["gameDataPath"] = str(data)
    game_exe = game / "hoi4.app/Contents/MacOS/hoi4"
    alternatives = out.get("alternativeExecutables", [])
    if not isinstance(alternatives, list):
        msg = "invalid alternativeExecutables"
        raise ValueError(msg)  # noqa: TRY004 -- invalid external configuration is a value error
    for mode in [out, *alternatives]:
        if not isinstance(mode, dict) or resource(mode.get("exePath"), game) != game_exe:
            msg = "unsupported executable; no launch mode was dropped"
            raise ValueError(msg)
        args = mode.get("exeArgs", [])
        if not isinstance(args, list) or any(
            not isinstance(arg, str) or "\x00" in arg or arg == "--capie-dev-child" for arg in args
        ):
            msg = "invalid or ambiguous game arguments"
            raise ValueError(msg)
        mode["exePath"] = str(child)
        mode["exeArgs"] = [
            "--capie-dev-child",
            str(game_exe),
            str(game_exe.parent),
            str(data / "dlc_load.json"),
            str(library),
            digest,
            *pair_args,
            "--",
            *args,
        ]
    for field in ("dlcPath", "themeFile", "ingameSettingsLayoutPath"):
        if field in out:
            out[field] = str(resource(out[field], game))
    return out


def write_new(destination: Path, settings: dict) -> None:
    """Write a fresh private staging directory, refusing every existing destination."""
    destination = absolute(destination)
    destination.mkdir(mode=0o700)
    with (destination / "launcher-settings.json").open("x", encoding="utf-8") as stream:
        json.dump(settings, stream, ensure_ascii=False, indent=2)
        stream.write("\n")


def unique_object(pairs: list[tuple[str, object]]) -> dict:
    """Reject duplicate JSON keys instead of silently selecting a configuration value.

    Returns:
        The validated and normalized result.

    Raises:
        ValueError: If external configuration cannot be handled safely.
    """
    result = {}
    for key, value in pairs:
        if key in result:
            msg = "duplicate JSON setting"
            raise ValueError(msg)
        result[key] = value
    return result


def main() -> None:
    """Stage only; launching is a separate action.

    Raises:
        ValueError: If settings or destination are invalid.
    """
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("game", type=Path)
    parser.add_argument("child", type=Path)
    parser.add_argument("library", type=Path)
    parser.add_argument("library_sha256")
    parser.add_argument("destination", type=Path)
    parser.add_argument("--capie-runtime", nargs=2, metavar=("PATH", "SHA256"))
    args = parser.parse_args()
    game = absolute(args.game).resolve(strict=True)
    destination = absolute(args.destination).resolve()
    if destination == game or game in destination.parents:
        msg = "staging must remain outside the game directory"
        raise ValueError(msg)
    with (game / "launcher-settings.json").open("rb") as stream:
        raw = stream.read(1024 * 1024 + 1)
    if len(raw) > 1024 * 1024:
        msg = "oversized settings"
        raise ValueError(msg)
    settings = json.loads(raw.decode("utf-8-sig"), object_pairs_hook=unique_object)
    runtime = (Path(args.capie_runtime[0]), args.capie_runtime[1]) if args.capie_runtime else None
    staged = prepare(
        settings, game, Path.home(), args.child, args.library, args.library_sha256, runtime
    )
    data = Path(staged["gameDataPath"]).resolve()
    if destination == data or data in destination.parents:
        msg = "staging must remain outside game user data"
        raise ValueError(msg)
    write_new(destination, staged)
    print(f"Prepared: {destination / 'launcher-settings.json'}")
    print("No process started; real playset remains unchanged.")


if __name__ == "__main__":
    main()
