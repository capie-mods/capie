"""What every step receives: configuration, options, adapters and derived values."""

from __future__ import annotations

import sys
import tempfile
from dataclasses import dataclass, field
from pathlib import Path
from typing import TYPE_CHECKING

from releasekit.console import Status
from releasekit.runner import expand_command, quote
from releasekit.versions import read_capie_versions

if TYPE_CHECKING:
    from collections.abc import Callable, Mapping

    from releasekit.config import CommandSpec, ReleaseConfig
    from releasekit.console import Reporter
    from releasekit.prompts import Prompter
    from releasekit.runner import CommandResult, CommandRunner
    from releasekit.versions import CapieVersions
    from releasekit.virustotal import HttpTransport

GAME_EXE_NAME = "hoi4.exe"
"""Game executable inside the game folder."""


@dataclass
class Options:
    """Command-line options (one object for every step; unused ones keep their defaults)."""

    dry_run: bool = False
    yes: bool = False
    verbose: bool = False
    game_dir: str = ""
    minhook_dir: str = ""
    psscriptanalyzer_dir: str = ""
    skip_slow: bool = False
    signing_key: str = ""
    rebuild_capie: bool = False
    write_pins: bool = False
    repo: str = ""
    publish: bool = False
    stable: bool = False
    update_notes: bool = False
    api_key_env: str = ""
    items: tuple[str, ...] = ()
    steam_user: str = ""
    steamcmd: str = ""
    visibility: str = ""
    update_description: bool = False


@dataclass
class Context:
    """Shared state of one run."""

    config: ReleaseConfig
    options: Options
    runner: CommandRunner
    reporter: Reporter
    prompter: Prompter
    transport: HttpTransport
    env: Mapping[str, str]
    sleep: Callable[[float], None]
    _capie: CapieVersions | None = field(default=None, init=False)

    @property
    def capie(self) -> CapieVersions:
        """CAPIE version constants (read once from ``game_build.hpp``).

        Returns:
            CAPIE version constants (read once from ``game_build.hpp``).
        """
        if self._capie is None:
            header = self.config.resolve(self.config.versions.capie_header)
            self._capie = read_capie_versions(header.read_text(encoding="utf-8"))
        return self._capie

    @property
    def version(self) -> str:
        """Release version of the CAPIE bundle (``capie_release``).

        Returns:
            Release version of the CAPIE bundle (``capie_release``).
        """
        return self.capie.release

    @property
    def repo(self) -> str:
        """GitHub repository of the release (option, else configuration).

        Returns:
            GitHub repository of the release (option, else configuration).
        """
        return self.options.repo or self.config.github.repo

    @property
    def tag(self) -> str:
        """Release tag (``v`` + version).

        Returns:
            Release tag (``v`` + version).
        """
        return self.config.github.tag_prefix + self.version

    @property
    def out_dir(self) -> Path:
        """Output folder of this version (``dist/release/<version>``).

        Returns:
            Output folder of this version (``dist/release/<version>``).
        """
        return self.config.resolve(self.config.out_dir) / self.version

    @property
    def game_dir(self) -> Path | None:
        """Game folder when ``hoi4.exe`` is there, else ``None``.

        Returns:
            Game folder when ``hoi4.exe`` is there, else ``None``.
        """
        configured = self.options.game_dir or self.config.game_dir
        if not configured:
            return None
        folder = Path(configured)
        return folder if (folder / GAME_EXE_NAME).is_file() else None

    def placeholder_values(self) -> dict[str, str | None]:
        """Values of the ``{placeholders}`` in configured commands.

        Returns:
            Values by name; ``None`` marks one that is not available here.
        """
        game = self.game_dir
        return {
            "python": sys.executable,
            "pwsh": "pwsh",
            "root": str(self.config.root),
            "temp": tempfile.gettempdir(),
            "out_dir": str(self.out_dir),
            "game_dir": str(game) if game else None,
            "game_exe": str(game / GAME_EXE_NAME) if game else None,
            "minhook_dir": self.options.minhook_dir or None,
            "rebuild_output": self.config.build.rebuild_output,
            "psscriptanalyzer_dir": self.options.psscriptanalyzer_dir or None,
        }

    def run_spec(self, step: str, spec: CommandSpec) -> CommandResult | None:
        """Run a configured command, or show it in a dry run.

        Missing placeholders skip the command with a ``SKIP`` line that names them.

        Args:
            step: Step name for the summary.
            spec: Command.

        Returns:
            The result, or ``None`` when the command was skipped or only shown.
        """
        command = expand_command(spec, self.placeholder_values())
        if command.missing:
            needs = ", ".join(command.missing)
            self.reporter.record(step, Status.SKIP, f"{spec.name}: needs {needs}")
            return None
        if spec.slow and self.options.skip_slow:
            self.reporter.record(
                step, Status.SKIP, f"{spec.name}: slow check skipped (--skip-slow)"
            )
            return None
        if self.options.dry_run:
            self.reporter.line(Status.PLAN, f"{spec.name}: {quote(command.argv)}")
            return None
        self.reporter.line(Status.NOTE, f"{spec.name}: {quote(command.argv)}")
        return self.runner.run(
            command.argv,
            cwd=self.config.root,
            env=command.env,
            on_line=lambda line: self.reporter.detail(line, echo=self.options.verbose),
        )
