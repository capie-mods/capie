"""Step 3, GitHub release with ``gh``: a draft pre-release unless told otherwise.

The release is created in the repository from the configuration (``capie-mods/capie``
after the split) or ``--repo``, at the exact local commit (``--target <sha>``), with the
notes built from ``CHANGELOG.md`` plus the fixed sections the release checklist asks
for (supported game build, multiplayer statement, SmartScreen, VirusTotal, checksums).
A draft does not create the tag; GitHub creates it when the release is published.

``--publish`` (not a draft) and ``--stable`` (not a pre-release) each need a typed
confirmation. ``install.ps1 -Update`` reads ``releases/latest``, which ignores drafts
and pre-releases, so players only see a release that is both published and stable.
"""

from __future__ import annotations

import json
from typing import TYPE_CHECKING

from releasekit import changelog, gitinfo
from releasekit.console import Status
from releasekit.package import MANIFEST_NAME, SIGNATURE_NAME, SUMS_NAME
from releasekit.runner import quote
from releasekit.signing import pinned_fields

if TYPE_CHECKING:
    from pathlib import Path

    from releasekit.context import Context

STEP = "release"
"""Step name in the summary."""

NOTES_NAME = "release-notes.md"
"""Generated release notes in the output folder."""

VIRUSTOTAL_NOTES_NAME = "virustotal.md"
"""VirusTotal section written by the VirusTotal step."""

INSTALLER_ASSET = "install.ps1"
"""Installer asset name."""


def assets(ctx: Context) -> list[Path]:
    """Files attached to the release.

    Args:
        ctx: Run context.

    Returns:
        ZIP, manifest, signature, checksums and the installer, in upload order.
    """
    out = ctx.out_dir
    zip_name = ctx.config.bundle.zip_name.format(version=ctx.version)
    return [
        out / zip_name,
        out / MANIFEST_NAME,
        out / SIGNATURE_NAME,
        out / SUMS_NAME,
        ctx.config.resolve(ctx.config.signing.installer_script),
    ]


def build_notes(ctx: Context, commit: str) -> str | None:
    """Release notes in Markdown.

    Args:
        ctx: Run context.
        commit: Released commit.

    Returns:
        The notes, or ``None`` when the changelog has no section for this version.
    """
    path = ctx.config.resolve(ctx.config.github.changelog)
    body = changelog.section(
        path.read_text(encoding="utf-8") if path.is_file() else "", ctx.version
    )
    if body is None:
        return None
    capie = ctx.capie
    zip_name = ctx.config.bundle.zip_name.format(version=ctx.version)
    sums = ctx.out_dir / SUMS_NAME
    vt = ctx.out_dir / VIRUSTOTAL_NOTES_NAME
    virustotal = (
        vt.read_text(encoding="utf-8").strip()
        if vt.is_file()
        else "Pending: results are added before the release is published."
    )
    return "\n".join(
        [
            body,
            "",
            "## Supported game build",
            "",
            f"Hearts of Iron IV {capie.game_version} (Steam, x64),",
            f"`hoi4.exe` SHA-256 `{capie.game_sha256}`.",
            f"CAPIE {capie.release}, script API level {capie.api_level}.",
            "On any other build the launcher",
            "starts the game without CAPIE.",
            "",
            "## Multiplayer",
            "",
            "Not declared multiplayer-compatible: multiplayer has not been tested yet.",
            "",
            "## Windows SmartScreen",
            "",
            "The files are not code-signed (ADR-0031).",
            'SmartScreen may show "Windows protected your PC";',
            'choose "More info" and "Run anyway" only after checking the SHA-256 values below.',
            "The installer verifies the signed `release-manifest.json`",
            "before it installs anything.",
            "",
            "## VirusTotal",
            "",
            virustotal,
            "",
            "## Checksums",
            "",
            "```",
            sums.read_text(encoding="utf-8").strip() if sums.is_file() else f"(see {SUMS_NAME})",
            "```",
            "",
            f"Built locally by the maintainer from commit `{commit}` ({zip_name}).",
        ]
    )


