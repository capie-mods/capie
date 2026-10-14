"""Step 5, Steam Workshop: stage each mod and upload them all with one SteamCMD run.

For every item in ``[[workshop.items]]`` (or the ``--item`` ones):

1. Stage a fresh copy of the mod: only files tracked by git under ``content_dir``,
   minus the ``exclude`` patterns (developer files). ``descriptor.mod`` is rewritten the
   way the Paradox Launcher publishes a mod: no ``path``/``archive`` line, and
   ``remote_file_id`` set once the item has an ID. The local ``*.mod`` registration file
   is never uploaded. A change note still marked ``DRAFT`` is refused.
2. Write ``item.vdf`` for ``workshop_build_item``: ``appid`` 394360, ``publishedfileid``
   (0 = new item), ``contentfolder``, ``previewfile``, ``changenote`` (from the item's
   changelog, converted to BBCode). A new item also gets ``title``, ``description`` and
   ``visibility`` 2 (Hidden, ``k_ERemoteStoragePublishedFileVisibilityPrivate``). An
   existing item keeps its visibility, title and description unless ``--visibility`` or
   ``--update-description`` say otherwise. Values are made safe for SteamCMD's parser,
   which has no escape sequences (:mod:`releasekit.vdf`), and every VDF is parsed back
   with the same rules before SteamCMD runs.
3. Ask every question first (visibility other than hidden, then "upload"), and refuse
   while another ``steamcmd.exe`` is still running (an interrupted earlier run would
   print into the prompts). Nothing starts before the answers.
4. Run one ``steamcmd +login <account> +workshop_build_item <vdf> ... +quit`` on the
   console, so the account logs in once for all items. Only the account name is passed.
   SteamCMD asks for the password and the Steam Guard code itself; this tool never
   accepts, stores or logs them, and does not capture SteamCMD's output at all.
5. SteamCMD writes a new item's ``publishedfileid`` back into its VDF; the tool stores it
   in ``release.toml``. If it cannot be read, the maintainer may type it in.
"""

from __future__ import annotations

import fnmatch
import re
import shutil
from dataclasses import dataclass
from pathlib import Path, PurePosixPath
from typing import TYPE_CHECKING

from releasekit import changelog, gitinfo
from releasekit.config import VISIBILITY_VALUES, set_published_file_id
from releasekit.console import Status
from releasekit.runner import quote
from releasekit.vdf import VdfError, parse_vdf, render_vdf, vdf_published_id, vdf_value
from releasekit.versions import VersionError, descriptor_fields, descriptor_version

if TYPE_CHECKING:
    from collections.abc import Sequence

    from releasekit.config import WorkshopItem
    from releasekit.context import Context

STEP = "workshop"
"""Step name in the summary."""

DESCRIPTOR = "descriptor.mod"
"""Descriptor file name inside a mod folder."""

VDF_NAME = "item.vdf"
"""Build config file name in an item's staging folder."""

PREVIEW_MAX_BYTES = 1024 * 1024
"""Largest preview image Steam and the Paradox Launcher accept."""

PREVIEW_SUFFIXES = (".png", ".jpg", ".jpeg", ".gif")
"""Preview image formats Steam accepts."""

HIDDEN = "hidden"
"""Default visibility of a new item."""

STEAM_ACCOUNT = re.compile(r"^[A-Za-z0-9_]{2,64}$")
"""A Steam account name (never a password: no other characters are accepted)."""

STEAMCMD_PROCESS = "steamcmd.exe"
"""Process name of a SteamCMD that is still running."""

_DROPPED_DESCRIPTOR_KEYS = ("path", "archive", "remote_file_id")
"""Descriptor keys that are machine-specific or set by this tool."""


@dataclass(frozen=True)
class StagedItem:
    """A Workshop item ready for upload."""

    item: WorkshopItem
    version: str
    files: tuple[tuple[str, Path], ...]
    descriptor: str
    changenote: str
    visibility: int | None


