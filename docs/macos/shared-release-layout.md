# One CAPIE release, separate platform packages

The completed port must use one CAPIE version, one Git tag and one GitHub Release.
The version source remains `plugin/capie/src/core/game_build.hpp`; macOS must not
introduce an independent product version or announce API 7 before implementation
and acceptance. Existing Windows files and installer configuration remain unchanged.

| Artifact / configuration | Windows | macOS |
| --- | --- | --- |
| Complete platform asset | `CAPIE-{version}-win64.zip` | `CAPIE-{version}-macos-x86_64.zip` (not available yet) |
| Packaging configuration | `tools/release/release.toml` | Native launcher component builder below; complete-runtime configuration still required |
| Entrypoint | Existing Windows launcher/proxy configuration | Native `capie-parent` → original dowser → `capie-child` → original HOI4 |
| Installer instructions | Existing `install.ps1` documentation | macOS-specific instructions; never copy DLLs or component files into hoi4.app |
| Runtime manifest | Existing signed `release-manifest.json` contract | Separate platform manifest/signature must include native runtime and game hash; not implemented yet |

The existing releasekit GitHub step selects one Windows ZIP and its Windows manifest.
It has **not** been changed to enforce or upload a joint release. Before publication,
it must require both complete platform assets, equal product/API versions, separate
supported-game hashes, signatures and platform acceptance evidence. A draft or component
archive is not sufficient. Windows installer manifest names/format must not be silently
repurposed for macOS. A shared release index can reference the two verified platform
manifests once the macOS installer trust contract exists.

## Tested launcher component packaging

```sh
tools/build-macos-launcher.sh /absolute/new-native-build
uv run --no-project python tools/release/package-macos-launcher.py \
  /absolute/new-native-build /absolute/new-component-output
uv run --no-project python tools/release/tests/test_macos_component.py
```

The packager reuses releasekit's version reader, version-consistency checks, BundleEntry,
entry-path validation, SHA-256 and fixed ZIP timestamp. It packages exactly:

- `bin/capie-parent` and `bin/capie-child`, with Unix executable mode 0755;
- `launcher-component.json`, with shared version, target API, file sizes/hashes,
  `runtime_ready:false`, `provided_api_level:null`, `signed:false`;
- `INSTALL-macos.md`, explicitly identifying the incomplete component and its limitations.

Output is `CAPIE-{version}-macos-x86_64-launcher-component.zip` plus SHA256SUMS.txt.
The component is deterministic for identical bytes and rejects missing files, symlinks,
wrong architecture, wrong Mach-O file type and existing output directories. Header checks
are packaging sanity checks, not proof of executable semantics, source provenance or ABI.
There is no runtime dylib substitution, installer activation or publication operation.

## Remaining production requirements

1. Complete native runtime with correct API/script behavior and game acceptance; the
   read-only diagnostic cannot fill this role.
2. Trusted platform manifest/signature and build provenance binding the exact binaries
   to the shared version; the component's computed hashes are not an independent trust root.
3. Native installer ownership/update/uninstall, configured paths and atomic launcher
   coordination. Current process enumeration is only a snapshot.
4. Acceptance of the native parent through Steam/dowser, real enabled/disabled playsets,
   main/debug modes and fallback behavior. Direct-Paradox recorder success proves only
   the real-playset boundary already documented in development-child-launcher.md.
5. Joint-release pipeline wiring that refuses publication when either platform is missing
   or incomplete; no publication was performed during this packaging work.
