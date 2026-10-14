"""Signature check, reviewed hashes and the installer's pinned release block.

* :func:`verify_signature` checks ``release-manifest.json.sig`` the way ``install.ps1``
  does (RSA PKCS#1 v1.5 with SHA-256 over the exact manifest bytes), from the public
  ``Modulus``/``Exponent`` of a .NET ``RSAKeyValue`` XML. Only those two public values
  are ever read, also when they come from the private key file.
* :func:`reviewed_hashes` reads the SHA-256 table of ``DEPLOYMENT.md``: the binaries
  that the launcher and proxy pin and that passed review.
* :func:`render_pinned_block` and :func:`replace_pinned_block` produce the
  ``$script:RaPinnedRelease`` block of ``tools/bootstrap/src/Shared/Configuration.ps1``.
"""

from __future__ import annotations

import base64
import hashlib
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

_TABLE_ROW = re.compile(r"^\|\s*([A-Za-z0-9_.-]+)\s*\|\s*([0-9A-Fa-f]{64})\s*\|", re.MULTILINE)
"""``| file | SHA-256 |`` row of a Markdown table."""

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


def reviewed_hashes(markdown: str) -> dict[str, str]:
    """Read ``| file | SHA-256 |`` rows (the first table row per file wins).

    Args:
        markdown: Text of ``DEPLOYMENT.md``.

    Returns:
        Upper-case hashes by file name.
    """
    found: dict[str, str] = {}
    for name, digest in _TABLE_ROW.findall(markdown):
        found.setdefault(name, digest.upper())
    return found


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
                "(for -CheckUpdate messages; the launcher enforces it)."
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