def is_excluded(relative: str, patterns: tuple[str, ...]) -> bool:
    """Tell whether a file is a developer file that must not be uploaded.

    Args:
        relative: POSIX path relative to the mod folder.
        patterns: ``fnmatch`` patterns matched against the whole path and every part.

    Returns:
        True when any pattern matches.
    """
    parts = PurePosixPath(relative).parts
    return any(
        fnmatch.fnmatch(relative, p) or any(fnmatch.fnmatch(part, p) for part in parts)
        for p in patterns
    )


def workshop_descriptor(text: str, published_file_id: int) -> str:
    """Rewrite ``descriptor.mod`` for the Workshop copy.

    Args:
        text: Original descriptor.
        published_file_id: Item ID (0 = not known yet).

    Returns:
        Descriptor without ``path``/``archive``, with ``remote_file_id`` when the ID is known.
    """
    keep = [
        line
        for line in text.removeprefix("﻿").replace("\r\n", "\n").split("\n")
        if line.split("=", 1)[0].strip() not in _DROPPED_DESCRIPTOR_KEYS
    ]
    body = "\n".join(keep).rstrip("\n")
    if published_file_id:
        body += f'\nremote_file_id="{published_file_id}"'
    return body + "\n"


def visibility_for(item: WorkshopItem, requested: str) -> int | None:
    """Visibility value to send.

    Args:
        item: Workshop item.
        requested: ``--visibility`` value (empty = default).

    Returns:
        The Steam value, or ``None`` to leave an existing item's visibility unchanged.
    """
    if requested:
        return VISIBILITY_VALUES[requested]
    return None if item.published_file_id else VISIBILITY_VALUES[HIDDEN]


def vdf_fields(
    staged: StagedItem, content: Path, preview: Path, app_id: int, description: str | None
) -> list[tuple[str, str]]:
    """Fields of the item's VDF (paths with forward slashes).

    Args:
        staged: Staged item.
        content: Absolute staged content folder.
        preview: Absolute preview image.
        app_id: Steam app ID.
        description: Description to send, or ``None`` to keep the current one.

    Returns:
        Keys and values in output order.
    """
    item = staged.item
    fields = [
        ("appid", str(app_id)),
        ("publishedfileid", str(item.published_file_id)),
        ("contentfolder", content.as_posix()),
        ("previewfile", preview.as_posix()),
    ]
    if staged.visibility is not None:
        fields.append(("visibility", str(staged.visibility)))
    if not item.published_file_id or description is not None:
        fields.append(("title", item.title))
    if description is not None:
        fields.append(("description", description))
    fields.append(("changenote", staged.changenote))
    return fields


def vdf_problem(fields: list[tuple[str, str]], text: str) -> str | None:
    """Parse a rendered VDF the way SteamCMD does and compare it with the fields.

    Args:
        fields: Keys and values that were rendered.
        text: Rendered VDF.

    Returns:
        ``None`` when SteamCMD will read exactly these values, else the problem.
    """
    try:
        parsed = parse_vdf(text)
    except VdfError as error:
        return str(error)
    if parsed != {key: vdf_value(value) for key, value in fields}:
        return "parsed values differ from the rendered ones"
    return None


