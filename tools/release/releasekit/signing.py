"""Signature check, reviewed hashes and the installer's pinned release block.

* :func:`verify_signature` checks ``release-manifest.json.sig`` the way ``install.ps1``
  does (RSA PKCS#1 v1.5 with SHA-256 over the exact manifest bytes), from the public
  ``Modulus``/``Exponent`` of a .NET ``RSAKeyValue`` XML. Only those two public values
  are ever read, also when they come from the private key file.
* :func:`reviewed_hashes` reads ``plugin/capie/deploy/bundle-inputs.json``, the one list of
  reviewed CAPIE binaries (name, source, SHA-256, DLL flag) that ``build_launcher.ps1``,
  ``build_proxy.ps1`` and this tool all read; :func:`reviewed_folder` derives the one folder
  of the reviewed build from its ``source`` entries.
* :func:`render_pinned_block` and :func:`replace_pinned_block` produce the
  ``$script:RaPinnedRelease`` block of ``tools/bootstrap/src/Shared/Configuration.ps1``.
"""

from __future__ import annotations

import base64
import hashlib
import json
import re
from dataclasses import dataclass

SHA256_DIGEST_INFO = bytes.fromhex("3031300d060960864801650304020105000420")
"""DER prefix of a SHA-256 ``DigestInfo`` (RFC 8017, section 9.2, note 1)."""

PKCS1_MIN_PADDING = 8
"""Smallest number of 0xFF padding bytes in an EMSA-PKCS1-v1_5 block."""

MIN_KEY_BITS = 2048
"""Smallest key the installer accepts."""

_MODULUS = re.compile(r"<Modulus>\s*([A-Za-z0-9+/=\s]+?)\s*</Modulus>")
"""Public modulus element of an ``RSAKeyValue``."""

_EXPONENT = re.compile(r"<Exponent>\s*([A-Za-z0-9+/=\s]+?)\s*</Exponent>")
"""Public exponent element of an ``RSAKeyValue``."""

_LOWER_SHA256 = re.compile(r"^[0-9a-f]{64}$")
"""A ``sha256`` value of ``bundle-inputs.json`` (lower-case, as the native builds compile it)."""

_INPUT_NAME = re.compile(r"^[A-Za-z0-9_.-]+$")
"""A ``name`` value of ``bundle-inputs.json`` (a plain file name)."""

_INPUT_KEYS = frozenset({"name", "source", "sha256", "dll"})
"""The keys every ``bundle-inputs.json`` entry has, and no other."""

_PINNED_BLOCK = re.compile(
    r"^\$script:RaPinnedRelease = \[ordered\]@\{.*?^\}[ \t]*$", re.MULTILINE | re.DOTALL
)
"""The pinned release block of the installer configuration."""

_PINNED_FIELD = re.compile(r"^\s*(Version|ZipName|ZipSha256)\s*=\s*'([^']*)'", re.MULTILINE)
"""A scalar field inside the pinned release block."""


class SigningError(ValueError):
    """A key, signature or pinned block is unusable."""


@dataclass(frozen=True)
class PublicKey:
    """RSA public key."""

    modulus: int
    exponent: int

    @property
    def bits(self) -> int:
        """Key size in bits.

        Returns:
            Key size in bits.
        """
        return self.modulus.bit_length()


def _b64_int(text: str) -> int:
    """Decode a big-endian Base64 integer.

    Args:
        text: Base64 text (blanks ignored).

    Returns:
        The integer.
    """
    return int.from_bytes(base64.b64decode("".join(text.split())), "big")


def public_key_from_xml(xml: str) -> PublicKey:
    """Read the public part of a .NET ``RSAKeyValue`` XML (public or private).

    Args:
        xml: Key XML.

    Returns:
        The public key.

    Raises:
        SigningError: No modulus or exponent.
    """
    modulus = _MODULUS.search(xml)
    exponent = _EXPONENT.search(xml)
    if modulus is None or exponent is None:
        msg = "not an RSAKeyValue XML (no Modulus/Exponent)"
        raise SigningError(msg)
    return PublicKey(_b64_int(modulus.group(1)), _b64_int(exponent.group(1)))


def pkcs1_sha256_block(data: bytes, size: int) -> bytes:
    """Build the EMSA-PKCS1-v1_5 encoding of SHA-256(data).

    Args:
        data: Signed bytes.
        size: Key size in bytes.

    Returns:
        The encoded block (``00 01 FF..FF 00 DigestInfo``).

    Raises:
        SigningError: The key is too small for the encoding.
    """
    digest = SHA256_DIGEST_INFO + hashlib.sha256(data).digest()
    padding = size - len(digest) - 3
    if padding < PKCS1_MIN_PADDING:
        msg = "key too small for a SHA-256 signature"
        raise SigningError(msg)
    return b"\x00\x01" + b"\xff" * padding + b"\x00" + digest


def verify_signature(key: PublicKey, data: bytes, signature_b64: str) -> bool:
    """Verify an RSA PKCS#1 v1.5 SHA-256 signature.

    Args:
        key: Public key.
        data: Signed bytes (the manifest file).
        signature_b64: Base64 signature (the ``.sig`` file).

    Returns:
        True when the signature is valid for these bytes.
    """
    size = (key.bits + 7) // 8
    try:
        signature = int.from_bytes(base64.b64decode(signature_b64.strip(), validate=True), "big")
        expected = pkcs1_sha256_block(data, size)
    except (ValueError, SigningError):
        return False
    if signature >= key.modulus:
        return False
    return pow(signature, key.exponent, key.modulus).to_bytes(size, "big") == expected


