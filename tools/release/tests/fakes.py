"""Test doubles and a synthetic repository for the release tool tests.

Nothing here starts a process or opens a network connection: :class:`FakeRunner`
answers git, gh, pwsh and steamcmd from Python, :class:`FakeTransport` answers the
VirusTotal API, and :func:`make_repo` writes a small repository with every file the
configuration points to.
"""

from __future__ import annotations

import base64
import hashlib
import io
import json
import random
import zipfile
from dataclasses import dataclass, field
from pathlib import Path
from typing import TYPE_CHECKING

from releasekit.cli import main
from releasekit.runner import CommandResult
from releasekit.signing import pkcs1_sha256_block
from releasekit.vdf import parse_vdf
from releasekit.virustotal import HttpResponse

if TYPE_CHECKING:
    from collections.abc import Callable, Mapping, Sequence

COMMIT = "0123456789abcdef0123456789abcdef01234567"
"""Commit SHA the fake git reports for ``HEAD``."""

GAME_SHA = "7DC947BE34970DA1E1C787BCDDBE7F62B610FF258AEBF8F06AA8B348F3A031D5"
"""Supported game hash written into the synthetic ``game_build.hpp``."""

BINARIES = {
    "capie_core.dll": b"MZ core",
    "capie_editor.dll": b"MZ editor",
    "capie_injector.exe": b"MZ injector",
}
"""Fake reviewed CAPIE binaries."""

PNG = b"\x89PNG\r\n\x1a\n" + b"\0" * 32
"""A tiny file that starts like a PNG."""

KEY_BITS = 512
"""Size of the throw-away test key (big enough for a PKCS#1 SHA-256 block)."""

MR_ROUNDS = 24
"""Miller-Rabin rounds of the test prime generator."""

PUBLIC_EXPONENT = 65537
"""RSA public exponent."""

SMALL_PRIMES = (3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37)
"""Trial divisors before Miller-Rabin."""


