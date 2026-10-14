"""Step 5, Steam Workshop: stage each mod and upload it with SteamCMD.

For every item in ``[[workshop.items]]`` (or the ``--item`` ones):

1. Stage a fresh copy of the mod: only files tracked by git under ``content_dir``,
   minus the ``exclude`` patterns (developer files). ``descriptor.mod`` is rewritten the
   way the Paradox Launcher publishes a mod: no ``path``/``archive`` line, and
   ``remote_file_id`` set once the item has an ID. The local ``*.mod`` registration file
   is never uploaded.
2. Write ``item.vdf`` for ``workshop_build_item``: ``appid`` 394360, ``publishedfileid``
   (0 = new item), ``contentfolder``, ``previewfile``, ``changenote`` (from the item's
   changelog, converted to BBCode). A new item also gets ``title``, ``description`` and
   ``visibility`` 2 (Hidden, ``k_ERemoteStoragePublishedFileVisibilityPrivate``). An
   existing item keeps its visibility, title and description unless ``--visibility`` or
   ``--update-description`` say otherwise; any visibility other than hidden needs a
   typed confirmation.
3. Run ``steamcmd +login <account> +workshop_build_item <vdf> +quit`` on the console.
   Only the account name is passed. SteamCMD asks for the password and the Steam Guard
   code itself; this tool never accepts, stores or logs them, and does not capture
   SteamCMD's output at all.
4. SteamCMD writes the new ``publishedfileid`` back into the VDF; the tool stores it in
   ``release.toml``. If it cannot be read, the maintainer may type it in.
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
from releasekit.versions import VersionError, descriptor_fields, descriptor_version

if TYPE_CHECKING:
    from releasekit.config import WorkshopItem
    from releasekit.context import Context

STEP = "workshop"
"""Step name in the summary."""

DESCRIPTOR = "descriptor.mod"
"""Descriptor file name inside a mod folder."""

PREVIEW_MAX_BYTES = 1024 * 1024
"""Largest preview image Steam and the Paradox Launcher accept."""

PREVIEW_SUFFIXES = (".png", ".jpg", ".jpeg", ".gif")
"""Preview image formats Steam accepts."""

HIDDEN = "hidden"
"""Default visibility of a new item."""

STEAM_ACCOUNT = re.compile(r"^[A-Za-z0-9_]{2,64}$")
"""A Steam account name (never a password: no other characters are accepted)."""

_DROPPED_DESCRIPTOR_KEYS = ("path", "archive", "remote_file_id")
"""Descriptor keys that are machine-specific or set by this tool."""

_VDF_ID = re.compile(r'"publishedfileid"\s+"(\d+)"')
"""``publishedfileid`` line of a VDF."""


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


def vdf_quote(value: str) -> str:
    """Quote a VDF string.

    Args:
        value: Text.

    Returns:
        The quoted text with backslashes and quotes escaped.
    """
    return '"' + value.replace("\\", "\\\\").replace('"', '\\"') + '"'


def render_vdf(fields: list[tuple[str, str]]) -> str:
    """Render a ``workshopitem`` VDF.

    Args:
        fields: Keys and values in output order.

    Returns:
        VDF text.
    """
    body = "".join(f"\t{vdf_quote(k)}\t{vdf_quote(v)}\n" for k, v in fields)
    return '"workshopitem"\n{\n' + body + "}\n"


def vdf_published_id(text: str) -> int:
    """Read ``publishedfileid`` from a VDF.

    Args:
        text: VDF text.

    Returns:
        The ID (0 when missing).
    """
    match = _VDF_ID.search(text)
    return int(match.group(1)) if match else 0


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
    """Fields of the item's VDF.

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
        ("contentfolder", str(content)),
        ("previewfile", str(preview)),
    ]
    if staged.visibility is not None:
        fields.append(("visibility", str(staged.visibility)))
    if not item.published_file_id or description is not None:
        fields.append(("title", item.title))
    if description is not None:
        fields.append(("description", description))
    fields.append(("changenote", staged.changenote))
    return fields


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
    notes_path = ctx.config.resolve(item.changelog)
    notes = changelog.section(
        notes_path.read_text(encoding="utf-8") if notes_path.is_file() else "", version
    )
    if notes is None:
        problems.append(f"{item.changelog} has no section [{version}] for the change note")
    changenote = changelog.to_bbcode(notes or "")
    if len(changenote) > changelog.STEAM_CHANGENOTE_LIMIT:
        problems.append("change note longer than 8000 characters")
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