def stage(ctx: Context, item: WorkshopItem) -> StagedItem | None:
    """Collect and check everything an upload needs (writes nothing).

    Args:
        ctx: Run context.
        item: Workshop item.

    Returns:
        The staged item, or ``None`` after recording why it cannot be uploaded.
    """
    reporter = ctx.reporter
    folder = ctx.config.resolve(item.content_dir)
    try:
        original = (folder / DESCRIPTOR).read_text(encoding="utf-8")
        version = descriptor_version(original)
    except (OSError, VersionError) as error:
        reporter.record(STEP, Status.FAIL, f"{item.key}: {error}")
        return None
    problems = []
    fields = descriptor_fields(original)
    if fields.get("supported_version", "") == "":
        problems.append("descriptor.mod has no supported_version")
    tracked = gitinfo.tracked_files(ctx.runner, ctx.config.root, item.content_dir)
    prefix = PurePosixPath(item.content_dir)
    files = []
    for path in tracked:
        relative = PurePosixPath(path).relative_to(prefix).as_posix()
        if relative != DESCRIPTOR and not is_excluded(relative, ctx.config.workshop.exclude):
            files.append((relative, ctx.config.root / path))
    picture = fields.get("picture")
    if picture is not None and picture not in {r for r, _ in files}:
        problems.append(f"descriptor picture {picture!r} is not in the mod folder")
    problems += _preview_problems(ctx, item)
    changenote, text_problems = _texts(ctx, item, version)
    problems += text_problems
    for problem in problems:
        reporter.record(STEP, Status.FAIL, f"{item.key}: {problem}")
    if problems:
        return None
    return StagedItem(
        item=item,
        version=version,
        files=tuple(files),
        descriptor=workshop_descriptor(original, item.published_file_id),
        changenote=changenote,
        visibility=visibility_for(item, ctx.options.visibility),
    )


def _texts(ctx: Context, item: WorkshopItem, version: str) -> tuple[str, list[str]]:
    """Change note (BBCode) and checks of the texts sent to Steam.

    Args:
        ctx: Run context.
        item: Workshop item.
        version: Item version.

    Returns:
        The change note and the problems (missing section, DRAFT marker, too long).
    """
    problems = []
    notes_path = ctx.config.resolve(item.changelog)
    notes = changelog.section(
        notes_path.read_text(encoding="utf-8") if notes_path.is_file() else "", version
    )
    if notes is None:
        problems.append(f"{item.changelog} has no section [{version}] for the change note")
    elif changelog.is_draft(notes):
        problems.append(f"{item.changelog} [{version}] is still marked DRAFT")
    description = _description(ctx, item)
    if description is not None and changelog.is_draft(description):
        problems.append(f"{item.description_file} is still marked DRAFT")
    changenote = changelog.to_bbcode(notes or "")
    if len(changenote) > changelog.STEAM_CHANGENOTE_LIMIT:
        problems.append("change note longer than 8000 characters")
    return changenote, problems


def _preview_problems(ctx: Context, item: WorkshopItem) -> list[str]:
    """Check the preview image.

    Args:
        ctx: Run context.
        item: Workshop item.

    Returns:
        Problems (empty when the image is usable).
    """
    if not item.preview_file:
        return ["no preview_file configured (Steam needs a preview image under 1 MB)"]
    preview = ctx.config.resolve(item.preview_file)
    if not preview.is_file():
        return [f"preview image not found: {item.preview_file}"]
    if preview.suffix.lower() not in PREVIEW_SUFFIXES:
        return [f"preview image must be PNG, JPG or GIF: {item.preview_file}"]
    if preview.stat().st_size >= PREVIEW_MAX_BYTES:
        return [f"preview image is 1 MB or larger: {item.preview_file}"]
    return []


def steam_account(ctx: Context) -> str | None:
    """The Steam account name (option, else configuration), validated.

    Args:
        ctx: Run context.

    Returns:
        The account name, or ``None`` after recording the problem.
    """
    account = ctx.options.steam_user or ctx.config.workshop.steam_user
    if not account:
        status = Status.PLAN if ctx.options.dry_run else Status.FAIL
        ctx.reporter.record(
            STEP, status, "no Steam account: pass --steam-user <account name> (never a password)"
        )
        return None
    if not STEAM_ACCOUNT.match(account):
        ctx.reporter.record(
            STEP, Status.FAIL, "the Steam account name may only contain letters, digits and _"
        )
        return None
    return account


