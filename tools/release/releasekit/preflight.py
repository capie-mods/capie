"""Step 1, preflight: is this commit ready to be released?

Read-only checks first (working tree, branch, version sources, changelogs, installer
placeholders, the switch mod's Workshop ID, the installed game build), then the
configured test and lint commands (``[[preflight.commands]]`` in ``release.toml``).
A command with ``clean_after`` must leave those paths unchanged in git, which is how
"``gen.py`` output equals ``mod/``" is checked. Commands that need the game, MinHook or
PSScriptAnalyzer are skipped with a note when those are not available.
"""

from __future__ import annotations

import hashlib
from typing import TYPE_CHECKING

from releasekit import changelog, gitinfo
from releasekit.console import Status
from releasekit.versions import (
    VersionError,
    capie_problems,
    descriptor_version,
    launcher_version,
    launcher_workshop_id,
    stale_description_versions,
)

if TYPE_CHECKING:
    from pathlib import Path

    from releasekit.context import Context

STEP = "preflight"
"""Step name in the summary."""

TAIL_LINES = 15
"""Output lines of a failed command shown on the console."""

PLACEHOLDER_MARKERS = ("PLACEHOLDER", "TO-BE-GENERATED", "TO-BE-PINNED")
"""Texts that mark an unconfigured value in the installer configuration."""

HASH_CHUNK = 1 << 20
"""Read size when hashing large files."""


def sha256_file(path: Path) -> str:
    """Hash a file.

    Args:
        path: File.

    Returns:
        Upper-case hexadecimal SHA-256.
    """
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        while chunk := handle.read(HASH_CHUNK):
            digest.update(chunk)
    return digest.hexdigest().upper()


def check_git(ctx: Context) -> None:
    """Clean working tree and the release branch.

    Args:
        ctx: Run context.
    """
    reporter = ctx.reporter
    changed = gitinfo.status_lines(ctx.runner, ctx.config.root)
    if changed:
        shown = "; ".join(line.strip() for line in changed[:5])
        more = f" (+{len(changed) - 5} more)" if len(changed) > 5 else ""  # noqa: PLR2004
        reporter.record(STEP, Status.FAIL, f"working tree not clean: {shown}{more}")
    else:
        reporter.record(STEP, Status.OK, "working tree clean")
    current = gitinfo.branch(ctx.runner, ctx.config.root)
    if current != ctx.config.branch:
        reporter.record(
            STEP, Status.FAIL, f"on branch {current}, releases come from {ctx.config.branch}"
        )
    else:
        reporter.record(
            STEP,
            Status.OK,
            f"branch {current} @ {gitinfo.head_commit(ctx.runner, ctx.config.root)[:12]}",
        )


def check_versions(ctx: Context) -> None:
    """Version sources agree, and each released thing has release notes.

    Args:
        ctx: Run context.
    """
    reporter = ctx.reporter
    config = ctx.config
    problems = capie_problems(ctx.capie)
    for problem in problems:
        reporter.record(STEP, Status.FAIL, f"CAPIE version: {problem}")
    if not problems:
        reporter.record(
            STEP,
            Status.OK,
            f"CAPIE {ctx.version} (API {ctx.capie.api_level}, game {ctx.capie.game_version}) "
            f"from {config.versions.capie_header}",
        )
    script = config.resolve(config.versions.launcher_build_script)
    launcher = launcher_version(script.read_text(encoding="utf-8")) if script.is_file() else None
    if launcher is None or "dev" in launcher:
        reporter.record(
            STEP,
            Status.WARN,
            f"launcher version {launcher!r} in {config.versions.launcher_build_script}",
        )
    notes = config.resolve(config.github.changelog)
    text = notes.read_text(encoding="utf-8") if notes.is_file() else ""
    if changelog.section(text, ctx.version) is None:
        reporter.record(
            STEP, Status.FAIL, f"{config.github.changelog} has no section [{ctx.version}]"
        )
    else:
        reporter.record(
            STEP, Status.OK, f"release notes [{ctx.version}] in {config.github.changelog}"
        )
    for item in config.workshop.items:
        check_item_version(ctx, item.key)


