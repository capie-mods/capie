# Installed-launcher child fixture

Build using `bash plugin/capie/tests/macos_launcher/build.sh OUTPUT_DIRECTORY`.
These binaries exercise a wrapper/exec/DYLD boundary, not CAPIE or HOI4 hooks.

Configure an external test `launcher-settings.json` with `exePath` equal to the
absolute `child-wrapper` path and these four `exeArgs`:

1. `--enabled`, `--disabled`, or `--invalid`;
2. absolute `recorder` path;
3. absolute `marker.dylib` path;
4. an existing, fresh result directory.

Use a temporary `gameDataPath`, gameId `capie_fixture` and a distinct display name.
Run the installed launcher with `--pdxlGameDir STAGING --gameDir STAGING
--pdxlDataSuffix capie-probe`. The suffix separates launcher state, not OS/network
access. Verify the fixture display name before pressing PLAY. Existing launcher
instances can interfere; never press PLAY in the real HOI4 window for this fixture.

The recorder emits only PID, parent PID, cwd and whether DYLD is present. It never
logs authentication arguments. The marker emits only the PID in which it loaded.
Enabled succeeds when marker PID equals recorder PID. Disabled/invalid succeed
when the recorder runs without DYLD and no marker file is created.

Observed results through installed Paradox Launcher 2026.12 are retained in
`docs/macos/launcher-child-results.json`; context and limits are in
`docs/macos/loader-research.md`. All three modes passed on x86_64/Rosetta.

This fixture uses explicit switches, not a JSON playset gate. It does not preserve
Steam insertion libraries or arbitrary game arguments. Do not install it as the
production loader. It never changes the game executable or its launch settings.