def steamcmd_path(ctx: Context) -> str | None:
    """Find SteamCMD.

    Args:
        ctx: Run context.

    Returns:
        Path or command name, or ``None`` when it is not installed.
    """
    configured = ctx.options.steamcmd or ctx.config.workshop.steamcmd
    if configured:
        return configured if Path(configured).is_file() else ctx.runner.which(configured)
    return ctx.runner.which("steamcmd")


def steamcmd_running(ctx: Context) -> bool:
    """Tell whether a SteamCMD process is still running (read-only ``tasklist`` query).

    Args:
        ctx: Run context.

    Returns:
        True when ``steamcmd.exe`` is in the process list.
    """
    result = ctx.runner.run(
        ["tasklist", "/FI", f"IMAGENAME eq {STEAMCMD_PROCESS}", "/NH", "/FO", "CSV"],
        cwd=ctx.config.root,
    )
    return result.ok and f'"{STEAMCMD_PROCESS}"' in result.output.lower()


def confirm_visibility(ctx: Context, staged: StagedItem) -> bool:
    """Typed confirmation for a visibility other than hidden.

    Args:
        ctx: Run context.
        staged: Staged item.

    Returns:
        True when no confirmation is needed or it was given.
    """
    requested = ctx.options.visibility
    if (
        staged.visibility is None
        or staged.visibility == VISIBILITY_VALUES[HIDDEN]
        or ctx.options.dry_run
    ):
        return True
    return ctx.prompter.confirm(
        f"Make Workshop item {staged.item.title!r} {requested.upper()}?", requested
    )


def _base(ctx: Context, staged: StagedItem) -> Path:
    """Staging folder of an item.

    Args:
        ctx: Run context.
        staged: Staged item.

    Returns:
        ``<out>/workshop/<key>-<version>``.
    """
    return ctx.out_dir / "workshop" / f"{staged.item.key}-{staged.version}"


def item_vdf(ctx: Context, staged: StagedItem) -> tuple[list[tuple[str, str]], str]:
    """Fields and text of an item's VDF.

    Args:
        ctx: Run context.
        staged: Staged item.

    Returns:
        The fields and the rendered VDF.
    """
    content = (_base(ctx, staged) / "content").resolve()
    preview = ctx.config.resolve(staged.item.preview_file).resolve()
    fields = vdf_fields(
        staged, content, preview, ctx.config.workshop.app_id, _description(ctx, staged.item)
    )
    return fields, render_vdf(fields)


def write_stage(ctx: Context, staged: StagedItem) -> Path:
    """Copy the content into a fresh folder and write the VDF.

    Args:
        ctx: Run context.
        staged: Staged item.

    Returns:
        The VDF path.
    """
    content = _base(ctx, staged) / "content"
    if content.exists():
        shutil.rmtree(content)
    content.mkdir(parents=True)
    for relative, source in staged.files:
        target = content / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target)
    (content / DESCRIPTOR).write_text(staged.descriptor, encoding="utf-8", newline="\n")
    vdf = _base(ctx, staged) / VDF_NAME
    vdf.write_text(item_vdf(ctx, staged)[1], encoding="utf-8", newline="\n")
    return vdf


def _description(ctx: Context, item: WorkshopItem) -> str | None:
    """Description to send: always for a new item, else only with ``--update-description``.

    Args:
        ctx: Run context.
        item: Workshop item.

    Returns:
        The description text, or ``None`` to keep the current one.
    """
    if item.published_file_id and not ctx.options.update_description:
        return None
    if not item.description_file:
        return None
    return ctx.config.resolve(item.description_file).read_text(encoding="utf-8").strip()


