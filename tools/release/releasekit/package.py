"""Step 2, build: reviewed binaries, the release ZIP, the signed manifest.

Why the CAPIE DLLs are not simply recompiled here: MSVC output is not bit-reproducible
(PE timestamps), and the launcher and the proxy compile in the SHA-256 of the reviewed
``capie_core.dll``/``capie_editor.dll``/``capie_injector.exe`` (``DEPLOYMENT.md``). A fresh
build therefore never matches the pins. The step:

1. ``--rebuild-capie`` only: runs ``build.ps1`` (all offline tests) over the reviewed
   output folder and stops when the new hashes differ from ``DEPLOYMENT.md``, listing
   them for review and re-pinning.
2. Checks that the CAPIE binaries in the build folder are the reviewed ones.
3. Runs ``[[build.commands]]`` (launcher and proxy builds with their own tests).
4. Assembles the ZIP in the layout ``install.ps1`` expects (launcher at the root,
   ``runtime/``, ``proxy/version.dll``, ``mod/capie.mod`` + ``mod/capie/``), byte for byte
   reproducible from the same inputs (fixed timestamps, fixed order).
5. ``sign-release-manifest.ps1 -MakeManifest`` and ``-Sign`` (key path from the command
   line, refused inside the repository), then re-checks the manifest and the signature.
6. Writes ``SHA256SUMS.txt``, ``build-info.json`` and the installer pin block
   (``--write-pins`` puts it into ``Configuration.ps1`` and rebuilds ``install.ps1``).
"""

from __future__ import annotations

import hashlib
import json
import re
import zipfile
from dataclasses import dataclass
from pathlib import Path, PurePosixPath
from typing import TYPE_CHECKING

from releasekit import gitinfo
from releasekit.console import Status
from releasekit.runner import quote
from releasekit.signing import (
    PinnedRelease,
    SigningError,
    pinned_fields,
    public_key_from_xml,
    render_pinned_block,
    replace_pinned_block,
    reviewed_hashes,
    verify_signature,
)

if TYPE_CHECKING:
    from collections.abc import Sequence

    from releasekit.context import Context

STEP = "build"
"""Step name in the summary."""

MANIFEST_NAME = "release-manifest.json"
"""Manifest asset name (fixed by ``install.ps1``)."""

SIGNATURE_NAME = "release-manifest.json.sig"
"""Signature asset name (fixed by ``install.ps1``)."""

SUMS_NAME = "SHA256SUMS.txt"
"""Checksum list asset."""

BUILD_INFO_NAME = "build-info.json"
"""Local record of what went into the build (not uploaded)."""

PINS_NAME = "installer-pins.ps1.txt"
"""Rendered installer pin block (not uploaded)."""

ZIP_TIMESTAMP = (1980, 1, 1, 0, 0, 0)
"""Timestamp of every ZIP entry, so equal inputs give an equal ZIP."""

ZIP_FILE_MODE = 0o100644 << 16
"""Unix mode bits of every ZIP entry (regular file, rw-r--r--)."""

MANIFEST_PATH = re.compile(r"^[0-9A-Za-z._-]+(/[0-9A-Za-z._-]+)*$")
"""Allowed ZIP entry names (``$RaManifestPathPattern`` of the installer)."""

DOT_SEGMENT = re.compile(r"(^|/)\.\.?(/|$)")
"""``.`` or ``..`` path segment (refused by the installer)."""

ZIP_NAME = re.compile(r"^[0-9A-Za-z._-]+\.zip$")
"""Allowed ZIP file name (``$RaZipNamePattern`` of the installer)."""

TAIL_LINES = 20
"""Output lines of a failed build shown on the console."""


@dataclass(frozen=True)
class BundleEntry:
    """One file of the release ZIP."""

    name: str
    data: bytes

    @property
    def sha256(self) -> str:
        """Upper-case SHA-256 of the contents.

        Returns:
            Upper-case SHA-256 of the contents.
        """
        return hashlib.sha256(self.data).hexdigest().upper()


def sha256_bytes(data: bytes) -> str:
    """Hash bytes.

    Args:
        data: Bytes.

    Returns:
        Upper-case hexadecimal SHA-256.
    """
    return hashlib.sha256(data).hexdigest().upper()


