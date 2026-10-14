"""Command line: sub-commands, options and the order of the steps.

``all`` runs preflight, build, VirusTotal (only with ``--virustotal``), the GitHub
release and the Workshop upload (unless ``--skip-workshop``), and stops at the first
failed step. A dry run never stops early, so it shows the whole plan.
"""

from __future__ import annotations

import argparse
import os
import sys
import time
from datetime import UTC, datetime
from pathlib import Path
from typing import TYPE_CHECKING, cast

from releasekit import github, package, preflight, virustotal, workshop
from releasekit.config import DEFAULT_CONFIG, VISIBILITY_VALUES, ConfigError, load_config
from releasekit.console import Reporter, Status, use_color
from releasekit.context import Context, Options
from releasekit.prompts import ConsolePrompter
from releasekit.runner import SubprocessRunner
from releasekit.versions import VersionError

if TYPE_CHECKING:
    from collections.abc import Callable, Mapping, Sequence
    from typing import TextIO

    from releasekit.prompts import Prompter
    from releasekit.runner import CommandRunner
    from releasekit.virustotal import HttpTransport

EXIT_USAGE = 2
"""Exit code for a configuration or usage error."""

STEPS: dict[str, Callable[[Context], bool]] = {
    "preflight": preflight.run,
    "build": package.run,
    "virustotal": virustotal.run,
    "release": github.run,
    "workshop": workshop.run,
}
"""Step functions by sub-command name, in the order ``all`` runs them."""


def _common(parser: argparse.ArgumentParser) -> None:
    """Options every sub-command accepts.

    Args:
        parser: Sub-command parser.
    """
    parser.add_argument(
        "--config",
        type=Path,
        default=DEFAULT_CONFIG,
        help="release.toml (default: next to release.py)",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="show commands and files; send, publish and change nothing",
    )
    parser.add_argument(
        "--yes",
        action="store_true",
        help="skip the plain 'continue?' question (never the typed publish confirmations)",
    )
    parser.add_argument(
        "--verbose", action="store_true", help="echo tool output on the console (always in the log)"
    )
    parser.add_argument(
        "--no-color", action="store_true", help="no ANSI colours (also: NO_COLOR=1)"
    )


def _preflight_options(parser: argparse.ArgumentParser) -> None:
    """Options of the preflight step.

    Args:
        parser: Sub-command parser.
    """
    group = parser.add_argument_group("preflight")
    group.add_argument(
        "--game-dir",
        default="",
        help="HOI4 folder (default from release.toml); checks needing hoi4.exe skip without it",
    )
    group.add_argument(
        "--minhook-dir",
        default="",
        help="MinHook checkout for the native build and its offline tests",
    )
    group.add_argument(
        "--psscriptanalyzer-dir",
        default="",
        help="folder from 'Save-Module PSScriptAnalyzer -Path <dir>'",
    )
    group.add_argument(
        "--skip-slow", action="store_true", help="skip checks marked slow (native build)"
    )


def _build_options(parser: argparse.ArgumentParser) -> None:
    """Options of the build step.

    Args:
        parser: Sub-command parser.
    """
    group = parser.add_argument_group("build")
    group.add_argument(
        "--signing-key",
        default="",
        help="private key XML OUTSIDE the repository (sign-release-manifest.ps1 -NewKey)",
    )
    group.add_argument(
        "--rebuild-capie",
        action="store_true",
        help="run build.ps1 first and stop if the new DLL hashes need re-pinning",
    )
    group.add_argument(
        "--write-pins",
        action="store_true",
        help="write the release pins into the installer and rebuild install.ps1",
    )


def _release_options(parser: argparse.ArgumentParser) -> None:
    """Options of the GitHub release step.

    Args:
        parser: Sub-command parser.
    """
    group = parser.add_argument_group("release")
    group.add_argument("--repo", default="", help="GitHub owner/repo (default from release.toml)")
    group.add_argument(
        "--publish", action="store_true", help="publish instead of a draft (typed confirmation)"
    )
    group.add_argument(
        "--stable", action="store_true", help="not a pre-release (typed confirmation)"
    )


def _virustotal_options(parser: argparse.ArgumentParser) -> None:
    """Options of the VirusTotal step.

    Args:
        parser: Sub-command parser.
    """
    group = parser.add_argument_group("virustotal")
    group.add_argument(
        "--api-key-env",
        default="",
        help="environment variable holding the API key (default from release.toml)",
    )
    group.add_argument(
        "--update-notes",
        action="store_true",
        help="also refresh the notes of the existing GitHub release",
    )


def _workshop_options(parser: argparse.ArgumentParser) -> None:
    """Options of the Workshop step.

    Args:
        parser: Sub-command parser.
    """
    group = parser.add_argument_group("workshop")
    group.add_argument(
        "--item", action="append", default=[], help="Workshop item key (repeatable; default: all)"
    )
    group.add_argument(
        "--steam-user", default="", help="Steam account NAME; SteamCMD asks for the password itself"
    )
    group.add_argument(
        "--steamcmd", default="", help="steamcmd.exe (default: from release.toml or PATH)"
    )
    group.add_argument(
        "--visibility",
        default="",
        choices=sorted(VISIBILITY_VALUES),
        help="new items default to hidden, existing items keep theirs; "
        "others need a typed confirmation",
    )
    group.add_argument(
        "--update-description",
        action="store_true",
        help="also replace the title and description of existing items",
    )


