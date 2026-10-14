# Building and testing

Everything here runs offline and never touches a running game, except the test session helper
at the end, which changes your HOI4 user settings (and backs them up first).

## Prerequisites

- Windows 10 or 11, PowerShell 7 or Windows PowerShell 5.1.
- [uv](https://docs.astral.sh/uv/) for Python. `uv sync` installs Python, the lint tools, pytest
  and the documentation tools (dependency groups `dev`, `observe` and `docs` in `pyproject.toml`).
- For the native code: Visual Studio 2022 with the C++ x64 tools and a Windows SDK, and a local
  checkout of [MinHook](https://github.com/TsudaKageyu/minhook). clang-tidy and clang-format come
  from the Visual Studio component "C++ Clang tools for Windows".
- Hearts of Iron IV 1.19.3.0 for the generator (it reads the installation, read only) and for the
  pin checks that compare signatures with `hoi4.exe` on disk.

## The mod

```powershell
uv run python tools/gen.py      # regenerate every file marked GENERATED in mod/
uv run python tools/check.py    # references, structure, localisation, state and city tables
```

For a game installed elsewhere:

```powershell
uv run python tools/gen.py --game "D:/Steam/steamapps/common/Hearts of Iron IV"
uv run python tools/check.py "D:/Steam/steamapps/common/Hearts of Iron IV"
```

Edit the generator (`tools/modgen/`, one module per feature) or the hand-written sources in
`tools/inline_editor/`, never a file marked `GENERATED`. `tools/gen.py` is only the command line;
helpers shared with `tools/check.py` live in `tools/rncommon/`.

`tools/tests/check_inline_editor.py` checks the in-place editor sources against their generated
copies in `mod/`, runs the script effects in a small virtual machine over hundreds of cases and
runs mutation tests on them.

These checks do not replace the game's own parser, a save and load test in the game, or a
multiplayer test.

## Python lint, types and unit tests

Configured in `pyproject.toml`: ruff with `select = ["ALL"]` plus the preview docstring rules, and
ty with every rule as an error.

```powershell
uv sync
uv run ruff check
uv run ruff format --check
uv run ty check
uv run python tools/check_constant_docstrings.py
uv run pytest
```

`tools/check_constant_docstrings.py` requires a docstring under every module-level `UPPER_CASE`
constant and `Args:` and `Returns:` sections in every function docstring, which ruff does not
check on its own. `uv run pytest` runs `tools/tests/` and `sdk/capie-script-sdk/tests/`.
Excluded paths (the archived `docs/evidence/`, pinned tools under `probes/`) are listed with their
reasons in `pyproject.toml`.

## PowerShell lint

PSScriptAnalyzer with `PSScriptAnalyzerSettings.psd1` and the project rules in
`tools/lint/RaScriptRules.psm1`, without installing anything into your profile:

```powershell
Save-Module PSScriptAnalyzer -Path $env:TEMP\psmodules
.\tools\lint-powershell.ps1 -ModulePath $env:TEMP\psmodules   # exit 0 means no findings
```

## CAPIE (native code)

```powershell
.\plugin\capie\build.ps1 -MinHookDir 'C:\path\to\minhook'            # both variants, all offline tests, benchmarks
.\plugin\capie\mutation_check.ps1                                     # mutants of the DLLs
.\plugin\capie\mutation_check.ps1 -MutantsFile tests\console\mutants_console.json
uv run pwsh -NoProfile -File plugin/capie/tools/run-clang-tidy.ps1    # clang-tidy and clang-format
```

Use `-OutputDirectory` to build a candidate next to the reviewed binaries instead of over them,
and `-Console` for a developer build with the console commands. All CAPIE C++ compiles with
`/MT /W4 /WX /std:c++20 /permissive-`. The build reads `hoi4.exe` as a file for the static pin
checks; it never starts or attaches to the game. Building over a DLL that a running game has
loaded is refused.

Launcher and proxy:

```powershell
pwsh -File .\plugin\capie\build_launcher.ps1             # build, unit and flow tests with fakes
pwsh -File .\plugin\capie\launcher_mutation_check.ps1
.\plugin\capie\build_proxy.ps1                           # after the reviewed DLLs are built
```

More detail, including the binary layout and the debug variant, is in
[plugin/capie/README.md](../../plugin/capie/README.md). The proxy's test limits are in
[proxy loader](proxy-loader.md).

## Script SDK

```powershell
uv run python sdk/capie-script-sdk/tools/gen_sdk.py           # write sdk/capie-script-sdk/mod/
uv run python sdk/capie-script-sdk/tools/gen_sdk.py --check   # exit 1 when mod/ is stale
```

## Installer

```powershell
.\tools\bootstrap\build-install.ps1 -Check                   # install.ps1 matches src/
$env:RA_BOOTSTRAP_TEST_ROOT = "$env:TEMP\ra-bootstrap-tests"
Invoke-Pester -Script .\tools\bootstrap\install.Tests.ps1
.\tools\bootstrap\mutation-test.ps1 -WorkRoot $env:TEMP\ra-mutation -PowerShellExe pwsh
```

See [tools/bootstrap/README.md](../../tools/bootstrap/README.md).

## Documentation sites

```powershell
uv run python sites/shared/sync_shared.py              # copy the shared theme into both sites
uv run python sites/shared/sync_shared.py --check      # exit 1 when a copy is stale
uv run python sites/shared/zensical_overrides.py       # Zensical assets without layout transitions
uv run zensical build -f sites/capie/zensical.toml --clean --strict
uv run zensical build -f sites/rename-anything/zensical.toml --clean --strict
```

`--strict` fails the build on any warning, such as a broken link. See
[sites/README.md](../../sites/README.md).

## Testing in the game

`tools/test-session.ps1` prepares a repeatable local test profile. The game must be closed.

```powershell
.\tools\test-session.ps1 -Action Begin -Language polish   # back up settings, 1720x900 window, dev mod only
.\tools\test-session.ps1 -Action Restore                  # put the originals back after checking SHA-256
```

It backs up `dlc_load.json`, `settings.txt` and, when present, `pdx_settings.txt` (newer game
builds rebuild the legacy settings from it, so language, window size and windowed mode are set
in both). Restore also handles older manifests with only two files. Try the helper with a
temporary `-UserDirectory` first, and never run it against your live profile while the game is
running. A repeatable test of both settings formats and the older manifest, using a fresh TEMP
folder only:

```powershell
powershell.exe -NoProfile -File tools/tests/test-session-fixture.ps1
```

What to record after a run, and the current results, are in the
[in-game test record](in-game-test-record.md).
