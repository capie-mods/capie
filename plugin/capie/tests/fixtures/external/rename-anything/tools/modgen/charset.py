r"""The character code table of the name buffers.

HOI4 script has no string type, so a typed name is an array of character codes.
Code 0 is an empty slot. Characters that would break HOI4 script or
localisation (``" [ ] | $ § £ \\ { } #`` and anything non-printable) are left
out on purpose. The code values are part of the save format and of the native
bridge contract: never renumber them.
"""

from __future__ import annotations

LOWER = "abcdefghijklmnopqrstuvwxyz"
"""Latin lower-case letters, codes 1 to 26."""

UPPER = LOWER.upper()
"""Latin upper-case letters, codes 27 to 52."""

DIGITS = "0123456789"
"""Digits, codes 53 to 62."""

PUNCTUATION = " -.'"
"""Space, hyphen, full stop and apostrophe, codes 63 to 66."""

PL_LOWER = "ąćęłńóśźż"
"""Polish lower-case letters, codes 67 to 75."""

PL_UPPER = "ĄĆĘŁŃÓŚŹŻ"
"""Polish upper-case letters, codes 76 to 84."""

LOWER_FIRST = 1
"""Code of ``a``."""

UPPER_FIRST = LOWER_FIRST + len(LOWER)
"""Code of ``A`` (27)."""

DIGIT_FIRST = UPPER_FIRST + len(UPPER)
"""Code of ``0`` (53)."""

PUNCTUATION_FIRST = DIGIT_FIRST + len(DIGITS)
"""Code of the space (63)."""

PL_LOWER_FIRST = PUNCTUATION_FIRST + len(PUNCTUATION)
"""Code of ``ą`` (67)."""

PL_UPPER_FIRST = PL_LOWER_FIRST + len(PL_LOWER)
"""Code of ``Ą`` (76)."""

LAST_CODE = PL_UPPER_FIRST + len(PL_UPPER) - 1
"""Highest character code (84)."""

FORBIDDEN = frozenset('"[]|$§£\\{}#')
"""Characters that must never appear in the table because they break script or loc."""


def _build_table() -> dict[int, str]:
    """Build the code to character table from the ranges above.

    Returns:
        Character code to character, codes 1 to :data:`LAST_CODE`.

    Raises:
        ValueError: A forbidden character slipped into one of the ranges.
    """
    table: dict[int, str] = {}
    for first, characters in (
        (LOWER_FIRST, LOWER),
        (UPPER_FIRST, UPPER),
        (DIGIT_FIRST, DIGITS),
        (PUNCTUATION_FIRST, PUNCTUATION),
        (PL_LOWER_FIRST, PL_LOWER),
        (PL_UPPER_FIRST, PL_UPPER),
    ):
        for offset, character in enumerate(characters):
            table[first + offset] = character
    if FORBIDDEN & set(table.values()):
        msg = "the character table holds a character that breaks script or localisation"
        raise ValueError(msg)
    return table


CHARS: dict[int, str] = _build_table()
"""Character code to character; code 0 means an empty slot and has no entry."""

CODE: dict[str, int] = {v: k for k, v in CHARS.items()}
"""Character to character code; the inverse of :data:`CHARS`."""

DISPLAY_ORDER = (
    list(range(LOWER_FIRST, UPPER_FIRST))
    + list(range(UPPER_FIRST, DIGIT_FIRST))
    + list(range(PUNCTUATION_FIRST, PL_LOWER_FIRST))
    + list(range(DIGIT_FIRST, PUNCTUATION_FIRST))
    + list(range(PL_LOWER_FIRST, LAST_CODE + 1))
)
"""Order of the per-code branches in a buffer slot table (letters, punctuation, digits, Polish).

Only the order of ``defined_text`` branches depends on it; it is kept as it was
so the generated file stays byte-identical.
"""


def char_key(code: int) -> str:
    """Return the localisation key that prints one character code.

    Args:
        code: Character code (0 for an empty slot).

    Returns:
        ``RN_CH_<code>``.
    """
    return f"RN_CH_{code}"
