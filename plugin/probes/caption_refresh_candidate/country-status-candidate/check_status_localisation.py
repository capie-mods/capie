"""Proves the CAPIE status localisation contract (R5-MINOR-2) offline, without the game.

1. Applies monorepo archive: docs/evidence/.../gen-py-capie-status.patch to a temporary copy of tools/gen.py (from HEAD)
   and reads the four localisation entries with ast (no generator run, no game files).
2. Models the engine: every key lookup is first offered to the country DLL (the hooks sit on the
   lookup functions), then the yml value is used and $KEY$ references are expanded recursively.
3. Asserts the visible RN_COUNTRY_PLUGIN text for english/polish/french with: the candidate DLL,
   no DLL, and the accepted (old) DLL; and reproduces the reviewer's bug for the rejected variant.
Usage: python -I check_status_localisation.py
"""
import ast
import pathlib
import re
import subprocess
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
REPO = HERE.parents[3]
PATCH = REPO / 'docs' / 'evidence' / 'session-20261011' / 'claude-native' / 'gen-py-capie-status.patch'
KEYS = ('RN_PLUGIN_STATUS', 'RN_PLUGIN_STATUS_EN', 'RN_PLUGIN_STATUS_PL', 'RN_COUNTRY_PLUGIN')
LOADED_EN, LOADED_PL = 'CAPIE 1.0 loaded', 'CAPIE 1.0 załadowane'
MISSING_EN, MISSING_PL = 'CAPIE missing: Rename Anything needs it', 'Brak CAPIE: wymaga go Rename Anything'
checks = failures = 0


def check(cond, label):
    global checks, failures
    checks += 1
    if not cond:
        failures += 1
        print('FAIL', label)


def entries(gen_source):
    found = {}
    for node in ast.walk(ast.parse(gen_source)):
        if isinstance(node, ast.Dict):
            for k, v in zip(node.keys, node.values):
                if isinstance(k, ast.Constant) and k.value in KEYS:
                    found[k.value] = ast.literal_eval(v)
    return found


# DLL behaviour exactly as country_status.patch / country.cpp implement it.
def dll_new(key):
    return {'RN_PLUGIN_STATUS': LOADED_EN, 'RN_PLUGIN_STATUS_EN': LOADED_EN, 'RN_PLUGIN_STATUS_PL': LOADED_PL}.get(key)


def dll_old(key):
    return 'DLL: ACTIVE (RN1)' if key == 'RN_PLUGIN_STATUS' else None


def dll_none(key):
    return None


def resolve(loc, lang, key, dll, depth=0):
    assert depth < 8
    override = dll(key)
    if override is not None:
        return override
    text = loc[key][1 if lang == 'polish' else 0]
    return re.sub(r'\$([A-Z0-9_]+)\$', lambda m: resolve(loc, lang, m.group(1), dll, depth + 1), text)


def main():
    base = subprocess.run(['git', '-C', str(REPO), 'show', 'HEAD:tools/gen.py'], check=True, capture_output=True).stdout
    with tempfile.TemporaryDirectory() as tmp:
        root = pathlib.Path(tmp)
        (root / 'tools').mkdir()
        (root / 'tools' / 'gen.py').write_bytes(base)
        applied = subprocess.run(['git', 'apply', '--unsafe-paths', '--directory=.', str(PATCH)], cwd=root,
                                 capture_output=True, text=True)
        check(applied.returncode == 0, f'patch applies to HEAD gen.py: {applied.stderr.strip()}')
        loc = entries((root / 'tools' / 'gen.py').read_text(encoding='utf-8'))
    check(set(loc) == set(KEYS), 'all four entries present after the patch')

    for lang, loaded, missing in (('english', LOADED_EN, MISSING_EN), ('polish', LOADED_PL, MISSING_PL),
                                  ('french', LOADED_EN, MISSING_EN)):
        check(resolve(loc, lang, 'RN_COUNTRY_PLUGIN', dll_new) == loaded, f'{lang}: candidate DLL shows {loaded!r}')
        check(resolve(loc, lang, 'RN_COUNTRY_PLUGIN', dll_none) == missing, f'{lang}: no DLL shows the fallback')
    # Documented coupling: the accepted (old) DLL only overrides the bare key, so with this yml it
    # would show the "missing" fallback -> the patch must ship together with the candidate DLL.
    check(resolve(loc, 'english', 'RN_COUNTRY_PLUGIN', dll_old) == MISSING_EN, 'old DLL + new yml: fallback (ship together)')

    # Reviewer's R5-MINOR-2 bug, reproduced with the REJECTED variant (redefine the bare key per
    # language, RN_COUNTRY_PLUGIN = $RN_PLUGIN_STATUS$): the DLL intercepts the bare key first, so a
    # Polish player would see the English text.
    rejected = {
        'RN_PLUGIN_STATUS': ('$RN_PLUGIN_STATUS_EN$', '$RN_PLUGIN_STATUS_PL$'),
        'RN_PLUGIN_STATUS_EN': (MISSING_EN, MISSING_EN),
        'RN_PLUGIN_STATUS_PL': (MISSING_PL, MISSING_PL),
        'RN_COUNTRY_PLUGIN': ('$RN_PLUGIN_STATUS$', '$RN_PLUGIN_STATUS$'),
    }
    check(resolve(rejected, 'polish', 'RN_COUNTRY_PLUGIN', dll_new) == LOADED_EN, 'rejected variant: PL sees English (bug reproduced)')
    print(f'status localisation check: {checks} checks, {failures} failures')
    return 1 if failures else 0


if __name__ == '__main__':
    raise SystemExit(main())