def _gh(ctx: Context, *args: str) -> tuple[str, ...]:
    """A ``gh`` command line for the release repository.

    Args:
        ctx: Run context.
        *args: gh arguments before ``--repo``.

    Returns:
        The argv.
    """
    return ("gh", *args, "--repo", ctx.repo)


def check_local(ctx: Context) -> bool:
    """Assets, working tree and installer pins.

    Args:
        ctx: Run context.

    Returns:
        True when the local state allows a release.
    """
    reporter = ctx.reporter
    ok = True
    for asset in assets(ctx):
        if not asset.is_file():
            status = Status.PLAN if ctx.options.dry_run else Status.FAIL
            reporter.record(STEP, status, f"asset missing (run 'build' first): {asset}")
            ok = ok and ctx.options.dry_run
    if gitinfo.status_lines(ctx.runner, ctx.config.root):
        reporter.record(
            STEP, Status.FAIL, "working tree not clean: commit the installer pins first"
        )
        ok = False
    manifest = ctx.out_dir / MANIFEST_NAME
    if manifest.is_file():
        data = json.loads(manifest.read_text(encoding="utf-8"))
        installer = ctx.config.resolve(ctx.config.signing.installer_configuration).read_text(
            encoding="utf-8"
        )
        pins = pinned_fields(installer.replace("\r\n", "\n"))
        if data.get("version") != ctx.version:
            reporter.record(
                STEP,
                Status.FAIL,
                f"{MANIFEST_NAME} is for {data.get('version')}, not {ctx.version}",
            )
            ok = False
        if str(pins.get("ZipSha256", "")).upper() != str(data.get("zipSha256", "")).upper():
            status = Status.FAIL if ctx.options.publish else Status.WARN
            reporter.record(
                STEP,
                status,
                "install.ps1 pins another ZIP: 'install.ps1 -Install' would refuse this release",
            )
            ok = ok and not ctx.options.publish
    return ok


def check_ready(ctx: Context, commit: str) -> bool:
    """Local state, then the repository on GitHub, before anything is created.

    Args:
        ctx: Run context.
        commit: Commit to release.

    Returns:
        True when the release can be created.
    """
    reporter = ctx.reporter
    ok = check_local(ctx)
    if ctx.options.dry_run:
        reporter.line(
            Status.PLAN, quote(_gh(ctx, "release", "view", ctx.tag)) + "  (must not exist)"
        )
        return ok
    if not ctx.runner.run(("gh", "auth", "status"), cwd=ctx.config.root).ok:
        reporter.record(STEP, Status.FAIL, "gh is not logged in (run 'gh auth login' yourself)")
        return False
    if ctx.runner.run(_gh(ctx, "release", "view", ctx.tag), cwd=ctx.config.root).ok:
        reporter.record(STEP, Status.FAIL, f"release {ctx.tag} already exists in {ctx.repo}")
        ok = False
    probe = ctx.runner.run(
        ("gh", "api", f"repos/{ctx.repo}/commits/{commit}", "--silent"), cwd=ctx.config.root
    )
    if not probe.ok:
        reporter.record(
            STEP, Status.FAIL, f"commit {commit[:12]} is not in {ctx.repo}: push it first"
        )
        ok = False
    return ok


def confirm_visibility(ctx: Context) -> bool:
    """Typed confirmations for a published or stable release.

    Args:
        ctx: Run context.

    Returns:
        True when every requested escalation was confirmed (or none was requested).
    """
    if ctx.options.dry_run:
        return True
    if ctx.options.publish and not ctx.prompter.confirm(
        f"Publish {ctx.tag} in {ctx.repo} now (visible to everyone, tag created)?", "publish"
    ):
        ctx.reporter.record(STEP, Status.FAIL, "publishing not confirmed")
        return False
    if ctx.options.stable and not ctx.prompter.confirm(
        f"Mark {ctx.tag} as a stable release "
        "(install.ps1 -Update offers it to players once published)?",
        "stable",
    ):
        ctx.reporter.record(STEP, Status.FAIL, "stable release not confirmed")
        return False
    return True