def write_stage(ctx: Context, staged: StagedItem) -> tuple[Path, Path]:
    """Copy the content into a fresh folder and write the VDF.

    Args:
        ctx: Run context.
        staged: Staged item.

    Returns:
        Content folder and VDF path.
    """
    base = ctx.out_dir / "workshop" / f"{staged.item.key}-{staged.version}"
    content = base / "content"
    if content.exists():
        shutil.rmtree(content)
    for relative, source in staged.files:
        target = content / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target)
    (content / DESCRIPTOR).write_text(staged.descriptor, encoding="utf-8", newline="\n")
    description = _description(ctx, staged.item)
    preview = ctx.config.resolve(staged.item.preview_file)
    vdf = base / "item.vdf"
    vdf.write_text(
        render_vdf(
            vdf_fields(
                staged,
                content.resolve(),
                preview.resolve(),
                ctx.config.workshop.app_id,
                description,
            )
        ),
        encoding="utf-8",
        newline="\n",
    )
    return content, vdf


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


def record_new_id(ctx: Context, staged: StagedItem, vdf: Path) -> None:
    """Store the ID of a newly created item in ``release.toml``.

    Args:
        ctx: Run context.
        staged: Staged item.
        vdf: VDF that SteamCMD updated.
    """
    new_id = vdf_published_id(vdf.read_text(encoding="utf-8"))
    if not new_id:
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
            f"set published_file_id in {ctx.config.path.name} by hand",
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


def upload(ctx: Context, staged: StagedItem, steamcmd: str, account: str) -> bool:
    """Stage, run SteamCMD and record the result for one item.

    Args:
        ctx: Run context.
        staged: Staged item.
        steamcmd: SteamCMD executable.
        account: Steam account name.

    Returns:
        True when SteamCMD reported success.
    """
    item = staged.item
    kind = f"update of {item.published_file_id}" if item.published_file_id else "NEW item"
    visibility = (
        "unchanged"
        if staged.visibility is None
        else next(k for k, v in VISIBILITY_VALUES.items() if v == staged.visibility)
    )
    ctx.reporter.line(
        Status.NOTE,
        f"{item.title} {staged.version}: {kind}, visibility {visibility}, "
        f"{len(staged.files) + 1} files",
    )
    if not confirm_visibility(ctx, staged):
        ctx.reporter.record(
            STEP, Status.FAIL, f"{item.key}: visibility {ctx.options.visibility} not confirmed"
        )
        return False
    if ctx.options.dry_run:
        base = ctx.out_dir / "workshop" / f"{item.key}-{staged.version}"
        preview = ctx.config.resolve(item.preview_file)
        vdf_text = render_vdf(
            vdf_fields(
                staged,
                base / "content",
                preview,
                ctx.config.workshop.app_id,
                _description(ctx, item),
            )
        )
        for relative, _ in staged.files:
            ctx.reporter.line(Status.PLAN, f"  stage {relative}")
        ctx.reporter.line(Status.PLAN, f"  stage {DESCRIPTOR} (rewritten):")
        for line in staged.descriptor.splitlines():
            ctx.reporter.line(Status.PLAN, f"    {line}")
        ctx.reporter.line(Status.PLAN, f"  {base / 'item.vdf'}:")
        for line in vdf_text.splitlines():
            ctx.reporter.line(Status.PLAN, f"    {line}")
        ctx.reporter.line(
            Status.PLAN,
            quote(
                [
                    steamcmd,
                    "+login",
                    account,
                    "+workshop_build_item",
                    str(base / "item.vdf"),
                    "+quit",
                ]
            ),
        )
        return True
    _, vdf = write_stage(ctx, staged)
    argv = [steamcmd, "+login", account, "+workshop_build_item", str(vdf), "+quit"]
    ctx.reporter.line(Status.NOTE, quote(argv))
    ctx.reporter.line(
        Status.NOTE,
        "SteamCMD now asks for the password and Steam Guard code itself (not seen by this tool)",
    )
    code = ctx.runner.run_interactive(argv, cwd=vdf.parent)
    if code != 0:
        ctx.reporter.record(STEP, Status.FAIL, f"{item.key}: steamcmd exited with {code}")
        return False
    ctx.reporter.record(STEP, Status.OK, f"{item.key} {staged.version} uploaded ({kind})")
    if not item.published_file_id:
        record_new_id(ctx, staged, vdf)
    return True


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
    account = steam_account(ctx)
    steamcmd = steamcmd_path(ctx)
    if steamcmd is None:
        status = Status.PLAN if ctx.options.dry_run else Status.FAIL
        reporter.record(STEP, status, "steamcmd not found (install it, or pass --steamcmd <path>)")
    if not ctx.options.dry_run:
        if account is None or steamcmd is None:
            return False
        titles = ", ".join(f"{s.item.title} {s.version}" for s in staged)
        if not ctx.options.yes and not ctx.prompter.confirm(
            f"Upload to the Steam Workshop as {account}: {titles}?", "upload"
        ):
            reporter.record(STEP, Status.SKIP, "Workshop upload not confirmed")
            return True
    ok = True
    for item in staged:
        ok = upload(ctx, item, steamcmd or "steamcmd", account or "<account>") and ok
    return ok
