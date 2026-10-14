"""Read-only questions to git: working tree state, branch, commit, tracked files."""

from __future__ import annotations

from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from pathlib import Path

    from releasekit.runner import CommandRunner


class GitError(RuntimeError):
    """A git command failed."""


def _git(runner: CommandRunner, root: Path, *args: str) -> str:
    """Run one read-only git command.

    Args:
        runner: Process runner.
        root: Repository root.
        *args: git arguments.

    Returns:
        Standard output.

    Raises:
        GitError: Non-zero exit code.
    """
    result = runner.run(["git", *args], cwd=root)
    if not result.ok:
        msg = f"git {' '.join(args)} failed: {result.output.strip()}"
        raise GitError(msg)
    return result.output


def status_lines(runner: CommandRunner, root: Path, paths: tuple[str, ...] = ()) -> list[str]:
    """List changed and untracked files (``git status --porcelain``).

    Args:
        runner: Process runner.
        root: Repository root.
        paths: Limit to these paths (empty = whole tree).

    Returns:
        One porcelain line per changed path (empty when clean).
    """
    args = ["status", "--porcelain", "--untracked-files=all"]
    if paths:
        args += ["--", *paths]
    return [line for line in _git(runner, root, *args).splitlines() if line.strip()]


def branch(runner: CommandRunner, root: Path) -> str:
    """Name of the checked-out branch.

    Args:
        runner: Process runner.
        root: Repository root.

    Returns:
        The branch name (``HEAD`` when detached).
    """
    return _git(runner, root, "rev-parse", "--abbrev-ref", "HEAD").strip()


def head_commit(runner: CommandRunner, root: Path) -> str:
    """Full SHA of ``HEAD``.

    Args:
        runner: Process runner.
        root: Repository root.

    Returns:
        The commit SHA.
    """
    return _git(runner, root, "rev-parse", "HEAD").strip()


def tracked_files(runner: CommandRunner, root: Path, folder: str) -> list[str]:
    """Files tracked by git below a folder.

    Args:
        runner: Process runner.
        root: Repository root.
        folder: Folder relative to the root.

    Returns:
        POSIX paths relative to the root, sorted.
    """
    output = _git(runner, root, "ls-files", "-z", "--", folder)
    return sorted(path for path in output.split("\0") if path)
