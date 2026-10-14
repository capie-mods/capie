"""The ``workshopitem`` VDF that SteamCMD reads, and a check that it will parse.

SteamCMD reads the build config with Valve's KeyValues parser **without escape
sequences**: a quoted string ends at the next ``"``, and a backslash is an ordinary
character. A backslash before a quote therefore does not put a quote into the text; it
ends the string early, and the rest of the value is read as more keys and values. That
broke the first real upload (``KeyValues Error: RecursiveLoadFromBuffer: got } in key ...
Failed to parse build config file``) and cut the first "CAPIE" item description short.

So :func:`vdf_value` makes every value safe for that parser instead of escaping it:

* ``"`` becomes ``'``. A straight single quote keeps the design system's rule of straight
  quotes in English text and is plain ASCII; typographic quotes would break that rule.
* A backslash becomes ``/``. Windows accepts ``/`` in the content and preview paths, and a
  slash reads the same in a description, so nothing depends on whether a parser treats
  the backslash as an escape.

:func:`parse_vdf` is a tokenizer that follows the same no-escape rules, used to check
every VDF before SteamCMD sees it (and by the tests).
"""

from __future__ import annotations

import re

WORKSHOP_KEYS = frozenset(
    {
        "appid",
        "publishedfileid",
        "contentfolder",
        "previewfile",
        "visibility",
        "title",
        "description",
        "changenote",
    }
)
"""Keys of a ``workshopitem`` build config (Steamworks, "SteamCMD Integration")."""

ROOT_KEY = "workshopitem"
"""Name of the root block."""

_PUBLISHED_ID = re.compile(r'"publishedfileid"\s+"(\d+)"')
"""``publishedfileid`` line of a VDF."""

_BRACES = frozenset("{}")
"""Single-character tokens."""


class VdfError(ValueError):
    """The text would not parse as a ``workshopitem`` VDF without escapes."""


def vdf_value(value: str) -> str:
    """Make a value safe for KeyValues without escape sequences.

    Args:
        value: Text.

    Returns:
        The text with ``"`` replaced by ``'`` and every backslash by ``/``.
    """
    return value.replace('"', "'").replace("\\", "/")


def render_vdf(fields: list[tuple[str, str]]) -> str:
    """Render a ``workshopitem`` VDF.

    Args:
        fields: Keys and values in output order.

    Returns:
        VDF text whose values contain no ``"`` and no backslash.
    """
    body = "".join(f'\t"{vdf_value(k)}"\t"{vdf_value(v)}"\n' for k, v in fields)
    return f'"{ROOT_KEY}"\n{{\n{body}}}\n'


def tokenize(text: str) -> list[tuple[str, bool, int]]:
    """Split VDF text into tokens the way KeyValues does with escapes off.

    Args:
        text: VDF text.

    Returns:
        ``(token, quoted, offset)`` triples.

    Raises:
        VdfError: A quoted string is not closed.
    """
    tokens: list[tuple[str, bool, int]] = []
    i = 0
    while i < len(text):
        ch = text[i]
        if ch.isspace():
            i += 1
        elif text.startswith("//", i):
            end = text.find("\n", i)
            i = len(text) if end < 0 else end + 1
        elif ch == '"':
            end = text.find('"', i + 1)
            if end < 0:
                msg = f"unterminated quoted string at offset {i}"
                raise VdfError(msg)
            tokens.append((text[i + 1 : end], True, i))
            i = end + 1
        elif ch in _BRACES:
            tokens.append((ch, False, i))
            i += 1
        else:
            start = i
            while i < len(text) and not text[i].isspace() and text[i] not in '"{}':
                i += 1
            tokens.append((text[start:i], False, start))
    return tokens


def parse_vdf(text: str) -> dict[str, str]:
    """Parse a ``workshopitem`` VDF strictly, without escape sequences.

    Args:
        text: VDF text.

    Returns:
        Values by key.

    Raises:
        VdfError: Wrong structure, an unknown or repeated key, or text after the block.
    """
    tokens = tokenize(text)
    if len(tokens) < 3 or tokens[0][:2] != (ROOT_KEY, True) or tokens[1][:2] != ("{", False):  # noqa: PLR2004 - name, {, }
        msg = f'expected "{ROOT_KEY}" {{ at the start'
        raise VdfError(msg)
    values: dict[str, str] = {}
    i = 2
    while True:
        if i >= len(tokens):
            msg = "missing closing }"
            raise VdfError(msg)
        key, quoted, offset = tokens[i]
        if (key, quoted) == ("}", False):
            break
        if not quoted or key not in WORKSHOP_KEYS or key in values:
            msg = f"unexpected key {key[:40]!r} at offset {offset}"
            raise VdfError(msg)
        if i + 1 >= len(tokens) or not tokens[i + 1][1]:
            msg = f"key {key!r} at offset {offset} has no quoted value"
            raise VdfError(msg)
        values[key] = tokens[i + 1][0]
        i += 2
    if i + 1 != len(tokens):
        msg = f"text after the closing }} at offset {tokens[i + 1][2]}"
        raise VdfError(msg)
    return values


def vdf_published_id(text: str) -> int:
    """Read ``publishedfileid`` from a VDF.

    Args:
        text: VDF text.

    Returns:
        The ID (0 when missing).
    """
    match = _PUBLISHED_ID.search(text)
    return int(match.group(1)) if match else 0