def create_command(ctx: Context, commit: str, notes: Path) -> tuple[str, ...]:
    """The ``gh release create`` command line.

    Args:
        ctx: Run context.
        commit: Target commit.
        notes: Notes file.

    Returns:
        The argv (draft and pre-release unless ``--publish`` / ``--stable``).
    """
    flags = []
    if not ctx.options.publish:
        flags.append("--draft")
    if not ctx.options.stable:
        flags.append("--prerelease")
    title = ctx.config.github.title.format(version=ctx.version)
    return _gh(
        ctx,
        "release",
        "create",
        ctx.tag,
        *(str(a) for a in assets(ctx)),
        "--title",
        title,
        "--notes-file",
        str(notes),
        "--target",
        commit,
        *flags,
    )


def update_notes(ctx: Context) -> bool:
    """Replace the notes of an existing release (after the VirusTotal step).

    Args:
        ctx: Run context.

    Returns:
        True when the notes were updated (or only shown).
    """
    commit = gitinfo.head_commit(ctx.runner, ctx.config.root)
    notes = build_notes(ctx, commit)
    if notes is None:
        ctx.reporter.record(STEP, Status.FAIL, f"no changelog section [{ctx.version}]")
        return False
    path = ctx.out_dir / NOTES_NAME
    argv = _gh(ctx, "release", "edit", ctx.tag, "--notes-file", str(path))
    if ctx.options.dry_run:
        ctx.reporter.line(Status.PLAN, quote(argv))
        return True
    path.write_text(notes + "\n", encoding="utf-8", newline="\n")
    result = ctx.runner.run(argv, cwd=ctx.config.root)
    status = Status.OK if result.ok else Status.FAIL
    ctx.reporter.record(
        STEP,
        status,
        f"release notes of {ctx.tag} updated"
        if result.ok
        else f"gh release edit failed: {result.output.strip()}",
    )
    return result.ok


def run(ctx: Context) -> bool:
    """Run the whole GitHub release step.

    Args:
        ctx: Run context.

    Returns:
        True when nothing failed.
    """
    kind = ("published" if ctx.options.publish else "draft") + (
        " stable" if ctx.options.stable else " pre-release"
    )
    ctx.reporter.heading(f"GitHub release: {ctx.tag} ({kind}) in {ctx.repo}")
    commit = gitinfo.head_commit(ctx.runner, ctx.config.root)
    notes = build_notes(ctx, commit)
    if notes is None:
        ctx.reporter.record(
            STEP, Status.FAIL, f"{ctx.config.github.changelog} has no section [{ctx.version}]"
        )
        return False
    if not check_ready(ctx, commit) or not confirm_visibility(ctx):
        return False
    notes_path = ctx.out_dir / NOTES_NAME
    argv = create_command(ctx, commit, notes_path)
    if ctx.options.dry_run:
        ctx.reporter.line(Status.PLAN, f"write {notes_path} ({len(notes.splitlines())} lines)")
        ctx.reporter.line(Status.PLAN, quote(argv))
        return True
    notes_path.write_text(notes + "\n", encoding="utf-8", newline="\n")
    result = ctx.runner.run(argv, cwd=ctx.config.root)
    if not result.ok:
        ctx.reporter.record(STEP, Status.FAIL, f"gh release create failed: {result.output.strip()}")
        return False
    ctx.reporter.record(STEP, Status.OK, f"{kind} {ctx.tag} created: {result.output.strip()}")
    if not ctx.options.publish:
        ctx.reporter.record(
            STEP,
            Status.NOTE,
            f"review the draft, then publish it on GitHub or with: "
            f"gh release edit {ctx.tag} --draft=false --repo {ctx.repo}",
        )
    return True