def build_parser() -> argparse.ArgumentParser:
    """The argument parser.

    Returns:
        Parser with one sub-command per step plus ``all``.
    """
    parser = argparse.ArgumentParser(
        prog="release.py",
        description="Local release of CAPIE and its mods: "
        "preflight, build, GitHub release, VirusTotal, Steam Workshop.",
    )
    commands = parser.add_subparsers(dest="command", required=True, metavar="COMMAND")
    adders = {
        "preflight": (_preflight_options,),
        "build": (_preflight_options, _build_options),
        "release": (_release_options,),
        "virustotal": (_virustotal_options, _release_options),
        "workshop": (_workshop_options,),
    }
    helps = {
        "preflight": "clean tree, versions, tests and lint",
        "build": "reviewed binaries, release ZIP, signed manifest",
        "release": "GitHub release (draft pre-release by default)",
        "virustotal": "scan release files (asks before uploading)",
        "workshop": "upload mods with SteamCMD (hidden by default)",
    }
    for name, groups in adders.items():
        sub = commands.add_parser(name, help=helps[name])
        _common(sub)
        for add in groups:
            add(sub)
    every = commands.add_parser("all", help="every step in order")
    _common(every)
    for add in (
        _preflight_options,
        _build_options,
        _release_options,
        _virustotal_options,
        _workshop_options,
    ):
        add(every)
    every.add_argument("--virustotal", action="store_true", help="include the VirusTotal step")
    every.add_argument("--skip-workshop", action="store_true", help="leave out the Workshop step")
    return parser


def options_from(args: argparse.Namespace) -> Options:
    """Copy parsed arguments into :class:`Options` (absent ones keep their defaults).

    Args:
        args: Parsed arguments.

    Returns:
        The options.
    """
    values = vars(args)
    fields = Options.__dataclass_fields__
    options = Options(
        **{key: value for key, value in values.items() if key in fields and key != "items"}
    )
    options.items = tuple(values.get("item", []))
    return options


def steps_for(args: argparse.Namespace) -> list[str]:
    """Steps a sub-command runs.

    Args:
        args: Parsed arguments.

    Returns:
        Step names in order.
    """
    if args.command != "all":
        return [args.command]
    steps = ["preflight", "build"]
    if args.virustotal:
        steps.append("virustotal")
    steps.append("release")
    if not args.skip_workshop:
        steps.append("workshop")
    return steps


def main(  # noqa: PLR0913 - every adapter is injectable for the tests
    argv: Sequence[str] | None = None,
    *,
    runner: CommandRunner | None = None,
    prompter: Prompter | None = None,
    transport: HttpTransport | None = None,
    env: Mapping[str, str] | None = None,
    stream: TextIO | None = None,
    sleep: Callable[[float], None] = time.sleep,
) -> int:
    """Run the tool.

    Args:
        argv: Arguments (default: ``sys.argv[1:]``).
        runner: Process adapter.
        prompter: Question adapter.
        transport: HTTP adapter.
        env: Environment variables.
        stream: Console stream.
        sleep: Wait function (VirusTotal rate limits).

    Returns:
        Exit code: 0 success, 1 a step failed, 2 bad configuration.
    """
    args = build_parser().parse_args(argv)
    env = os.environ if env is None else env
    stream = cast("TextIO", sys.stdout) if stream is None else stream
    try:
        config = load_config(args.config)
    except ConfigError as error:
        stream.write(f"FAIL  {error}\n")
        return EXIT_USAGE
    options = options_from(args)
    stamp = datetime.now(UTC).strftime("%Y%m%d-%H%M%S")
    log = (
        config.resolve(config.out_dir)
        / "logs"
        / f"{stamp}-{args.command}{'-dry-run' if options.dry_run else ''}.log"
    )
    reporter = Reporter(stream, color=use_color(stream, env, disabled=args.no_color), log_path=log)
    ctx = Context(
        config=config,
        options=options,
        runner=runner or SubprocessRunner(),
        reporter=reporter,
        prompter=prompter or ConsolePrompter(),
        transport=transport or virustotal.UrllibTransport(),
        env=env,
        sleep=sleep,
    )
    try:
        reporter.line(Status.NOTE, f"CAPIE {ctx.version}, repository {config.root}")
    except (OSError, VersionError) as error:
        reporter.line(Status.FAIL, f"cannot read the CAPIE version: {error}")
        return EXIT_USAGE
    if options.dry_run:
        reporter.line(
            Status.PLAN,
            "dry run: nothing is sent, published, uploaded or changed in the repository",
        )
    for name in steps_for(args):
        if not STEPS[name](ctx) and not options.dry_run:
            reporter.record(name, Status.FAIL, f"stopped after the {name} step")
            break
    return reporter.summary(dry_run=options.dry_run)
