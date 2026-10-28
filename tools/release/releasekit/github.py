"""Step 3, GitHub release with ``gh``: a draft pre-release unless told otherwise.

The release is created in the repository from the configuration (``capie-mods/capie``
after the split) or ``--repo``, at the exact local commit (``--target <sha>``), with the
notes built from ``CHANGELOG.md`` plus the fixed sections the release checklist asks
for (supported game build, multiplayer statement, SmartScreen, VirusTotal, checksums).
A draft does not create the tag; GitHub creates it when the release is published.

Before anything is uploaded the step checks the files again, failing closed: the ZIP is
re-hashed against the manifest (``zipName``, ``zipSha256`` and every file inside), every line
of ``SHA256SUMS.txt`` against the files, and the manifest signature against the public key
the installer pins (``Configuration.ps1``). So a re-run that wrote a new ZIP next to an old
manifest can never be published.

``--publish`` (not a draft) and ``--stable`` (not a pre-release) each need a typed
confirmation. ``install.ps1 -Update`` reads ``releases/latest``, which ignores drafts
and pre-releases, so players only see a release that is both published and stable.
"""

from __future__ import annotations

import json
import zipfile
from typing import TYPE_CHECKING

from releasekit import changelog, gitinfo
from releasekit.console import Status
from releasekit.package import (
    MANIFEST_NAME,
    SIGNATURE_NAME,
    SUMS_NAME,
    installer_public_key,
    sha256_bytes,
)
from releasekit.runner import quote
from releasekit.signing import pinned_fields, verify_signature

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
    if body is None or changelog.is_draft(body):
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


def _zip_problems(zip_path: Path, manifest: dict[str, object]) -> list[str]:
    """Re-hash the ZIP and its files against the manifest.

    Args:
        zip_path: Release ZIP.
        manifest: Parsed ``release-manifest.json``.

    Returns:
        One line per mismatch.
    """
    problems: list[str] = []
    zip_sha = sha256_bytes(zip_path.read_bytes())
    if manifest.get("zipName") != zip_path.name:
        problems.append(f"{MANIFEST_NAME} names {manifest.get('zipName')!r}, not {zip_path.name}")
    if str(manifest.get("zipSha256", "")).upper() != zip_sha:
        problems.append(
            f"{zip_path.name} SHA-256 {zip_sha} != {MANIFEST_NAME} {manifest.get('zipSha256')}"
        )
    try:
        with zipfile.ZipFile(zip_path) as archive:
            inside = {i.filename: sha256_bytes(archive.read(i)) for i in archive.infolist()}
    except zipfile.BadZipFile:
        return [*problems, f"{zip_path.name} is not a readable ZIP"]
    files = manifest.get("files")
    listed = {str(k): str(v).upper() for k, v in files.items()} if isinstance(files, dict) else {}
    if inside != listed:
        problems.append(f"the files inside {zip_path.name} differ from {MANIFEST_NAME}")
    return problems


def asset_problems(ctx: Context) -> list[str]:
    """Re-check the release files on disk: ZIP against manifest, sums, and the signature.

    Args:
        ctx: Run context (the ZIP, manifest, signature and sums must exist).

    Returns:
        One line per mismatch; empty when the files are exactly what was signed.
    """
    out = ctx.out_dir
    zip_path = out / ctx.config.bundle.zip_name.format(version=ctx.version)
    manifest = (out / MANIFEST_NAME).read_bytes()
    try:
        data = json.loads(manifest.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError):
        data = None
    if not isinstance(data, dict):
        return [f"{MANIFEST_NAME} is not a JSON object"]
    problems = _zip_problems(zip_path, data)
    signed = (zip_path, out / MANIFEST_NAME, out / SIGNATURE_NAME)  # what SHA256SUMS.txt lists
    expected = {p.name: sha256_bytes(p.read_bytes()).lower() for p in signed}
    sums: dict[str, str] = {}
    for line in (out / SUMS_NAME).read_text(encoding="ascii", errors="replace").splitlines():
        digest, _, name = line.partition("  ")
        sums[name] = digest
    if sums != expected:
        problems.append(f"{SUMS_NAME} does not list exactly the current {', '.join(expected)}")
    key = installer_public_key(ctx)
    signature = (out / SIGNATURE_NAME).read_text(encoding="utf-8")
    if key is None:
        problems.append(
            f"no public key in {ctx.config.signing.installer_configuration}: "
            "the signature cannot be verified"
        )
    elif not verify_signature(key, manifest, signature):
        problems.append(f"{SIGNATURE_NAME} does not verify with the installer public key")
    return problems


def check_local(ctx: Context) -> bool:
    """Assets (re-verified by :func:`asset_problems`), working tree and installer pins.

    Args:
        ctx: Run context.

    Returns:
        True when the local state allows a release.
    """
    reporter = ctx.reporter
    ok = True
    present = True
    for asset in assets(ctx):
        if not asset.is_file():
            status = Status.PLAN if ctx.options.dry_run else Status.FAIL
            reporter.record(STEP, status, f"asset missing (run 'build' first): {asset}")
            ok = ok and ctx.options.dry_run
            present = False
    if present:
        problems = asset_problems(ctx)
        for problem in problems:
            reporter.record(STEP, Status.FAIL, f"{problem}: run 'build' again")
        if problems:
            ok = False
        else:
            reporter.record(STEP, Status.OK, "ZIP, manifest, checksums and signature re-verified")
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
        ctx.reporter.record(
            STEP, Status.FAIL, f"no reviewed changelog section [{ctx.version}] (missing or DRAFT)"
        )
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
            STEP,
            Status.FAIL,
            f"{ctx.config.github.changelog} [{ctx.version}] is missing or marked DRAFT",
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
