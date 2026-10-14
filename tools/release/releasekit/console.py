"""Status lines, the log file and the final summary.

Every message goes through :class:`Reporter`: one coloured status label per line
(``OK``, ``WARN``, ``FAIL``, ``SKIP``, ``PLAN``, ``NOTE``, the same words as the
bootstrap installer) on the console, and the same text with a timestamp in the log
file. Colour is off when ``NO_COLOR`` is set to anything, ``TERM`` is ``dumb``, the
stream is not a terminal or ``--no-color`` is given.

Values registered with :meth:`Reporter.redact` (the VirusTotal API key) are replaced
by ``***`` before anything is printed or logged.
"""

from __future__ import annotations

from dataclasses import dataclass
from datetime import UTC, datetime
from enum import StrEnum
from typing import TYPE_CHECKING, TextIO

if TYPE_CHECKING:
    from collections.abc import Callable, Mapping
    from pathlib import Path


class Status(StrEnum):
    """Result of one check or action."""

    OK = "OK"
    WARN = "WARN"
    FAIL = "FAIL"
    SKIP = "SKIP"
    PLAN = "PLAN"
    NOTE = "NOTE"


COLORS = {
    Status.OK: "\x1b[32m",
    Status.WARN: "\x1b[33m",
    Status.FAIL: "\x1b[31m",
    Status.SKIP: "\x1b[90m",
    Status.PLAN: "\x1b[36m",
    Status.NOTE: "\x1b[34m",
}
"""ANSI colour of each status label."""

BOLD = "\x1b[1m"
"""ANSI bold, used for step headings."""

RESET = "\x1b[0m"
"""ANSI reset."""

LABEL_WIDTH = 5
"""Width of the status label column."""

REDACTED = "***"
"""Replacement text of a redacted value."""


def use_color(stream: TextIO, env: Mapping[str, str], *, disabled: bool) -> bool:
    """Decide whether to print ANSI colours.

    Args:
        stream: Output stream.
        env: Environment variables.
        disabled: ``--no-color`` was given.

    Returns:
        True when colours should be used.
    """
    if disabled or env.get("NO_COLOR", "") != "" or env.get("TERM", "") == "dumb":
        return False
    isatty = getattr(stream, "isatty", None)
    return bool(isatty and isatty())


@dataclass(frozen=True)
class Outcome:
    """One line of the final summary."""

    step: str
    status: Status
    detail: str


def _now() -> datetime:
    """Current time in UTC (the default clock).

    Returns:
        The current time.
    """
    return datetime.now(UTC)


class Reporter:
    """Console output, log file and the summary of one run."""

    def __init__(
        self,
        stream: TextIO,
        *,
        color: bool,
        log_path: Path | None = None,
        clock: Callable[[], datetime] = _now,
    ) -> None:
        """Set up the reporter.

        Args:
            stream: Console stream.
            color: Use ANSI colours.
            log_path: Log file (appended; its folder is created), or ``None`` for no log.
            clock: Time source for log timestamps.
        """
        self._stream = stream
        self._color = color
        self._log_path = log_path
        self._clock = clock
        self._secrets: list[str] = []
        self.outcomes: list[Outcome] = []
        if log_path is not None:
            log_path.parent.mkdir(parents=True, exist_ok=True)

    @property
    def log_path(self) -> Path | None:
        """Log file of this run, if any.

        Returns:
            Log file of this run, if any.
        """
        return self._log_path

    def redact(self, secret: str) -> None:
        """Never print or log this value.

        Args:
            secret: Value to hide (empty values are ignored).
        """
        if secret:
            self._secrets.append(secret)

    def _clean(self, text: str) -> str:
        """Replace registered secrets.

        Args:
            text: Text to print or log.

        Returns:
            The text without secrets.
        """
        for secret in self._secrets:
            text = text.replace(secret, REDACTED)
        return text

    def _log(self, text: str) -> None:
        """Append one line to the log file.

        Args:
            text: Line without timestamp.
        """
        if self._log_path is None:
            return
        stamp = self._clock().isoformat(timespec="seconds")
        with self._log_path.open("a", encoding="utf-8") as log:
            log.write(f"{stamp} {text}\n")

    def _paint(self, code: str, text: str) -> str:
        """Wrap text in an ANSI code when colours are on.

        Args:
            code: ANSI start sequence.
            text: Text.

        Returns:
            The (possibly) coloured text.
        """
        return f"{code}{text}{RESET}" if self._color else text

    def heading(self, text: str) -> None:
        """Print a step heading.

        Args:
            text: Heading.
        """
        text = self._clean(text)
        self._stream.write("\n" + self._paint(BOLD, f"== {text}") + "\n")
        self._stream.flush()
        self._log(f"== {text}")

    def line(self, status: Status, text: str) -> None:
        """Print one status line.

        Args:
            status: Label.
            text: Message.
        """
        text = self._clean(text)
        label = self._paint(COLORS[status], status.value.ljust(LABEL_WIDTH))
        self._stream.write(f"  {label} {text}\n")
        self._stream.flush()
        self._log(f"{status.value.ljust(LABEL_WIDTH)} {text}")

    def detail(self, text: str, *, echo: bool) -> None:
        """Record tool output: always in the log, on the console only when asked.

        Args:
            text: One output line.
            echo: Also print it on the console.
        """
        text = self._clean(text.rstrip("\r\n"))
        if echo:
            self._stream.write(f"        {text}\n")
            self._stream.flush()
        self._log(f"      | {text}")

    def record(self, step: str, status: Status, detail: str) -> None:
        """Print a status line and keep it for the summary.

        Args:
            step: Step name (``preflight``, ``build``...).
            status: Result.
            detail: Message.
        """
        self.line(status, detail)
        self.outcomes.append(Outcome(step, status, self._clean(detail)))

    def failed(self, step: str | None = None) -> bool:
        """Tell whether anything failed.

        Args:
            step: Only look at this step; ``None`` means all steps.

        Returns:
            True when at least one ``FAIL`` was recorded.
        """
        return any(
            o.status is Status.FAIL and (step is None or o.step == step) for o in self.outcomes
        )

    def summary(self, *, dry_run: bool) -> int:
        """Print the summary table and return the exit code.

        Args:
            dry_run: The run was a dry run (stated in the summary).

        Returns:
            0 when nothing failed, 1 otherwise.
        """
        self.heading("Summary" + (" (dry run: nothing was sent or published)" if dry_run else ""))
        counts: dict[Status, int] = {}
        for outcome in self.outcomes:
            counts[outcome.status] = counts.get(outcome.status, 0) + 1
            if outcome.status in {Status.FAIL, Status.WARN, Status.NOTE}:
                self.line(outcome.status, f"[{outcome.step}] {outcome.detail}")
        totals = ", ".join(f"{status.value} {counts.get(status, 0)}" for status in Status)
        self.line(Status.FAIL if self.failed() else Status.OK, totals)
        if self._log_path is not None:
            self.line(Status.NOTE, f"log: {self._log_path}")
        return 1 if self.failed() else 0