@dataclass(frozen=True)
class BundleInput:
    """One reviewed CAPIE binary of ``bundle-inputs.json``."""

    name: str
    source: str
    sha256: str
    dll: bool


def bundle_inputs(text: str) -> tuple[BundleInput, ...]:
    """Parse and validate ``bundle-inputs.json``.

    Args:
        text: Text of the file (a JSON array of ``{"name", "source", "sha256", "dll"}``).

    Returns:
        The entries in file order.

    Raises:
        SigningError: Not valid JSON, a wrong shape, a non-lower-case hash or a duplicate name.
    """
    try:
        data = json.loads(text.removeprefix("﻿"))
    except json.JSONDecodeError as error:
        msg = f"bundle-inputs.json is not valid JSON: {error}"
        raise SigningError(msg) from error
    if not isinstance(data, list) or not data:
        msg = "bundle-inputs.json must be a non-empty JSON array"
        raise SigningError(msg)
    entries: list[BundleInput] = []
    seen: set[str] = set()
    for index, item in enumerate(data):
        if not isinstance(item, dict) or set(item) != _INPUT_KEYS:
            msg = f"bundle-inputs.json entry {index} must have exactly {sorted(_INPUT_KEYS)}"
            raise SigningError(msg)
        name, source, digest, dll = item["name"], item["source"], item["sha256"], item["dll"]
        if not isinstance(name, str) or not _INPUT_NAME.match(name):
            msg = f"bundle-inputs.json entry {index}: bad name {name!r}"
            raise SigningError(msg)
        if not isinstance(source, str) or not source:
            msg = f"bundle-inputs.json entry {name}: bad source {source!r}"
            raise SigningError(msg)
        if not isinstance(digest, str) or not _LOWER_SHA256.match(digest):
            msg = f"bundle-inputs.json entry {name}: sha256 must be 64 lower-case hex digits"
            raise SigningError(msg)
        if not isinstance(dll, bool):
            msg = f"bundle-inputs.json entry {name}: dll must be true or false"
            raise SigningError(msg)
        if name.lower() in seen:
            msg = f"bundle-inputs.json lists {name} twice"
            raise SigningError(msg)
        seen.add(name.lower())
        entries.append(BundleInput(name, source, digest, dll))
    return tuple(entries)


def reviewed_folder(inputs: tuple[BundleInput, ...]) -> str:
    """The one folder that holds the reviewed binaries.

    Args:
        inputs: Entries of ``bundle-inputs.json``.

    Returns:
        The repository-relative folder all ``source`` values share (forward slashes).

    Raises:
        SigningError: The sources name more than one folder.
    """
    folders = sorted({entry.source.rpartition("/")[0] for entry in inputs})
    if len(folders) != 1:
        msg = f"bundle-inputs.json sources must share one folder, found {folders}"
        raise SigningError(msg)
    return folders[0]


def reviewed_hashes(text: str) -> dict[str, str]:
    """Reviewed hashes by file name, from ``bundle-inputs.json``.

    Args:
        text: Text of ``bundle-inputs.json``.

    Returns:
        Upper-case hashes by file name.
    """
    return {entry.name: entry.sha256.upper() for entry in bundle_inputs(text)}


@dataclass(frozen=True)
class PinnedRelease:
    """Values of the installer's pinned release."""

    version: str
    zip_name: str
    zip_sha256: str
    files: tuple[tuple[str, str], ...]
    game_sha256: tuple[str, ...]


def render_pinned_block(pinned: PinnedRelease) -> str:
    """Render the ``$script:RaPinnedRelease`` block.

    Args:
        pinned: Release values.

    Returns:
        PowerShell source of the block (ASCII, LF, no trailing newline).
    """
    width = max(len(f"'{name}'") for name, _ in pinned.files)
    files = "\n".join(
        f"        {f"'{name}'".ljust(width)} = '{digest}'" for name, digest in pinned.files
    )
    games = ", ".join(f"'{h}'" for h in pinned.game_sha256)
    return "\n".join(
        [
            "$script:RaPinnedRelease = [ordered]@{",
            f"    Version             = '{pinned.version}'",
            f"    ZipName             = '{pinned.zip_name}'",
            f"    ZipSha256           = '{pinned.zip_sha256}'",
            "    Files               = [ordered]@{",
            files,
            "    }",
            (
                "    # hoi4.exe builds this release's DLLs support "
                "(install notice, -Update, -CheckUpdate; the launcher enforces it)."
            ),
            f"    SupportedGameSha256 = @({games})",
            "}",
        ]
    )


def pinned_fields(configuration: str) -> dict[str, str]:
    """Read Version, ZipName and ZipSha256 of the pinned block.

    Args:
        configuration: Text of ``Configuration.ps1``.

    Returns:
        The fields found (empty when there is no block).
    """
    block = _PINNED_BLOCK.search(configuration)
    return dict(_PINNED_FIELD.findall(block.group(0))) if block else {}


def replace_pinned_block(configuration: str, block: str) -> str:
    """Replace the pinned block, keeping the rest of the file.

    Args:
        configuration: Text of ``Configuration.ps1``.
        block: New block from :func:`render_pinned_block`.

    Returns:
        The new text.

    Raises:
        SigningError: The file has no pinned block.
    """
    if _PINNED_BLOCK.search(configuration) is None:
        msg = "Configuration.ps1 has no $script:RaPinnedRelease block"
        raise SigningError(msg)
    return _PINNED_BLOCK.sub(lambda _: block, configuration, count=1)