def check_item_version(ctx: Context, key: str) -> None:
    """A Workshop item's descriptor version, change note and description.

    Args:
        ctx: Run context.
        key: Item key.
    """
    reporter = ctx.reporter
    item = ctx.config.item(key)
    descriptor = ctx.config.resolve(item.content_dir) / "descriptor.mod"
    try:
        version = descriptor_version(descriptor.read_text(encoding="utf-8"))
    except (OSError, VersionError) as error:
        reporter.record(STEP, Status.FAIL, f"{key}: {error}")
        return
    notes = ctx.config.resolve(item.changelog)
    text = notes.read_text(encoding="utf-8") if notes.is_file() else ""
    if changelog.section(text, version) is None:
        reporter.record(
            STEP,
            Status.WARN,
            f"{key} {version}: {item.changelog} has no section [{version}] "
            "(needed for the Workshop change note)",
        )
    else:
        reporter.record(STEP, Status.OK, f"{key} {version}: change note in {item.changelog}")
    if item.description_file:
        description = ctx.config.resolve(item.description_file)
        stale = stale_description_versions(description.read_text(encoding="utf-8"), ctx.capie.short)
        if stale:
            reporter.record(
                STEP,
                Status.WARN,
                f"{item.description_file} names CAPIE {', '.join(stale)}, "
                f"current is {ctx.capie.short}",
            )


def check_installer(ctx: Context) -> None:
    """Unconfigured values in the installer and the switch mod's compiled-in Workshop ID.

    Args:
        ctx: Run context.
    """
    reporter = ctx.reporter
    config = ctx.config
    installer = config.resolve(config.signing.installer_configuration)
    text = installer.read_text(encoding="utf-8") if installer.is_file() else ""
    markers = [m for m in PLACEHOLDER_MARKERS if m in text]
    if markers:
        reporter.record(
            STEP,
            Status.WARN,
            f"installer configuration still has placeholders ({', '.join(markers)}): "
            f"{config.signing.installer_configuration}",
        )
    if ctx.repo not in text:
        reporter.record(
            STEP,
            Status.WARN,
            f"installer GitHub repository is not {ctx.repo} "
            "(install.ps1 -Update would look elsewhere)",
        )
    switch = next(
        (i for i in config.workshop.items if i.content_dir == config.bundle.switch_mod.source_dir),
        None,
    )
    header = config.resolve(config.versions.launcher_config)
    compiled = (
        launcher_workshop_id(header.read_text(encoding="utf-8")) if header.is_file() else None
    )
    if (
        switch is not None
        and switch.published_file_id
        and compiled != str(switch.published_file_id)
    ):
        reporter.record(
            STEP,
            Status.WARN,
            f"launcher capie_mod_workshop_id is {compiled!r}, "
            f"the switch mod is item {switch.published_file_id}: "
            "set it in launcher/config.hpp and rebuild the launcher, "
            "or Workshop subscribers are not recognised",
        )


def check_game(ctx: Context) -> None:
    """The installed game is the build CAPIE supports.

    Args:
        ctx: Run context.
    """
    game = ctx.game_dir
    if game is None:
        ctx.reporter.record(
            STEP, Status.SKIP, "game not found: checks that need hoi4.exe are skipped (--game-dir)"
        )
        return
    actual = sha256_file(game / "hoi4.exe")
    if actual != ctx.capie.game_sha256:
        ctx.reporter.record(
            STEP,
            Status.WARN,
            f"installed hoi4.exe {actual[:16]}... is not the supported build "
            f"{ctx.capie.game_version}; pin tests will fail",
        )
    else:
        ctx.reporter.record(
            STEP, Status.OK, f"installed hoi4.exe is the supported build {ctx.capie.game_version}"
        )


def run_commands(ctx: Context) -> None:
    """Run the configured checks.

    Args:
        ctx: Run context.
    """
    for spec in ctx.config.preflight:
        result = ctx.run_spec(STEP, spec)
        if result is None:
            continue
        if not result.ok:
            tail = result.output.strip().splitlines()[-TAIL_LINES:]
            for line in tail:
                ctx.reporter.detail(line, echo=True)
            ctx.reporter.record(STEP, Status.FAIL, f"{spec.name} (exit {result.returncode})")
            continue
        if spec.clean_after:
            changed = gitinfo.status_lines(ctx.runner, ctx.config.root, spec.clean_after)
            if changed:
                ctx.reporter.record(
                    STEP,
                    Status.FAIL,
                    f"{spec.name} changed {len(changed)} tracked file(s) in "
                    f"{', '.join(spec.clean_after)}: output is stale",
                )
                continue
        ctx.reporter.record(STEP, Status.OK, spec.name)


def run(ctx: Context) -> bool:
    """Run the whole preflight step.

    Args:
        ctx: Run context.

    Returns:
        True when nothing failed.
    """
    ctx.reporter.heading(f"Preflight: CAPIE {ctx.version} -> {ctx.repo}")
    check_git(ctx)
    check_versions(ctx)
    check_installer(ctx)
    check_game(ctx)
    run_commands(ctx)
    return not ctx.reporter.failed(STEP)
