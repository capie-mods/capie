r"""The CAPIE character code table, API level 2.

HOI4 script has no string type, so CAPIE sends each typed character as an integer
code and the SDK turns codes back into text with one localisation key per code.
This module is the single definition of that table. The native encoder in
``plugin/capie/src/features/editor/input_codes.hpp`` must hold the same list in the
same order; ``plugin/capie/tests/check_alphabet.py`` compares the two.

Codes 1 to 84 are Rename Anything's table and never change. API level 2 appends
eight punctuation marks, the Latin-1 Supplement letters and Latin Extended-A, in
code-point order, skipping the characters the first block already has. Characters
with meaning in HOI4 script or localisation (``" [ ] | $ § £ \\ { } # @``) and
control characters are never part of the table.
"""

from __future__ import annotations

LEGACY = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -.'ąćęłńóśźżĄĆĘŁŃÓŚŹŻ"
"""Rename Anything's 84 characters, codes 1 to 84 (API level 1)."""

PUNCTUATION = ",!?()&:/"
"""Punctuation added in API level 2, codes 85 to 92."""

LATIN1_FIRST = 0xC0
"""First Latin-1 Supplement letter (``À``)."""

LATIN1_LAST = 0xFF
"""Last Latin-1 Supplement letter (``ÿ``)."""

LATIN1_SYMBOLS = frozenset((chr(0xD7), chr(0xF7)))
"""Multiplication and division sign: the two Latin-1 symbols inside the letter range."""

EXTENDED_A_FIRST = 0x100
"""First Latin Extended-A character (``Ā``)."""

EXTENDED_A_LAST = 0x17F
"""Last Latin Extended-A character (U+017F, long s)."""

FORBIDDEN = frozenset('"[]|$§£\\{}#@')
"""Characters that break HOI4 script or localisation; never in the table."""

LEGACY_LAST_CODE = len(LEGACY)
"""Highest API level 1 code (84)."""

MAX_FIELD_LENGTH = 32
"""Most characters in one field."""


def _build_alphabet() -> str:
    """Build the API level 2 alphabet in code order.

    Returns:
        Every character of the table; the character at index ``i`` has code ``i + 1``.

    Raises:
        ValueError: The table holds a duplicate, a forbidden or a control character.
    """
    letters = [
        chr(point)
        for point in range(LATIN1_FIRST, EXTENDED_A_LAST + 1)
        if chr(point) not in LATIN1_SYMBOLS and chr(point) not in LEGACY
    ]
    alphabet = LEGACY + PUNCTUATION + "".join(letters)
    if len(set(alphabet)) != len(alphabet):
        msg = "the CAPIE alphabet holds a character twice"
        raise ValueError(msg)
    if FORBIDDEN & set(alphabet) or any(ord(c) < 0x20 or 0x7F <= ord(c) <= 0x9F for c in alphabet):  # noqa: PLR2004 - C0/C1 bounds
        msg = "the CAPIE alphabet holds a character that breaks script or localisation"
        raise ValueError(msg)
    return alphabet


ALPHABET = _build_alphabet()
"""The API level 2 alphabet: code ``i + 1`` is ``ALPHABET[i]``."""

LAST_CODE = len(ALPHABET)
"""Highest code (264)."""

CHARS: dict[int, str] = {index + 1: character for index, character in enumerate(ALPHABET)}
"""Code to character; code 0 is an empty slot and has no entry."""

CODE: dict[str, int] = {character: code for code, character in CHARS.items()}
"""Character to code."""

SPACE_CODE = CODE[" "]
"""Code of the space (63)."""


def encode(text: str) -> list[int]:
    """Encode a text with the table (reference model of the native encoder).

    Args:
        text: Text of at most :data:`MAX_FIELD_LENGTH` characters.

    Returns:
        One code per character.

    Raises:
        ValueError: The text is empty, too long or holds a character outside the table.
    """
    if not text or len(text) > MAX_FIELD_LENGTH:
        msg = "a field holds 1 to 32 characters"
        raise ValueError(msg)
    try:
        return [CODE[character] for character in text]
    except KeyError as error:
        msg = f"character outside the CAPIE alphabet: {error.args[0]!r}"
        raise ValueError(msg) from error


def decode(codes: list[int]) -> str:
    """Decode codes back to text.

    Args:
        codes: Codes 1 to :data:`LAST_CODE`.

    Returns:
        The text.
    """
    return "".join(CHARS[code] for code in codes)
