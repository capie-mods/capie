"""Release notes from ``CHANGELOG.md`` (Keep a Changelog layout).

A version's section starts at ``## [x.y.z]`` (optionally followed by `` - date``) and
ends at the next ``## `` heading. The GitHub release uses the section as Markdown; the
Workshop change note gets it converted to Steam BBCode by :func:`to_bbcode`.
"""

from __future__ import annotations

import re

STEAM_CHANGENOTE_LIMIT = 8000
"""Longest change note the tool sends to Steam (characters); longer notes are refused."""

DRAFT_MARKER = re.compile(r"\bDRAFT\b")
"""Marks notes that are not reviewed yet; such text is never sent to Steam or GitHub."""

_HEADING = re.compile(r"^## ", re.MULTILINE)
"""Any second-level heading."""

_BULLET = re.compile(r"^\s*[-*]\s+(.*)$")
"""A Markdown list item."""

_SUBHEADING = re.compile(r"^#{3,6}\s+(.*)$")
"""A third- to sixth-level heading."""

_BOLD = re.compile(r"\*\*(.+?)\*\*")
"""Markdown bold."""

_CODE = re.compile(r"`([^`]+)`")
"""Markdown inline code."""

_LINK = re.compile(r"\[([^\]]+)\]\(([^)]+)\)")
"""Markdown link."""


def section(text: str, version: str) -> str | None:
    """Extract the notes of one version.

    Args:
        text: Changelog text.
        version: Version (``x.y.z``).

    Returns:
        The section body without its heading and surrounding blank lines, or ``None``
        when the version has no section or the section is empty.
    """
    heading = re.compile(rf"^## \[{re.escape(version)}\][^\n]*$", re.MULTILINE)
    match = heading.search(text)
    if match is None:
        return None
    following = _HEADING.search(text, match.end())
    body = text[match.end() : following.start() if following else len(text)].strip()
    return body or None


def _inline(text: str) -> str:
    """Convert inline Markdown to BBCode.

    Args:
        text: One line.

    Returns:
        The converted line.
    """
    text = _LINK.sub(r"[url=\2]\1[/url]", text)
    text = _BOLD.sub(r"[b]\1[/b]", text)
    return _CODE.sub(r"\1", text)


def to_bbcode(markdown: str) -> str:
    """Convert a changelog section to Steam BBCode.

    Headings become bold lines, runs of list items become ``[list]`` blocks (indented
    continuation lines join their item), links, bold and inline code are converted;
    other lines are kept.

    Args:
        markdown: Section body.

    Returns:
        BBCode text.
    """
    out: list[str] = []
    in_list = False
    for raw in markdown.splitlines():
        bullet = _BULLET.match(raw)
        if bullet:
            if not in_list:
                out.append("[list]")
                in_list = True
            out.append("[*]" + _inline(bullet.group(1).strip()))
            continue
        if in_list and raw[:1].isspace() and raw.strip():
            out[-1] += " " + _inline(raw.strip())
            continue
        if in_list:
            out.append("[/list]")
            in_list = False
        sub = _SUBHEADING.match(raw)
        out.append(f"[b]{_inline(sub.group(1).strip())}[/b]" if sub else _inline(raw))
    if in_list:
        out.append("[/list]")
    return "\n".join(out).strip()


def is_draft(text: str) -> bool:
    """Tell whether text still carries the ``DRAFT`` marker.

    Args:
        text: Changelog section or description.

    Returns:
        True when the word ``DRAFT`` (upper case) appears.
    """
    return DRAFT_MARKER.search(text) is not None
