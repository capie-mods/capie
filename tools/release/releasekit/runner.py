"""Starting external processes: the ``CommandRunner`` protocol and its real adapter.

Every process of the tool (git, gh, pwsh, steamcmd, the Python checks) starts here, so
the tests replace one object and nothing real runs. Two modes:

* :meth:`CommandRunner.run` captures the output (and streams it line by line to a
  callback, so a ten-minute native build still shows progress).
* :meth:`CommandRunner.run_interactive` gives the child the console and captures
  nothing. SteamCMD runs this way: the password and the Steam Guard code are typed
  into SteamCMD itself and never pass through this process or its log.

:func:`expand_command` fills ``{placeholders}`` in configured commands.
"""

from __future__ import annotations

import os
import shutil
import string
import subprocess
from dataclasses import dataclass
from typing import TYPE_CHECKING, Protocol

if TYPE_CHECKING:
    from collections.abc import Callable, Mapping, Sequence
    from pathlib import Path

    from releasekit.config import CommandSpec


@dataclass(frozen=True)
class CommandResult:
    """Exit code and combined output of a finished command."""

    argv: tuple[str, ...]
    returncode: int
    output: str

    @property
    def ok(self) -> bool:
        """True when the exit code is 0.

        Returns:
            True when the exit code is 0.
        """
        return self.returncode == 0


class CommandRunner(Protocol):
    """Starts processes."""

    def run(
        self,
        argv: Sequence[str],
        *,
        cwd: Path,
        env: Mapping[str, str] | None = None,
        on_line: Callable[[str], None] | None = None,
    ) -> CommandResult:
        """Run a command and capture stdout and stderr together.

        Args:
            argv: Program and arguments (no shell).
            cwd: Working folder.
            env: Extra environment variables on top of the current ones.
            on_line: Called with every output line while the command runs.

        Returns:
            The result.
        """
        ...

    def run_interactive(self, argv: Sequence[str], *, cwd: Path) -> int:
        """Run a command on the console without capturing anything.

        Args:
            argv: Program and arguments (no shell).
            cwd: Working folder.

        Returns:
            The exit code.
        """
        ...

    def which(self, program: str) -> str | None:
        """Find a program on ``PATH``.

        Args:
            program: Program name or path.

        Returns:
            The full path, or ``None`` when it is not found.
        """
        ...


class SubprocessRunner:
    """The real :class:`CommandRunner`."""

    def run(
        self,
        argv: Sequence[str],
        *,
        cwd: Path,
        env: Mapping[str, str] | None = None,
        on_line: Callable[[str], None] | None = None,
    ) -> CommandResult:
        """Run a command and capture stdout and stderr together.

        Args:
            argv: Program and arguments (no shell).
            cwd: Working folder.
            env: Extra environment variables on top of the current ones.
            on_line: Called with every output line while the command runs.

        Returns:
            The result; a program that cannot be started gives exit code 127.
        """
        merged = {**os.environ, **(env or {})}
        lines: list[str] = []
        try:
            process = subprocess.Popen(  # noqa: S603 - argv list from the reviewed config, no shell
                list(argv),
                cwd=cwd,
                env=merged,
                stdin=subprocess.DEVNULL,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                encoding="utf-8",
                errors="replace",
            )
        except OSError as error:
            return CommandResult(tuple(argv), 127, f"cannot start {argv[0]}: {error}")
        with process:
            stdout = process.stdout
            if stdout is not None:
                for line in stdout:
                    lines.append(line)
                    if on_line is not None:
                        on_line(line)
            returncode = process.wait()
        return CommandResult(tuple(argv), returncode, "".join(lines))

    def run_interactive(self, argv: Sequence[str], *, cwd: Path) -> int:
        """Run a command on the console without capturing anything.

        Args:
            argv: Program and arguments (no shell).
            cwd: Working folder.

        Returns:
            The exit code; 127 when the program cannot be started.
        """
        try:
            completed = subprocess.run(list(argv), cwd=cwd, check=False)  # noqa: S603 - no shell
        except OSError:
            return 127
        return completed.returncode

    def which(self, program: str) -> str | None:
        """Find a program on ``PATH``.

        Args:
            program: Program name or path.

        Returns:
            The full path, or ``None`` when it is not found.
        """
        return shutil.which(program)


def placeholders(text: str) -> set[str]:
    """List the ``{name}`` placeholders of a string.

    Args:
        text: Template.

    Returns:
        Placeholder names.
    """
    return {
        field for _, field, _, _ in string.Formatter().parse(text) if field is not None and field
    }


@dataclass(frozen=True)
class ExpandedCommand:
    """A configured command with its placeholders filled, or the reason it cannot run."""

    name: str
    argv: tuple[str, ...]
    env: dict[str, str]
    missing: tuple[str, ...]


def expand_command(spec: CommandSpec, values: Mapping[str, str | None]) -> ExpandedCommand:
    """Fill the placeholders of a configured command.

    Args:
        spec: Command from the configuration.
        values: Placeholder values; ``None`` means "not available on this machine".

    Returns:
        The command; ``missing`` lists placeholders without a value (the command is then
        skipped, never run with a hole in it).
    """
    texts = [*spec.argv, *(value for _, value in spec.env)]
    needed = set().union(*(placeholders(text) for text in texts))
    missing = tuple(sorted(name for name in needed if values.get(name) is None))
    if missing:
        return ExpandedCommand(spec.name, spec.argv, dict(spec.env), missing)
    filled = {name: str(values[name]) for name in needed}
    return ExpandedCommand(
        spec.name,
        tuple(arg.format_map(filled) for arg in spec.argv),
        {key: value.format_map(filled) for key, value in spec.env},
        (),
    )


def quote(argv: Sequence[str]) -> str:
    """Render a command line for display (PowerShell-friendly quoting).

    Args:
        argv: Program and arguments.

    Returns:
        One line; arguments with spaces or quotes are wrapped in single quotes.
    """
    rendered = []
    for arg in argv:
        if arg and not any(ch in arg for ch in " '\"\t;&|"):
            rendered.append(arg)
        else:
            rendered.append("'" + arg.replace("'", "''") + "'")
    return " ".join(rendered)
