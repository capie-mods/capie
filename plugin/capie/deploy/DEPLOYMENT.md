# Switching the launcher and bootstrap to CAPIE binaries (prepared, not applied)

## One list of reviewed binaries: `bundle-inputs.json`

`plugin/capie/deploy/bundle-inputs.json` is the only place that names the reviewed CAPIE
binaries and their hashes. `build_launcher.ps1` bundles and pins exactly these files, and
`build_proxy.ps1` compiles the hashes of the two DLL entries into `version.dll`, both through
`deploy/BundleInputs.psm1`. Before this file existed the proxy compiled its own, older hash
list, so the proxy and the launcher could pin different builds without anyone noticing.
The release and bootstrap tools read the same file.

Format: a JSON array with exactly three objects, in this order (the launcher's
`BundleManifest` order and the proxy's `dll_hashes` order):

```json
[
  {"name": "capie_core.dll",     "source": "plugin/build/capie-v15-dev/capie_core.dll",     "sha256": "<64 lower-case hex>", "dll": true},
  {"name": "capie_editor.dll",   "source": "plugin/build/capie-v15-dev/capie_editor.dll",   "sha256": "<64 lower-case hex>", "dll": true},
  {"name": "capie_injector.exe", "source": "plugin/build/capie-v15-dev/capie_injector.exe", "sha256": "<64 lower-case hex>", "dll": false}
]
```

| Field | Meaning |
| --- | --- |
| `name` | File name inside the package's `runtime/` folder. Exactly `capie_core.dll`, `capie_editor.dll`, `capie_injector.exe`. |
| `source` | Repository-relative path of the reviewed build output, forward slashes, no `..`; ends with `/` + `name`. |
| `sha256` | SHA-256 of that file, 64 lower-case hex characters. |
| `dll` | `true` for the two DLLs (PE DLL expected, injected, pinned by the proxy), `false` for the injector. |

No other field is allowed; a missing or extra field, another order, upper-case hex or a
wrong `dll` flag fails every build that reads the file.

Build-time checks:

- every `source` must exist and have its `sha256` (a missing reviewed build fails the
  build; it is never replaced by a fresh compile, because MSVC output is not
  bit-reproducible);
- the hashes in the generated `launcher_manifest.generated.hpp` (launcher) and
  `proxy_manifest.generated.hpp` (proxy) are read back and must equal the JSON's (all
  three for the launcher, the two `dll` entries for the proxy). This is the check that
  the proxy's accepted hashes and the launcher bundle cannot differ;
- the table between the `bundle-inputs` markers below must list the same names and
  hashes (any case) in the same order;
- each build warns when the other build's output folder still holds a generated header
  with other hashes (a stale `plugin/build/capie-launcher` or `plugin/build/capie-proxy`:
  rebuild it).

To move to new reviewed binaries, change `bundle-inputs.json` and the table below in the
same commit, then rebuild both the launcher and the proxy.

## Proxy package layout

`plugin/capie/build_proxy.ps1` builds the opt-in proxy from the reviewed core/editor hashes
in `bundle-inputs.json`. For a future signed release, place its `plugin/build/capie-proxy/version.dll` at
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
MSVC output is not bit-reproducible. `bundle-inputs.json` holds them; this table mirrors it and
both builds check that it does:

<!-- bundle-inputs:begin -->
| File | SHA-256 |
| --- | --- |
| capie_core.dll | 4C4C237C081B7D08894D0BDB380C98D33E0656116D072250635ECB6AB7E0A7F4 |
| capie_editor.dll | 65453B0D72A8FB5D727DD6C904905CBBC5F004B5607707F7A26CB1D596270837 |
| capie_injector.exe | 72E552BA18700BF4FC8482BB9BC0D11D0CEB5F8173DDA60AE08B8F0EF754644D |
<!-- bundle-inputs:end -->

The proxy is switched too: it pins the two DLL hashes above from the same file. A proxy built
before this change still carries the stage-2 (1.0) pins below and refuses the 1.5 DLLs; rebuild it.

Stage-2 release build after the clang-tidy pass (`plugin/build/capie/`; historical, no longer pinned):

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