def record_new_id(ctx: Context, staged: StagedItem, vdf: Path, *, ask: bool) -> None:
    """Store the ID of a newly created item in ``release.toml``.

    Args:
        ctx: Run context.
        staged: Staged item.
        vdf: VDF that SteamCMD updated.
        ask: Ask for the ID when the VDF has none (only after a successful run).
    """
    new_id = vdf_published_id(vdf.read_text(encoding="utf-8"))
    if not new_id and ask:
        typed = ctx.prompter.ask(
            f"SteamCMD did not write the new ID of {staged.item.title!r} into the VDF. "
            "Type it (empty to skip):"
        )
        new_id = int(typed) if typed.isdigit() else 0
    if not new_id:
        ctx.reporter.record(
            STEP,
            Status.WARN,
            f"{staged.item.key}: new item ID unknown; "
            f"set published_file_id in {ctx.config.path.name} by hand if it was created",
        )
        return
    text = ctx.config.path.read_text(encoding="utf-8")
    ctx.config.path.write_text(
        set_published_file_id(text, staged.item.key, new_id), encoding="utf-8", newline="\n"
    )
    ctx.reporter.record(
        STEP,
        Status.OK,
        f"{staged.item.key}: item {new_id} stored in {ctx.config.path.name} (commit it)",
    )
    ctx.reporter.record(
        STEP,
        Status.NOTE,
        f"{staged.item.key}: upload again ('workshop --item {staged.item.key}') "
        "so descriptor.mod carries remote_file_id",
    )
    if staged.item.content_dir == ctx.config.bundle.switch_mod.source_dir:
        ctx.reporter.record(
            STEP,
            Status.NOTE,
            f'set capie_mod_workshop_id = "{new_id}" in {ctx.config.versions.launcher_config}, '
            "rebuild the launcher, release again",
        )


def describe(ctx: Context, staged: StagedItem) -> None:
    """One summary line per item.

    Args:
        ctx: Run context.
        staged: Staged item.
    """
    item = staged.item
    kind = f"update of {item.published_file_id}" if item.published_file_id else "NEW item"
    visibility = (
        "unchanged"
        if staged.visibility is None
        else next(k for k, v in VISIBILITY_VALUES.items() if v == staged.visibility)
    )
    description = "new description" if _description(ctx, item) is not None else "description kept"
    ctx.reporter.line(
        Status.NOTE,
        f"{item.title} {staged.version}: {kind}, visibility {visibility}, {description}, "
        f"{len(staged.files) + 1} files",
    )


def check_vdfs(ctx: Context, staged: Sequence[StagedItem]) -> bool:
    """Parse every VDF with SteamCMD's rules before anything runs.

    Args:
        ctx: Run context.
        staged: Staged items.

    Returns:
        True when every VDF parses to exactly its values.
    """
    ok = True
    for item in staged:
        fields, text = item_vdf(ctx, item)
        problem = vdf_problem(fields, text)
        if problem is not None:
            ctx.reporter.record(STEP, Status.FAIL, f"{item.item.key}: {VDF_NAME} {problem}")
            ok = False
        else:
            ctx.reporter.record(
                STEP, Status.OK, f"{item.item.key}: {VDF_NAME} parses as SteamCMD reads it"
            )
    return ok


def show_plan(ctx: Context, staged: Sequence[StagedItem], argv: list[str]) -> None:
    """Dry run: staged files, descriptors, VDFs and the SteamCMD command.

    Args:
        ctx: Run context.
        staged: Staged items.
        argv: SteamCMD command line.
    """
    for item in staged:
        for relative, _ in item.files:
            ctx.reporter.line(Status.PLAN, f"  {item.item.key}: stage {relative}")
        ctx.reporter.line(Status.PLAN, f"  {item.item.key}: stage {DESCRIPTOR} (rewritten):")
        for line in item.descriptor.splitlines():
            ctx.reporter.line(Status.PLAN, f"    {line}")
        ctx.reporter.line(Status.PLAN, f"  {_base(ctx, item) / VDF_NAME}:")
        for line in item_vdf(ctx, item)[1].splitlines():
            ctx.reporter.line(Status.PLAN, f"    {line}")
    ctx.reporter.line(Status.PLAN, quote(argv))


