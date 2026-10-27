# Switching the launcher and bootstrap to CAPIE binaries (prepared, not applied)

## Proxy package layout

`plugin/capie/build_proxy.ps1` builds the opt-in proxy from the reviewed core/editor hashes
below. For a future signed release, place its `plugin/build/capie-proxy/version.dll` at
`proxy/version.dll` inside the package. Keep `capie_core.dll` and `capie_editor.dll` under
`runtime/`; the proxy resolves them from the installed `current/runtime/`. Include the
matching launcher/injector and MinHook license for the launcher alternative.

The signed release manifest must list the proxy and every runtime file with the actual
reviewed hashes. Rebuild the proxy whenever either payload hash changes, since its trust
pins are compiled in. The bootstrap's unpublished release configuration remains a
placeholder; this change does not publish or silently repin a release.

Development startup acceptance and cleanup evidence:
[proxy-loader.md](../../../docs/contributing/proxy-loader.md).

Stage 2 does not touch the accepted manifests. After independent review and a full
in-game test of the CAPIE build, these replacements are needed. SHA-256 values must be
taken from the reviewed build; MSVC output is not bit-reproducible across rebuilds (PE
timestamps), so the values below are those of the stage-2 build only.

## CAPIE 1.5 release pinned in the launcher (2026-10-15)

> Since CAPIE 1.5.1 the reviewed hashes live in one machine-readable file,
> `plugin/capie/deploy/bundle-inputs.json`, which `build_launcher.ps1`, `build_proxy.ps1` and the
> release kit (`tools/release`) read. The tables below are the review history; when they and the
> JSON file disagree, the JSON file is what gets built and packaged.

Review `docs/evidence/session-20261015/capie-15-review/review.txt` (W1): the launcher bundles
exactly these three release files of `plugin/build/capie-v15-dev/` (top level only; never
`debug/` or `capie-v15-console-dev/`). They are not rebuilt before the in-game test (W2), because
MSVC output is not bit-reproducible. `plugin/capie/build_launcher.ps1` (`$BundleInputs`) checks
them against this table:

| File | SHA-256 |
| --- | --- |
| capie_core.dll | 7A1C946F8A15F6120A0232453E3013B9F854EC2A6B526D0AB9C86F19959D0DE1 |
| capie_editor.dll | 0DAD2732552305F43E1E87FB6E10E6A5B473E683EA27F1E82D0EBD9B8934D656 |
| capie_injector.exe | EB5E9FF5F9B944B55CA5EB4B343D18D507584284FB60F5F119A8BE8843484C25 |

The proxy (`build_proxy.ps1`, `src/proxy`) is not switched: its compiled pins stay on the
stage-2 (1.0) hashes below until the proxy is rebuilt from these files.

Stage-2 release build after the clang-tidy pass (`plugin/build/capie/`; still pinned by the proxy):

| File | SHA-256 |
| --- | --- |
| capie_core.dll | D95B510D71F78AC5F5AB83C2EE38A803E6B5D6B651B7EBB4AD281820EB93BE00 |
| capie_editor.dll | 742D655E2DC92AF036C09FB6F4E808039040D9F696D8908E6D4FAED02C433CCA |
| capie_injector.exe | 3370A9855FFCD21480A764257627E9197D57C767C3EFEFDF5601153B47EEE64D |

## 1. `plugin/launcher/build.ps1` (`$inputs`, four entries)

Superseded for the wrapper-mode launcher: `plugin/capie/build_launcher.ps1` pins the three
CAPIE 1.5 release hashes above, copies them to `plugin/build/capie-launcher/runtime/` and generates
`launcher_manifest.generated.hpp` (three entries: core, editor, injector). The accepted
`plugin/launcher/` stays unchanged as the reference; the four-entry edit below only applies
if that old candidate were ever rebuilt with CAPIE binaries.

The launcher runs `bundle[dll_index + 1] PID bundle[dll_index]` for `dll_index` 0 and 2.
One injector serves both DLLs, so the same file is listed twice and `launcher.cpp` needs
no change:

```powershell
$inputs = @(
    @{ source='plugin/build/capie/capie_core.dll';     name='capie_core.dll';     sha='<reviewed>'; dll=$true },
    @{ source='plugin/build/capie/capie_injector.exe'; name='capie_injector.exe'; sha='<reviewed>'; dll=$false },
    @{ source='plugin/build/capie/capie_editor.dll';   name='capie_editor.dll';   sha='<reviewed>'; dll=$true },
    @{ source='plugin/build/capie/capie_injector.exe'; name='capie_injector.exe'; sha='<reviewed>'; dll=$false }
)
```
Also replace `plugin/src/*` and `plugin/build.ps1` in `$protectedNames` with the CAPIE
sources, and update the README table (country DLL, editor DLL, injector). The launcher
currently bundles the render bridge B20FBE88; the CAPIE editor replaces the rev7 caption
bridge, so this switch also moves the launcher from the render bridge to rev7 behaviour.

## 2. Bootstrap release manifest (`tools/bootstrap/src/Shared/Configuration.ps1`, generated `install.ps1`)

Replace the three runtime entries:

```powershell
'runtime/capie_core.dll'     = '<reviewed>'
'runtime/capie_injector.exe' = '<reviewed>'
'runtime/capie_editor.dll'   = '<reviewed>'
```
and the launcher hash after the launcher is rebuilt. `install.Tests.ps1` uses
`runtime/rename_anything_country.dll` as fixture names; rename them to keep the tests
describing real files.

## 3. Mod localisation

Already done in this stage (`tools/modgen/ui_strings.py`): `RN_COUNTRY_PLUGIN` now
references `$RN_PLUGIN_STATUS_EN$` / `$RN_PLUGIN_STATUS_PL$`. Ship the mod and
`capie_core.dll` together: the accepted country DLL overrides only the bare key, so with
the new localisation it would show "CAPIE missing".

## 4. QA tooling and documents

- The editor log is now `capie_editor.log` next to the DLL (was `caption_refresh_bridge.log`).
  Line families (`RN_INLINE`, `RN_IEBR`, `RN_CAPTION`, `RN_FOCUS`, `RN_HIDE`, `RN_KEYLOG`,
  `RN_KEYTICK`, `RN_DEBUG*`) and event names are unchanged. The release flavor writes no
  `RN_KEYLOG`/`RN_KEYTICK`/`RN_DEBUG` lines, no typed text, and its init line starts with
  `RN_LIFECYCLE initialized`.
- Session notes and QA scripts that grep `caption_refresh_bridge.log` must read
  `capie_editor.log` instead (`tools/test-session.ps1` itself has no DLL or log names).
- `plugin/README.md` describes the accepted binaries; replace with `plugin/capie/README.md`
  when the old builds are retired.
