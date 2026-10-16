"""Development parent: stage settings, then exec original dowser with inherited environment."""

import importlib.util
import os
import subprocess
import sys
from pathlib import Path


def command(game: Path, staging: Path, processes: str) -> list[str]:
    """Build the launcher command only when no existing launcher is observed.

    Returns:
        Separate argv elements for the original bootstrapper.

    Raises:
        RuntimeError: If an existing Paradox Launcher could receive the request.
        ValueError: If either directory is not absolute.
    """
    if not game.is_absolute() or not staging.is_absolute():
        message = "game and staging paths must be absolute"
        raise ValueError(message)
    if any(
        line.strip().endswith("/Contents/MacOS/Paradox Launcher") for line in processes.splitlines()
    ):
        message = "Paradox Launcher is already running; no process was stopped"
        raise RuntimeError(message)
    return [str(game / "dowser"), "--pdxlGameDir", str(staging), "--gameDir", str(staging)]


def main() -> None:
    """Use the staging CLI contract and never alter existing launcher/game processes.

    Raises:
        RuntimeError: If process inspection or the launcher-instance gate fails.
        ValueError: If the optional runtime arguments are malformed.
    """
    spec = importlib.util.spec_from_file_location(
        "staging", Path(__file__).with_name("stage-macos-child.py")
    )
    staging = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(staging)
    if len(sys.argv) not in (6, 9):
        staging.main()  # Its argparse usage explains the shared positional contract.
        return
    if len(sys.argv) == 9 and sys.argv[6] != "--capie-runtime":  # noqa: PLR2004 -- optional trailing triple
        message = "optional --capie-runtime PATH SHA256 must follow the five positional arguments"
        raise ValueError(message)
    process_list = subprocess.run(
        ["/bin/ps", "-axo", "comm="],
        capture_output=True,
        text=True,
        check=False,
    )
    if process_list.returncode != 0:
        message = "Cannot inspect launcher instances; nothing was launched"
        raise RuntimeError(message)
    game = Path(sys.argv[1]).resolve(strict=True)
    destination = Path(sys.argv[5]).resolve()
    argv = command(game, destination, process_list.stdout)
    staging.main()
    os.chdir(game)
    os.execv(argv[0], argv)  # noqa: S606 -- original dowser, separate argv; environment inherited


if __name__ == "__main__":
    main()