def entry_problems(entries: Sequence[BundleEntry], launcher: str) -> list[str]:
    """Check entry names the way the installer will.

    Args:
        entries: ZIP entries.
        launcher: Required launcher entry name.

    Returns:
        One message per problem (empty when the bundle is acceptable).
    """
    problems = []
    seen: set[str] = set()
    for entry in entries:
        if not MANIFEST_PATH.match(entry.name) or DOT_SEGMENT.search(entry.name):
            problems.append(f"unsafe ZIP entry name {entry.name!r}")
        if entry.name.lower() in seen:
            problems.append(f"duplicate ZIP entry {entry.name!r}")
        seen.add(entry.name.lower())
    if launcher not in {e.name for e in entries}:
        problems.append(f"launcher {launcher} is missing")
    return problems


def write_zip(path: Path, entries: Sequence[BundleEntry]) -> None:
    """Write a reproducible ZIP (fixed order, timestamps and modes; no folder entries).

    Args:
        path: Output file.
        entries: Entries in ZIP order.
    """
    with zipfile.ZipFile(path, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        for entry in entries:
            info = zipfile.ZipInfo(entry.name, date_time=ZIP_TIMESTAMP)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = ZIP_FILE_MODE
            archive.writestr(info, entry.data)


def local_descriptor_lines(descriptor: str, name: str) -> list[str]:
    """Lines of ``descriptor.mod`` for the local copy: own name, no ``path`` or Workshop ID.

    HOI4 does not load a subscribed Workshop mod while a local descriptor with the same ``name``
    exists, so the local copy is named differently from the Workshop item.

    Args:
        descriptor: Text of the switch mod's ``descriptor.mod``.
        name: Name of the local copy.

    Returns:
        The lines, without line endings.
    """
    lines = []
    for line in descriptor.removeprefix("\ufeff").replace("\r\n", "\n").split("\n"):
        stripped = line.strip()
        if stripped.startswith(("path=", "path =", "remote_file_id")):
            continue
        lines.append(f'name="{name}"' if stripped.startswith(("name=", "name =")) else line)
    return lines


def switch_mod_descriptor(descriptor: str, path_value: str, name: str) -> bytes:
    """Build the local registration file (``capie.mod``) from ``descriptor.mod``.

    The Workshop descriptor has no ``path``; the copy in the user's mod folder needs one, and
    its own name (see ``local_descriptor_lines``).

    Args:
        descriptor: Text of the switch mod's ``descriptor.mod``.
        path_value: ``path`` value (``mod/capie``).
        name: Name of the local copy.

    Returns:
        UTF-8 bytes with LF line endings.
    """
    text = (
        "\n".join(local_descriptor_lines(descriptor, name)).rstrip("\n")
        + f'\npath="{path_value}"\n'
    )
    return text.encode("utf-8")


def local_inner_descriptor(descriptor: str, name: str) -> bytes:
    """The ``descriptor.mod`` inside the local copy's folder, with the local name.

    Args:
        descriptor: Text of the switch mod's ``descriptor.mod``.
        name: Name of the local copy.

    Returns:
        UTF-8 bytes with LF line endings.
    """
    return ("\n".join(local_descriptor_lines(descriptor, name)).rstrip("\n") + "\n").encode("utf-8")


def collect_entries(ctx: Context) -> tuple[list[BundleEntry], list[str]]:
    """Read every file that goes into the ZIP.

    Args:
        ctx: Run context.

    Returns:
        The entries in ZIP order and the missing required sources.
    """
    config = ctx.config
    entries: list[BundleEntry] = []
    missing: list[str] = []
    for spec in config.bundle.files:
        source = config.resolve(spec.source)
        if source.is_file():
            entries.append(BundleEntry(spec.zip_path, source.read_bytes()))
        elif not spec.optional:
            missing.append(spec.source)
    switch = config.bundle.switch_mod
    descriptor = config.resolve(switch.source_dir) / "descriptor.mod"
    if descriptor.is_file():
        entries.append(
            BundleEntry(
                switch.descriptor_zip_path,
                switch_mod_descriptor(
                    descriptor.read_text(encoding="utf-8"),
                    switch.descriptor_path_value,
                    switch.descriptor_name,
                ),
            )
        )
    else:
        missing.append(str(descriptor))
    prefix = PurePosixPath(switch.source_dir)
    for tracked in gitinfo.tracked_files(ctx.runner, config.root, switch.source_dir):
        relative = PurePosixPath(tracked).relative_to(prefix)
        data = (config.root / tracked).read_bytes()
        if relative.as_posix() == "descriptor.mod":
            data = local_inner_descriptor(data.decode("utf-8"), switch.descriptor_name)
        entries.append(BundleEntry(f"{switch.zip_dir}/{relative.as_posix()}", data))
    return entries, missing


def check_reviewed(ctx: Context, entries: Sequence[BundleEntry]) -> bool:
    """The CAPIE binaries (build folder and ZIP copies) are the reviewed ones.

    Args:
        ctx: Run context.
        entries: ZIP entries.

    Returns:
        True when every reviewed file matches ``DEPLOYMENT.md``.
    """
    config = ctx.config
    table = reviewed_hashes(
        config.resolve(config.build.reviewed_hashes).read_text(encoding="utf-8")
    )
    ok = True
    for name in config.build.reviewed_files:
        built = config.resolve(config.build.capie_output) / name
        expected = table.get(name)
        actual = sha256_bytes(built.read_bytes()) if built.is_file() else None
        if expected is None or actual != expected:
            ctx.reporter.record(
                STEP,
                Status.FAIL,
                f"{config.build.capie_output}/{name} {actual} != reviewed {expected}",
            )
            ok = False
    by_name = {e.name: e for e in entries}
    for spec in config.bundle.files:
        entry = by_name.get(spec.zip_path)
        if spec.reviewed and entry is not None:
            expected = table.get(PurePosixPath(spec.zip_path).name)
            if entry.sha256 != expected:
                ctx.reporter.record(
                    STEP, Status.FAIL, f"{spec.zip_path} in the bundle is not the reviewed build"
                )
                ok = False
    if ok:
        ctx.reporter.record(
            STEP,
            Status.OK,
            f"CAPIE binaries match the reviewed hashes ({config.build.reviewed_hashes})",
        )
    return ok


def rebuild_capie(ctx: Context) -> bool:
    """Run ``build.ps1`` and compare the new binaries with the reviewed hashes.

    Args:
        ctx: Run context.

    Returns:
        True when the build passed and still matches the reviewed hashes.
    """
    result = ctx.run_spec(STEP, ctx.config.build.rebuild_capie)
    if result is None:
        return ctx.options.dry_run
    if not result.ok:
        for line in result.output.strip().splitlines()[-TAIL_LINES:]:
            ctx.reporter.detail(line, echo=True)
        ctx.reporter.record(
            STEP, Status.FAIL, f"{ctx.config.build.rebuild_capie.name} (exit {result.returncode})"
        )
        return False
    ctx.reporter.record(STEP, Status.OK, ctx.config.build.rebuild_capie.name)
    output = ctx.config.resolve(ctx.config.build.capie_output)
    table = reviewed_hashes(
        ctx.config.resolve(ctx.config.build.reviewed_hashes).read_text(encoding="utf-8")
    )
    changed = []
    for name in ctx.config.build.reviewed_files:
        actual = sha256_bytes((output / name).read_bytes())
        if actual != table.get(name):
            changed.append(f"{name} {actual}")
    if changed:
        ctx.reporter.record(
            STEP,
            Status.FAIL,
            "new CAPIE binaries differ from the reviewed hashes "
            "(expected: MSVC is not reproducible). "
            "After review, re-pin them in DEPLOYMENT.md, build_launcher.ps1 and build_proxy.ps1, "
            "commit, "
            "and run 'build' again: " + "; ".join(changed),
        )
        return False
    return True


def run_build_commands(ctx: Context) -> bool:
    """Run the launcher and proxy builds.

    Args:
        ctx: Run context.

    Returns:
        True when every build passed (or was only shown).
    """
    ok = True
    for spec in ctx.config.build.commands:
        result = ctx.run_spec(STEP, spec)
        if result is None:
            continue
        if result.ok:
            ctx.reporter.record(STEP, Status.OK, spec.name)
            continue
        for line in result.output.strip().splitlines()[-TAIL_LINES:]:
            ctx.reporter.detail(line, echo=True)
        ctx.reporter.record(STEP, Status.FAIL, f"{spec.name} (exit {result.returncode})")
        ok = False
    return ok


def signing_key(ctx: Context) -> Path | None:
    """Validate the private key path (outside the repository, existing).

    Args:
        ctx: Run context.

    Returns:
        The key path, or ``None`` after recording why it cannot be used.
    """
    raw = ctx.options.signing_key
    if not raw:
        status = Status.PLAN if ctx.options.dry_run else Status.FAIL
        ctx.reporter.record(STEP, status, "no --signing-key: the manifest cannot be signed")
        return None
    key = Path(raw).expanduser().resolve()
    if key.is_relative_to(ctx.config.root):
        ctx.reporter.record(
            STEP,
            Status.FAIL,
            "the signing key is inside the repository; keep it outside (ADR-0031)",
        )
        return None
    if not key.is_file():
        ctx.reporter.record(STEP, Status.FAIL, f"signing key not found: {key}")
        return None
    return key


def _pwsh_script(ctx: Context, script: str, *args: str) -> tuple[str, ...]:
    """Command line of a repository PowerShell script.

    Args:
        ctx: Run context.
        script: Script path relative to the root.
        *args: Script arguments.

    Returns:
        The argv.
    """
    return ("pwsh", "-NoProfile", "-File", str(ctx.config.resolve(script)), *args)


def _run_checked(ctx: Context, name: str, argv: Sequence[str]) -> bool:
    """Run a command that must succeed, recording the result.

    Args:
        ctx: Run context.
        name: Label.
        argv: Command.

    Returns:
        True when it succeeded.
    """
    result = ctx.runner.run(
        argv,
        cwd=ctx.config.root,
        on_line=lambda line: ctx.reporter.detail(line, echo=ctx.options.verbose),
    )
    if not result.ok:
        for line in result.output.strip().splitlines()[-TAIL_LINES:]:
            ctx.reporter.detail(line, echo=True)
        ctx.reporter.record(STEP, Status.FAIL, f"{name} (exit {result.returncode})")
        return False
    ctx.reporter.record(STEP, Status.OK, name)
    return True


def check_manifest(
    ctx: Context, manifest: bytes, zip_name: str, zip_sha: str, entries: Sequence[BundleEntry]
) -> bool:
    """Compare the generated manifest with what was packed.

    Args:
        ctx: Run context.
        manifest: Manifest bytes.
        zip_name: ZIP file name.
        zip_sha: ZIP SHA-256.
        entries: ZIP entries.

    Returns:
        True when version, ZIP name and hash, every file hash and the game hash agree.
    """
    data = json.loads(manifest.decode("utf-8"))
    files = {str(k): str(v).upper() for k, v in dict(data.get("files", {})).items()}
    expected_files = {e.name: e.sha256 for e in entries}
    problems = []
    if data.get("version") != ctx.version:
        problems.append("version")
    if data.get("zipName") != zip_name or str(data.get("zipSha256", "")).upper() != zip_sha:
        problems.append("ZIP name or hash")
    if files != expected_files:
        problems.append("file list or hashes")
    if [str(h).upper() for h in data.get("supportedGameSha256", [])] != [ctx.capie.game_sha256]:
        problems.append("supported game hash")
    if problems:
        ctx.reporter.record(
            STEP, Status.FAIL, f"manifest disagrees with the ZIP: {', '.join(problems)}"
        )
        return False
    ctx.reporter.record(STEP, Status.OK, f"manifest lists {len(files)} files with their SHA-256")
    return True


def check_signature(ctx: Context, manifest: bytes, signature: str, key_path: Path) -> bool:
    """Verify the signature with the installer's public key (or the key's own public part).

    Args:
        ctx: Run context.
        manifest: Manifest bytes.
        signature: Base64 signature.
        key_path: Private key file (only Modulus and Exponent are read).

    Returns:
        True when the signature verifies.
    """
    installer = ctx.config.resolve(ctx.config.signing.installer_configuration).read_text(
        encoding="utf-8"
    )
    pinned = re.search(r"\$script:RaUpdatePublicKeyXml = '([^']*)'", installer)
    source = "installer public key"
    try:
        key = public_key_from_xml(pinned.group(1)) if pinned else None
    except SigningError:
        key = None
    if key is None:
        ctx.reporter.record(
            STEP,
            Status.WARN,
            "installer public key is a placeholder: install.ps1 -Update would refuse this release",
        )
        key = public_key_from_xml(key_path.read_text(encoding="utf-8"))
        source = "public part of the signing key"
    if not verify_signature(key, manifest, signature):
        ctx.reporter.record(STEP, Status.FAIL, f"signature does not verify with the {source}")
        return False
    ctx.reporter.record(STEP, Status.OK, f"signature verifies with the {source} ({key.bits} bit)")
    return True


def write_pins(ctx: Context, pinned: PinnedRelease) -> None:
    """Write the pin block file and compare it with (or write it into) the installer.

    Args:
        ctx: Run context.
        pinned: Release values.
    """
    block = render_pinned_block(pinned)
    (ctx.out_dir / PINS_NAME).write_text(block + "\n", encoding="ascii", newline="\n")
    configuration = ctx.config.resolve(ctx.config.signing.installer_configuration)
    raw = configuration.read_bytes()
    text = raw.decode("utf-8").replace("\r\n", "\n")
    fields = pinned_fields(text)
    if fields.get("ZipSha256") == pinned.zip_sha256 and fields.get("Version") == pinned.version:
        ctx.reporter.record(STEP, Status.OK, "installer pins already name this ZIP")
        return
    if not ctx.options.write_pins:
        ctx.reporter.record(
            STEP,
            Status.WARN,
            f"installer pins name {fields.get('ZipName')!r}: run 'build --write-pins' "
            f"(or paste {PINS_NAME}), rebuild install.ps1 and commit before 'release'",
        )
        return
    new_text = replace_pinned_block(text, block)
    newline = "\r\n" if b"\r\n" in raw else "\n"
    configuration.write_bytes(new_text.replace("\n", newline).encode("utf-8"))
    ctx.reporter.record(
        STEP, Status.OK, f"pins written to {ctx.config.signing.installer_configuration}"
    )
    _run_checked(
        ctx, "install.ps1 rebuilt", _pwsh_script(ctx, ctx.config.signing.installer_build_script)
    )
    ctx.reporter.record(
        STEP,
        Status.NOTE,
        "commit Configuration.ps1 and install.ps1, then run 'release' from that commit",
    )


def plan(
    ctx: Context, entries: Sequence[BundleEntry], missing: Sequence[str], zip_name: str
) -> None:
    """Show what a real build would do.

    Args:
        ctx: Run context.
        entries: Entries that exist now.
        missing: Required sources that do not exist yet.
        zip_name: ZIP file name.
    """
    for spec in ctx.config.build.commands:
        ctx.run_spec(STEP, spec)
    out = ctx.out_dir
    ctx.reporter.line(Status.PLAN, f"write {out / zip_name} with {len(entries)} entries:")
    for entry in entries:
        ctx.reporter.line(
            Status.PLAN, f"  {entry.name}  {entry.sha256[:16]}...  ({len(entry.data)} bytes)"
        )
    for source in missing:
        ctx.reporter.record(STEP, Status.WARN, f"not built yet: {source}")
    sign = ctx.config.signing.sign_script
    ctx.reporter.line(
        Status.PLAN,
        quote(
            _pwsh_script(
                ctx,
                sign,
                "-MakeManifest",
                "-ZipPath",
                str(out / zip_name),
                "-Version",
                ctx.version,
                "-SupportedGameSha256",
                ctx.capie.game_sha256,
                "-OutPath",
                str(out / MANIFEST_NAME),
            )
        ),
    )
    key = ctx.options.signing_key or "<--signing-key>"
    ctx.reporter.line(
        Status.PLAN,
        quote(
            _pwsh_script(
                ctx,
                sign,
                "-Sign",
                "-ManifestPath",
                str(out / MANIFEST_NAME),
                "-PrivateKeyPath",
                key,
            )
        ),
    )
    ctx.reporter.line(
        Status.PLAN,
        f"write {SUMS_NAME}, {BUILD_INFO_NAME}, {PINS_NAME}"
        + (" and the installer pins" if ctx.options.write_pins else ""),
    )


def run(ctx: Context) -> bool:
    """Run the whole build step.

    Args:
        ctx: Run context.

    Returns:
        True when nothing failed.
    """
    reporter = ctx.reporter
    reporter.heading(f"Build: CAPIE {ctx.version} release bundle")
    zip_name = ctx.config.bundle.zip_name.format(version=ctx.version)
    if not ZIP_NAME.match(zip_name):
        reporter.record(
            STEP, Status.FAIL, f"ZIP name {zip_name!r} is not accepted by the installer"
        )
        return False
    if ctx.options.rebuild_capie and not rebuild_capie(ctx):
        return False
    key = signing_key(ctx)
    if ctx.options.dry_run:
        entries, missing = collect_entries(ctx)
        check_reviewed(ctx, entries)
        plan(ctx, entries, missing, zip_name)
        return not reporter.failed(STEP)
    if key is None or not run_build_commands(ctx):
        return False
    entries, missing = collect_entries(ctx)
    for source in missing:
        reporter.record(STEP, Status.FAIL, f"missing bundle file: {source}")
    problems = entry_problems(entries, ctx.config.bundle.launcher_zip_path)
    for problem in problems:
        reporter.record(STEP, Status.FAIL, problem)
    if missing or problems or not check_reviewed(ctx, entries):
        return False
    return _package(ctx, entries, zip_name, key)


def _package(ctx: Context, entries: Sequence[BundleEntry], zip_name: str, key: Path) -> bool:
    """Write the ZIP, manifest, signature, checksums and build record.

    Args:
        ctx: Run context.
        entries: Checked ZIP entries.
        zip_name: ZIP file name.
        key: Private key file.

    Returns:
        True when nothing failed.
    """
    out = ctx.out_dir
    out.mkdir(parents=True, exist_ok=True)
    zip_path = out / zip_name
    write_zip(zip_path, entries)
    zip_sha = sha256_bytes(zip_path.read_bytes())
    ctx.reporter.record(STEP, Status.OK, f"{zip_name} {zip_sha} ({len(entries)} files)")
    sign = ctx.config.signing.sign_script
    manifest_path = out / MANIFEST_NAME
    made = _run_checked(
        ctx,
        "manifest created",
        _pwsh_script(
            ctx,
            sign,
            "-MakeManifest",
            "-ZipPath",
            str(zip_path),
            "-Version",
            ctx.version,
            "-SupportedGameSha256",
            ctx.capie.game_sha256,
            "-OutPath",
            str(manifest_path),
        ),
    )
    if not made or not _run_checked(
        ctx,
        "manifest signed",
        _pwsh_script(
            ctx, sign, "-Sign", "-ManifestPath", str(manifest_path), "-PrivateKeyPath", str(key)
        ),
    ):
        return False
    manifest = manifest_path.read_bytes()
    signature = (out / SIGNATURE_NAME).read_text(encoding="utf-8")
    if not check_manifest(ctx, manifest, zip_name, zip_sha, entries) or not check_signature(
        ctx, manifest, signature, key
    ):
        return False
    assets = [zip_path, manifest_path, out / SIGNATURE_NAME]
    sums = "".join(f"{sha256_bytes(p.read_bytes()).lower()}  {p.name}\n" for p in assets)
    (out / SUMS_NAME).write_text(sums, encoding="ascii", newline="\n")
    commit = gitinfo.head_commit(ctx.runner, ctx.config.root)
    info = {
        "version": ctx.version,
        "commit": commit,
        "zip": {"name": zip_name, "sha256": zip_sha},
        "files": {e.name: e.sha256 for e in entries},
        "supportedGameSha256": [ctx.capie.game_sha256],
        "builtLocally": True,
    }
    (out / BUILD_INFO_NAME).write_text(
        json.dumps(info, indent=2) + "\n", encoding="utf-8", newline="\n"
    )
    write_pins(
        ctx,
        PinnedRelease(
            ctx.version,
            zip_name,
            zip_sha,
            tuple((e.name, e.sha256) for e in entries),
            (ctx.capie.game_sha256,),
        ),
    )
    ctx.reporter.record(STEP, Status.OK, f"release files in {out}")
    return not ctx.reporter.failed(STEP)