def confirm_all(ctx: Context, staged: Sequence[StagedItem], account: str) -> bool:
    """Every question, before anything is written or started.

    Args:
        ctx: Run context.
        staged: Staged items.
        account: Steam account name.

    Returns:
        True when the upload may go ahead.
    """
    if steamcmd_running(ctx):
        ctx.reporter.record(
            STEP,
            Status.FAIL,
            "steamcmd.exe is still running (an interrupted earlier upload?): close it first",
        )
        return False
    for item in staged:
        if not confirm_visibility(ctx, item):
            ctx.reporter.record(
                STEP,
                Status.FAIL,
                f"{item.item.key}: visibility {ctx.options.visibility} not confirmed",
            )
            return False
    titles = ", ".join(f"{s.item.title} {s.version}" for s in staged)
    if not ctx.options.yes and not ctx.prompter.confirm(
        f"Upload to the Steam Workshop as {account}: {titles}?", "upload"
    ):
        ctx.reporter.record(STEP, Status.SKIP, "Workshop upload not confirmed")
        return False
    return True


def upload_all(ctx: Context, staged: Sequence[StagedItem], steamcmd: str, account: str) -> bool:
    """Stage every item and run SteamCMD once for all of them.

    Args:
        ctx: Run context.
        staged: Confirmed items.
        steamcmd: SteamCMD executable.
        account: Steam account name.

    Returns:
        True when SteamCMD reported success.
    """
    vdfs = [write_stage(ctx, item) for item in staged]
    argv = [steamcmd, "+login", account]
    for vdf in vdfs:
        argv += ["+workshop_build_item", str(vdf)]
    argv.append("+quit")
    ctx.reporter.line(Status.NOTE, quote(argv))
    ctx.reporter.line(
        Status.NOTE,
        "SteamCMD now asks for the password and Steam Guard code itself (not seen by this tool)",
    )
    code = ctx.runner.run_interactive(argv, cwd=ctx.out_dir)
    for item, vdf in zip(staged, vdfs, strict=True):
        if code == 0:
            ctx.reporter.record(STEP, Status.OK, f"{item.item.key} {item.version} uploaded")
        if not item.item.published_file_id:
            record_new_id(ctx, item, vdf, ask=code == 0)
    if code != 0:
        ctx.reporter.record(
            STEP,
            Status.FAIL,
            f"steamcmd exited with {code}; check its output (items with an ID in their VDF "
            "were created)",
        )
    return code == 0


def run(ctx: Context) -> bool:
    """Run the whole Workshop step.

    Args:
        ctx: Run context.

    Returns:
        True when nothing failed.
    """
    reporter = ctx.reporter
    keys = ctx.options.items or tuple(i.key for i in ctx.config.workshop.items)
    reporter.heading(f"Steam Workshop: {', '.join(keys)} (app {ctx.config.workshop.app_id})")
    staged = [s for s in (stage(ctx, ctx.config.item(key)) for key in keys) if s is not None]
    if len(staged) != len(keys) and not ctx.options.dry_run:
        return False
    for item in staged:
        describe(ctx, item)
    if not check_vdfs(ctx, staged) and not ctx.options.dry_run:
        return False
    account = steam_account(ctx)
    steamcmd = steamcmd_path(ctx)
    if steamcmd is None:
        status = Status.PLAN if ctx.options.dry_run else Status.FAIL
        reporter.record(STEP, status, "steamcmd not found (install it, or pass --steamcmd <path>)")
    if ctx.options.dry_run:
        argv = [steamcmd or "steamcmd", "+login", account or "<account>"]
        for item in staged:
            argv += ["+workshop_build_item", str(_base(ctx, item) / VDF_NAME)]
        show_plan(ctx, staged, [*argv, "+quit"])
        return True
    if account is None or steamcmd is None or not staged:
        return False
    if not confirm_all(ctx, staged, account):
        return not reporter.failed(STEP)
    return upload_all(ctx, staged, steamcmd, account)