def _b64(value: int) -> str:
    """Big-endian Base64 of an integer.

    Args:
        value: Integer.

    Returns:
        Base64 text.
    """
    return base64.b64encode(value.to_bytes((value.bit_length() + 7) // 8, "big")).decode()


def _is_probable_prime(n: int, rng: random.Random) -> bool:
    """Miller-Rabin test.

    Args:
        n: Candidate.
        rng: Random source.

    Returns:
        True when ``n`` is probably prime.
    """
    if any(n % p == 0 for p in SMALL_PRIMES):
        return n in SMALL_PRIMES
    d, s = n - 1, 0
    while d % 2 == 0:
        d, s = d // 2, s + 1
    for _ in range(MR_ROUNDS):
        x = pow(rng.randrange(2, n - 1), d, n)
        if x in {1, n - 1}:
            continue
        for _ in range(s - 1):
            x = pow(x, 2, n)
            if x == n - 1:
                break
        else:
            return False
    return True


@dataclass(frozen=True)
class KeyPair:
    """A throw-away RSA key pair."""

    modulus: int
    private_exponent: int

    def xml(self, *, private: bool) -> str:
        """.NET ``RSAKeyValue`` XML.

        Args:
            private: Include the private exponent.

        Returns:
            Key XML.
        """
        extra = f"<D>{_b64(self.private_exponent)}</D>" if private else ""
        modulus = f"<Modulus>{_b64(self.modulus)}</Modulus>"
        return f"<RSAKeyValue>{modulus}<Exponent>AQAB</Exponent>{extra}</RSAKeyValue>"

    def sign(self, data: bytes) -> str:
        """Sign like ``sign-release-manifest.ps1 -Sign``.

        Args:
            data: Bytes to sign.

        Returns:
            Base64 signature.
        """
        size = (self.modulus.bit_length() + 7) // 8
        block = int.from_bytes(pkcs1_sha256_block(data, size), "big")
        return base64.b64encode(
            pow(block, self.private_exponent, self.modulus).to_bytes(size, "big")
        ).decode()


def make_key(seed: int) -> KeyPair:
    """Generate a deterministic throw-away key.

    Args:
        seed: Random seed.

    Returns:
        The key.
    """
    rng = random.Random(seed)  # noqa: S311 - test key, not a secret

    def prime() -> int:
        """One prime of half the key size.

        Returns:
            The prime.
        """
        while True:
            candidate = rng.getrandbits(KEY_BITS // 2) | (1 << (KEY_BITS // 2 - 1)) | 1
            if candidate % PUBLIC_EXPONENT != 1 and _is_probable_prime(candidate, rng):
                return candidate

    p, q = prime(), prime()
    return KeyPair(p * q, pow(PUBLIC_EXPONENT, -1, (p - 1) * (q - 1)))


CONFIG = """
root = "../.."
branch = "main"
out_dir = "dist/release"
game_dir = ""

[versions]
capie_header = "plugin/capie/src/core/game_build.hpp"
launcher_build_script = "plugin/capie/build_launcher.ps1"
launcher_config = "plugin/capie/src/launcher/config.hpp"

[preflight]
[[preflight.commands]]
name = "unit tests"
argv = ["{python}", "-m", "pytest"]
[[preflight.commands]]
name = "generator"
argv = ["{python}", "tools/gen.py", "--game", "{game_dir}"]
clean_after = ["mod"]

[build]
capie_output = "plugin/build/capie"
reviewed_hashes = "plugin/capie/deploy/DEPLOYMENT.md"
reviewed_files = ["capie_core.dll", "capie_editor.dll", "capie_injector.exe"]
[build.rebuild_capie]
name = "capie build"
argv = ["{pwsh}", "-File", "plugin/capie/build.ps1", "-MinHookDir", "{minhook_dir}"]
[[build.commands]]
name = "launcher build"
argv = ["{pwsh}", "-File", "plugin/capie/build_launcher.ps1"]

[bundle]
zip_name = "CAPIE-{version}-win64.zip"
launcher_zip_path = "RenameAnythingLauncher.exe"
[[bundle.files]]
zip = "RenameAnythingLauncher.exe"
source = "plugin/build/capie-launcher/RenameAnythingLauncher.exe"
[[bundle.files]]
zip = "runtime/capie_core.dll"
source = "plugin/build/capie-launcher/runtime/capie_core.dll"
reviewed = true
[[bundle.files]]
zip = "runtime/capie_editor.dll"
source = "plugin/build/capie-launcher/runtime/capie_editor.dll"
reviewed = true
[[bundle.files]]
zip = "runtime/capie_injector.exe"
source = "plugin/build/capie-launcher/runtime/capie_injector.exe"
reviewed = true
[[bundle.files]]
zip = "proxy/version.dll"
source = "plugin/build/capie-proxy/version.dll"
optional = true
[bundle.switch_mod]
source_dir = "plugin/capie/toggle-mod/capie"
zip_dir = "mod/capie"
descriptor_zip_path = "mod/capie.mod"
descriptor_path_value = "mod/capie"

[signing]
sign_script = "tools/bootstrap/sign-release-manifest.ps1"
installer_configuration = "tools/bootstrap/src/Shared/Configuration.ps1"
installer_build_script = "tools/bootstrap/build-install.ps1"
installer_script = "tools/bootstrap/install.ps1"

[github]
repo = "capie-mods/capie"
tag_prefix = "v"
title = "CAPIE {version}"
changelog = "plugin/capie/CHANGELOG.md"

[virustotal]
api_key_env = "VT_API_KEY"
poll_seconds = 1
max_wait_seconds = 5
request_gap_seconds = 0

[workshop]
app_id = 394360
steamcmd = "steamcmd"
steam_user = ""
exclude = ["*.md", ".*", "__pycache__"]

[[workshop.items]]
key = "rename-anything"
title = "Rename Anything"
content_dir = "mod"
preview_file = "mod/thumbnail.png"
description_file = ""
changelog = "CHANGELOG.md"
published_file_id = 0

[[workshop.items]]
key = "capie-switch"
title = "CAPIE"
content_dir = "plugin/capie/toggle-mod/capie"
preview_file = "plugin/capie/toggle-mod/capie/thumbnail.png"
description_file = "plugin/capie/toggle-mod/workshop-description.en.txt"
changelog = "plugin/capie/toggle-mod/CHANGELOG.md"
published_file_id = 3000000001
"""
"""Configuration of the synthetic repository (same shape as the real ``release.toml``)."""

CONFIGURATION_PS1 = """# Release configuration
$script:RaGitHubRepo = 'capie-mods/capie'

$script:RaUpdatePublicKeyXml = '{public_key}'

$script:RaPinnedRelease = [ordered]@{{
    Version             = '0.1.0'
    ZipName             = 'RenameAnything-0.1.0-win64.zip'
    ZipSha256           = 'TO-BE-PINNED-AT-RELEASE'
    Files               = [ordered]@{{
        'RenameAnythingLauncher.exe' = '{launcher}'
    }}
    SupportedGameSha256 = @('{game}')
}}
"""
"""Template of the installer configuration of the synthetic repository."""


def _write(root: Path, relative: str, data: str | bytes) -> None:
    """Write one file below the repository root.

    Args:
        root: Repository root.
        relative: POSIX path.
        data: Text (written with LF) or bytes.
    """
    path = root / relative
    path.parent.mkdir(parents=True, exist_ok=True)
    if isinstance(data, bytes):
        path.write_bytes(data)
    else:
        path.write_text(data, encoding="utf-8", newline="\n")


def make_repo(root: Path, key: KeyPair, *, installer_key: bool = True) -> Path:
    """Write the synthetic repository.

    Args:
        root: Empty folder.
        key: Signing key whose public part the installer pins.
        installer_key: Pin the public key (False keeps the placeholder).

    Returns:
        Path of its ``release.toml``.
    """
    files: dict[str, str | bytes] = {
        "tools/release/release.toml": CONFIG,
        "plugin/capie/src/core/game_build.hpp": (
            f'inline constexpr char game_exe_sha256[] = "{GAME_SHA.lower()}";\n'
            'inline constexpr char game_version[] = "1.19.3.0";\n'
            'inline constexpr char capie_version[] = "1.4";\n'
            'inline constexpr char capie_release[] = "1.4.0";\n'
            "inline constexpr unsigned capie_api_level = 6;\n"
            "inline constexpr unsigned capie_build_number = 10400;\n"
        ),
        "plugin/capie/build_launcher.ps1": "$script:LauncherVersion = '1.0.0'\n",
        "plugin/capie/src/launcher/config.hpp": (
            'inline constexpr char capie_mod_workshop_id[] = "3000000001";\n'
        ),
        "plugin/capie/deploy/DEPLOYMENT.md": "| File | SHA-256 |\n| --- | --- |\n"
        + "".join(
            f"| {name} | {hashlib.sha256(data).hexdigest().upper()} |\n"
            for name, data in BINARIES.items()
        ),
        "plugin/build/capie-launcher/RenameAnythingLauncher.exe": b"MZ launcher",
        "plugin/capie/CHANGELOG.md": (
            "# CAPIE\n\n## [Unreleased]\n\n## [1.4.0] - 2026-10-14\n\n- Flags.\n"
        ),
        "plugin/capie/toggle-mod/CHANGELOG.md": "## [1.0.0]\n\n- **First** release.\n",
        "plugin/capie/toggle-mod/workshop-description.en.txt": (
            '[h1]CAPIE[/h1] shows "CAPIE 1.4".\n'
        ),
        "plugin/capie/toggle-mod/capie/descriptor.mod": (
            'version="1.0.0"\nname="CAPIE"\npicture="thumbnail.png"\nsupported_version="1.19.*"\n'
        ),
        "plugin/capie/toggle-mod/capie/thumbnail.png": PNG,
        "plugin/capie/toggle-mod/capie/interface/capie.gui": "guiTypes = {}\n",
        "mod/descriptor.mod": (
            'version="0.1.0"\nname="Rename Anything"\npath="C:/dev/mod"\n'
            'supported_version="1.19.*"\n'
        ),
        "mod/thumbnail.png": PNG,
        "mod/common/rn.txt": "rn = yes\n",
        "mod/NOTES.md": "developer notes\n",
        "CHANGELOG.md": "## [0.1.0]\n\n### Added\n\n- Renaming.\n",
        "tools/bootstrap/install.ps1": "# installer\n",
        "tools/bootstrap/src/Shared/Configuration.ps1": CONFIGURATION_PS1.format(
            public_key=key.xml(private=False) if installer_key else "TO-BE-GENERATED",
            game=GAME_SHA,
            launcher="1" * 64,
        ),
    }
    for name, data in BINARIES.items():
        files[f"plugin/build/capie/{name}"] = data
        files[f"plugin/build/capie-launcher/runtime/{name}"] = data
    for relative, data in files.items():
        _write(root, relative, data)
    return root / "tools/release/release.toml"


def manifest_json(zip_path: Path, version: str, game: str) -> str:
    """What ``sign-release-manifest.ps1 -MakeManifest`` writes.

    Args:
        zip_path: Release ZIP.
        version: Version.
        game: Supported game hash.

    Returns:
        Manifest JSON.
    """
    with zipfile.ZipFile(zip_path) as archive:
        files = {
            i.filename: hashlib.sha256(archive.read(i)).hexdigest().upper()
            for i in archive.infolist()
        }
    return json.dumps(
        {
            "schema": 1,
            "version": version,
            "zipName": zip_path.name,
            "zipSha256": hashlib.sha256(zip_path.read_bytes()).hexdigest().upper(),
            "files": files,
            "supportedGameSha256": [game.upper()],
        },
        indent=2,
    )


def _arg(argv: Sequence[str], name: str) -> str:
    """Value after a ``-Name`` argument.

    Args:
        argv: Command line.
        name: Parameter name with dash.

    Returns:
        The value.
    """
    return argv[list(argv).index(name) + 1]


@dataclass
class FakeRunner:
    """Answers every command the tool starts, and records them."""

    root: Path
    key: KeyPair
    status: str = ""
    branch: str = "main"
    release_exists: bool = False
    commit_on_remote: bool = True
    steam_id: int = 3000000099
    failing: tuple[str, ...] = ()
    steamcmd_running: bool = False
    steamcmd_exit: int = 0
    calls: list[tuple[str, ...]] = field(default_factory=list)
    interactive: list[tuple[str, ...]] = field(default_factory=list)
    on_run: Callable[[tuple[str, ...]], None] | None = None

    def run(
        self,
        argv: Sequence[str],
        *,
        cwd: Path,
        env: Mapping[str, str] | None = None,
        on_line: Callable[[str], None] | None = None,
    ) -> CommandResult:
        """Answer one captured command.

        Args:
            argv: Command line.
            cwd: Working folder (unused).
            env: Extra environment (unused).
            on_line: Output callback.

        Returns:
            The scripted result.
        """
        del cwd, env
        call = tuple(argv)
        self.calls.append(call)
        if self.on_run is not None:
            self.on_run(call)
        code, output = self._answer(call)
        if on_line is not None:
            for line in output.splitlines():
                on_line(line)
        return CommandResult(call, code, output)

    def _answer(self, call: tuple[str, ...]) -> tuple[int, str]:  # noqa: PLR0911 - one branch per tool
        """Scripted answer.

        Args:
            call: Command line.

        Returns:
            Exit code and output.
        """
        joined = " ".join(call)
        if any(name in joined for name in self.failing):
            return 1, "simulated failure"
        if call[0] == "tasklist":
            listed = (
                '"steamcmd.exe","4242","Console","1","9,000 K"' if self.steamcmd_running else ""
            )
            return 0, listed or "INFO: No tasks are running which match the specified criteria."
        if call[:2] == ("git", "status"):
            return 0, self.status
        if call[:3] == ("git", "rev-parse", "--abbrev-ref"):
            return 0, self.branch + "\n"
        if call[:2] == ("git", "rev-parse"):
            return 0, COMMIT + "\n"
        if call[:2] == ("git", "ls-files"):
            folder = self.root / call[-1]
            found = sorted(
                p.relative_to(self.root).as_posix() for p in folder.rglob("*") if p.is_file()
            )
            return 0, "\0".join(found)
        if call[0] == "gh":
            return self._gh(call)
        if "-MakeManifest" in call:
            zip_path = Path(_arg(call, "-ZipPath"))
            text = manifest_json(
                zip_path, _arg(call, "-Version"), _arg(call, "-SupportedGameSha256")
            )
            Path(_arg(call, "-OutPath")).write_text(text, encoding="utf-8")
            return 0, "Wrote manifest"
        if "-Sign" in call:
            manifest = Path(_arg(call, "-ManifestPath"))
            Path(str(manifest) + ".sig").write_text(
                self.key.sign(manifest.read_bytes()), encoding="utf-8"
            )
            return 0, "Wrote sig"
        return 0, "ok"

    def _gh(self, call: tuple[str, ...]) -> tuple[int, str]:
        """Scripted gh answers.

        Args:
            call: gh command line.

        Returns:
            Exit code and output.
        """
        if call[1:3] == ("release", "view"):
            return (0, "exists") if self.release_exists else (1, "release not found")
        if call[1] == "api":
            return (0, "") if self.commit_on_remote else (1, "Not Found")
        if call[1:3] == ("release", "create"):
            return 0, "https://github.com/capie-mods/capie/releases/tag/v1.4.0"
        return 0, ""

    def run_interactive(self, argv: Sequence[str], *, cwd: Path) -> int:
        """Pretend to be SteamCMD: write the new ID into the VDF like the real one.

        Args:
            argv: Command line.
            cwd: Working folder (unused).

        Returns:
            0.
        """
        del cwd
        call = tuple(argv)
        self.interactive.append(call)
        new_id = self.steam_id
        for index, arg in enumerate(call):
            if arg != "+workshop_build_item":
                continue
            vdf = Path(call[index + 1])
            text = vdf.read_text(encoding="utf-8")
            parse_vdf(text)
            if '"publishedfileid"\t"0"' in text:
                text = text.replace('"publishedfileid"\t"0"', f'"publishedfileid"\t"{new_id}"')
                new_id += 1
            vdf.write_text(text, encoding="utf-8")
        return self.steamcmd_exit

    def which(self, program: str) -> str | None:
        """Every program is "installed".

        Args:
            program: Program name.

        Returns:
            The name itself.
        """
        return program

    def mutating(self) -> list[tuple[str, ...]]:
        """Commands that would change something outside this machine.

        Returns:
            gh create/edit calls (steamcmd runs are in :attr:`interactive`).
        """
        return [c for c in self.calls if c[0] == "gh" and len(c) > 2 and c[2] in {"create", "edit"}]  # noqa: PLR2004


@dataclass
class FakePrompter:
    """Answers questions from a table and records them."""

    words: dict[str, bool] = field(default_factory=dict)
    answer: str = ""
    asked: list[str] = field(default_factory=list)

    def confirm(self, question: str, word: str) -> bool:
        """Answer a typed confirmation.

        Args:
            question: Question.
            word: Expected word.

        Returns:
            The scripted answer for that word (default no).
        """
        self.asked.append(question)
        return self.words.get(word, False)

    def ask(self, question: str) -> str:
        """Answer a free-text question.

        Args:
            question: Question.

        Returns:
            The scripted answer.
        """
        self.asked.append(question)
        return self.answer


@dataclass
class FakeTransport:
    """VirusTotal API double."""

    known: dict[str, dict[str, int]] = field(default_factory=dict)
    requests: list[tuple[str, str, dict[str, str]]] = field(default_factory=list)

    def request(
        self, method: str, url: str, headers: Mapping[str, str], body: bytes | None = None
    ) -> HttpResponse:
        """Answer one request.

        Args:
            method: HTTP method.
            url: URL.
            headers: Headers.
            body: Body (unused).

        Returns:
            Known files: 200 with stats; unknown: 404; uploads: an analysis; analyses: completed.
        """
        del body
        self.requests.append((method, url, dict(headers)))
        if method == "POST":
            return HttpResponse(
                200, json.dumps({"data": {"type": "analysis", "id": "an-1"}}).encode()
            )
        if "/analyses/" in url:
            payload = {
                "data": {
                    "attributes": {
                        "status": "completed",
                        "stats": {"malicious": 0, "undetected": 70},
                    }
                }
            }
            return HttpResponse(200, json.dumps(payload).encode())
        sha = url.rsplit("/", 1)[-1]
        if sha in self.known:
            return HttpResponse(
                200,
                json.dumps(
                    {"data": {"attributes": {"last_analysis_stats": self.known[sha]}}}
                ).encode(),
            )
        return HttpResponse(404, b'{"error": {"code": "NotFoundError"}}')


@dataclass
class Harness:
    """The synthetic repository with its fakes, and a way to run the CLI on it."""

    root: Path
    config: Path
    key_file: Path
    runner: FakeRunner
    prompter: FakePrompter = field(default_factory=FakePrompter)
    transport: FakeTransport = field(default_factory=FakeTransport)
    env: dict[str, str] = field(default_factory=lambda: {"NO_COLOR": "1"})
    output: str = ""

    def run(self, *args: str) -> int:
        """Run ``release.py`` with the fakes.

        Args:
            *args: Command line after ``release.py``.

        Returns:
            Exit code; the console text is in :attr:`output`.
        """
        command, *rest = args
        stream = io.StringIO()
        code = main(
            [command, "--config", str(self.config), *rest],
            runner=self.runner,
            prompter=self.prompter,
            transport=self.transport,
            env=self.env,
            stream=stream,
            sleep=lambda _: None,
        )
        self.output = stream.getvalue()
        return code

    @property
    def out(self) -> Path:
        """Output folder of the release.

        Returns:
            ``dist/release/1.4.0`` below the root.
        """
        return self.root / "dist" / "release" / "1.4.0"
