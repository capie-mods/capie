"""Prove that the native alphabet and the SDK character table are the same list (C3).

Reads the two ``std::u32string_view`` literals ``alphabet`` and ``extended_alphabet`` from
``plugin/capie/src/features/editor/input_codes.hpp`` and compares them with
``sdk/capie-script-sdk/tools/capie_charset.py`` (the single definition of the API level 2
table) and with Rename Anything's ``tools/modgen/charset.py`` (codes 1 to 84): same
characters, same order, same size, no markup character. Font coverage is documented in
``plugin/capie/CONTRACT.md`` and is not checked here.

Usage::

    python -I check_alphabet.py
"""

import importlib.util
import pathlib
import re
import sys
from types import ModuleType

REPO = pathlib.Path(__file__).resolve().parents[3]
"""Repository root."""

HEADER = REPO / "plugin" / "capie" / "src" / "features" / "editor" / "input_codes.hpp"
"""Native encoder header."""

SDK_CHARSET = REPO / "sdk" / "capie-script-sdk" / "tools" / "capie_charset.py"
"""SDK character table."""

MOD_CHARSET = REPO / "tools" / "modgen" / "charset.py"
"""Rename Anything's character table."""

LEGACY_SIZE = 84
"""Codes of API level 1."""

EXTENDED_SIZE = 264
"""Codes of API level 2."""


def load(path: pathlib.Path, name: str) -> ModuleType:
    """Import a module from a file path.

    Args:
        path: Python file.
        name: Module name to register.

    Returns:
        The loaded module.

    Raises:
        ImportError: The file cannot be loaded.
    """
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        msg = f"cannot load {path}"
        raise ImportError(msg)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


def literal(source: str, name: str) -> str:
    """Return the concatenated value of a ``U"..."`` literal assigned to ``name``.

    Args:
        source: Header text.
        name: Variable name.

    Returns:
        The characters of the literal.

    Raises:
        ValueError: The variable is not found.
    """
    match = re.search(rf"\b{name}\s*=\s*((?:\s*U\"[^\"]*\")+)\s*;", source)
    if match is None:
        msg = f"{name} not found in {HEADER.name}"
        raise ValueError(msg)
    return "".join(str(part) for part in re.findall(r'U"([^"]*)"', match.group(1)))


def main() -> int:
    """Run the checks.

    Returns:
        Process exit code.
    """
    source = HEADER.read_text(encoding="utf-8")
    native_legacy = literal(source, "alphabet")
    native_extended = literal(source, "extended_alphabet")
    sdk = load(SDK_CHARSET, "capie_charset")
    mod = load(MOD_CHARSET, "rn_charset")
    mod_legacy = "".join(mod.CHARS[code] for code in range(1, mod.LAST_CODE + 1))
    cases = (
        (len(native_legacy) == LEGACY_SIZE, "native legacy table has 84 codes"),
        (native_legacy == mod_legacy, "native legacy table equals tools/modgen/charset.py"),
        (native_legacy == sdk.LEGACY, "SDK legacy block equals the native legacy table"),
        (len(native_extended) == EXTENDED_SIZE, "native extended table has 264 codes"),
        (native_extended == sdk.ALPHABET, "native extended table equals the SDK table, in order"),
        (native_extended.startswith(native_legacy), "codes 1..84 unchanged in the extended table"),
        (not set(native_extended) & sdk.FORBIDDEN, "no markup character in the table"),
        (sdk.LAST_CODE == EXTENDED_SIZE, "SDK LAST_CODE is 264"),
    )
    failures = 0
    for ok, label in cases:
        if not ok:
            failures += 1
            print(f"FAIL {label}")
    print(f"alphabet check: {len(cases)} checks, {failures} failures")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
