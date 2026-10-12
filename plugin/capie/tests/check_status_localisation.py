"""Prove the CAPIE status-text contract on the generated mod localisation (R5-MINOR-2).

Reads ``RN_PLUGIN_STATUS``, ``_EN``, ``_PL`` and ``RN_COUNTRY_PLUGIN`` from the generated
``mod/localisation/<language>/rename_anything_l_<language>.yml`` files and models the
engine: each key is first offered to the country DLL (its hooks sit on the lookup
functions), then the yml value is used and ``$KEY$`` references are expanded. Asserts the
visible ``RN_COUNTRY_PLUGIN`` text for english, polish and french with ``capie_core.dll``,
without a DLL and with the accepted (old) country DLL.

Usage::

    python -I check_status_localisation.py
"""

import pathlib
import re
import sys
from collections.abc import Callable

REPO = pathlib.Path(__file__).resolve().parents[3]
"""Repository root."""

KEYS = ("RN_PLUGIN_STATUS", "RN_PLUGIN_STATUS_EN", "RN_PLUGIN_STATUS_PL", "RN_COUNTRY_PLUGIN")
"""Localisation keys involved in the status line."""

LOADED_EN = "CAPIE 1.1 loaded"
"""English text served by capie_core.dll."""

LOADED_PL = "CAPIE 1.1 załadowane"
"""Polish text served by capie_core.dll."""

MISSING_EN = "CAPIE missing: Rename Anything needs it"
"""English fallback from the mod localisation."""

MISSING_PL = "Brak CAPIE: wymaga go Rename Anything"
"""Polish fallback from the mod localisation."""

MAX_DEPTH = 8
"""Recursion limit of ``$KEY$`` expansion."""


def entries(language: str) -> dict[str, str]:
    """Return the status entries of one generated localisation file.

    Args:
        language: HOI4 language folder name.

    Returns:
        Mapping of key to value.
    """
    path = REPO / "mod" / "localisation" / language / f"rename_anything_l_{language}.yml"
    found: dict[str, str] = {}
    for line in path.read_text(encoding="utf-8-sig").splitlines():
        match = re.match(r'\s*(\w+):\d*\s*"(.*)"\s*$', line)
        if match and match.group(1) in KEYS:
            found[match.group(1)] = match.group(2)
    return found


def dll_capie(key: str) -> str | None:
    """Model of capie_core.dll's status override.

    Args:
        key: Localisation key.

    Returns:
        Override text, or None to fall through.
    """
    return {
        "RN_PLUGIN_STATUS": LOADED_EN,
        "RN_PLUGIN_STATUS_EN": LOADED_EN,
        "RN_PLUGIN_STATUS_PL": LOADED_PL,
    }.get(key)


def dll_old(key: str) -> str | None:
    """Model of the accepted country DLL (bare key only).

    Args:
        key: Localisation key.

    Returns:
        Override text, or None.
    """
    return "DLL: ACTIVE (RN1)" if key == "RN_PLUGIN_STATUS" else None


def dll_none(key: str) -> str | None:
    """No DLL loaded.

    Args:
        key: Localisation key.

    Returns:
        Always None.
    """
    del key
    return None


def resolve(loc: dict[str, str], key: str, dll: Callable[[str], str | None], depth: int = 0) -> str:
    """Resolve a key the way the hooked engine does.

    Args:
        loc: Localisation entries.
        key: Key to resolve.
        dll: DLL model.
        depth: Recursion depth.

    Returns:
        Visible text.
    """
    if depth >= MAX_DEPTH:
        return "<loop>"
    override = dll(key)
    if override is not None:
        return override
    return re.sub(r"\$([A-Z0-9_]+)\$", lambda m: resolve(loc, m.group(1), dll, depth + 1), loc[key])


def main() -> int:
    """Run the checks.

    Returns:
        Process exit code.
    """
    checks = 0
    failures = 0
    for language, loaded, missing in (
        ("english", LOADED_EN, MISSING_EN),
        ("polish", LOADED_PL, MISSING_PL),
        ("french", LOADED_EN, MISSING_EN),
    ):
        loc = entries(language)
        cases = (
            (set(loc) == set(KEYS), f"{language}: four entries present"),
            (
                resolve(loc, "RN_COUNTRY_PLUGIN", dll_capie) == loaded,
                f"{language}: capie_core shows {loaded!r}",
            ),
            (
                resolve(loc, "RN_COUNTRY_PLUGIN", dll_none) == missing,
                f"{language}: no DLL shows the fallback",
            ),
            (
                resolve(loc, "RN_COUNTRY_PLUGIN", dll_old) == missing,
                f"{language}: old DLL shows the fallback",
            ),
        )
        for ok, label in cases:
            checks += 1
            if not ok:
                failures += 1
                print("FAIL", label)
    print(f"status localisation check: {checks} checks, {failures} failures")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
