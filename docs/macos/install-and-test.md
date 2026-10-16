# CAPIE on macOS: build, installation and verification status

**Not ready for native installation.** This worktree contains static reverse-engineering
evidence and offline test tooling. It does not yet provide a working CAPIE dylib pair,
validated loader, or native installer. API level 7 remains the target Windows contract;
an incomplete macOS implementation must not announce that capability.

## Target and current artifacts

Steam HOI4 1.19.3.0 (`Operation Postern v1.19.3.0.c01a (5632)`), thin x86_64 Mach-O,
SHA-256 `09419b087f8b4656c00df337bf838ff34c9b1d816b76cef5383ae6a4ac15b1fb`.
Preferred image base `0x100000000`. Apple Silicon needs Rosetta to run x86_64 code.
No game binary or launcher binary is modified by the tools below.

| Artifact | What it establishes |
|---|---|
| [Full inventory](address-inventory.json) | Every core layout constant and feature pin-table entry, with 140/140 source pin roles mapped statically and explicit runtime limitations. |
| [Editor map](editor-map.md) | Scripted-window, command, widget, caption and focus observations; missing mappings are explicit. |
| [Name, flag, portrait and host map](layers-map.md) | Per-feature address/offset evidence and unresolved behavior. |
| [Console map](console-map.md) | Additional API 3 dependencies outside the main pin headers, including different entry layout and ABI. |
| [Localisation coverage](localisation-coverage.md) | Corrects the earlier mixed reader/writer inventory; proves a common `$KEY$` path reaches the length-aware lookup; complete coverage remains open. |
| [Port plan](port-plan.md) | Platform dependencies, required adapters, loader constraints and acceptance work. |

## Build and run the currently portable tests

Requires macOS with Xcode Command Line Tools and Python 3. On Apple Silicon the
runner executes both arm64 and x86_64 test binaries; failure to run x86_64 is an
error, not a silently skipped success. On Intel it runs x86_64 only.

From the repository root:

```sh
tools/test-macos-offline.sh
tools/test-macos-profile.sh
python3 -m unittest discover -s docs/macos/re -p 'test_*.py'
python3 docs/macos/re/verify_evidence.py \
  "$HOME/Library/Application Support/Steam/steamapps/common/Hearts of Iron IV/hoi4.app/Contents/MacOS/hoi4"
```

The runner prints its exact scope and preserves test executables in the temporary
directory it prints. The runner now also tests image/profile verification, native return-string construction,
four-table widget guards and the shared localisation hook bodies (20 forwarding/override checks per architecture). See its per-suite output for the current check count;
the country/faction name reader now also executes shared protocol logic with the macOS
variable-table adapter; flag decoding and native cosmetic strings bring the combined
feature suite to 32 checks per architecture. These remain separate from CAPIE runtime activation. The 23 Python tests exercise digest mismatch, corrupted pins, invalid
ranges, empty evidence, duplicate IDs and manifest-export validation (references,
companions, pointer slots and relative targets). The default file check verifies 23 recorded
localisation evidence ranges after the full executable digest matches. Verify the
combined core/editor/layers/host evidence with `--manifest docs/macos/re/port-pin-evidence.json`
and the formatter analysis with `--manifest docs/macos/re/localisation-callback-evidence.json`.
Regenerate the combined evidence with `python3 docs/macos/re/build_pin_evidence.py`
when its source manifests change; generation requires the local `re/hoi4` symlink.

The shared localisation source now uses POSIX `strnlen` on macOS; Windows retains
`strnlen_s`. The macOS tests exercise the actual shared implementation, including
inactive forwarding, bounded keys, RN1 forms, status keys and pointer lifetime.
This does not establish that hooks have been installed or that API 7 works in-game.

For the separate owned-code experiments, see [DYLD/launcher research](loader-research.md),
[code-patching fixture](patch-fixture.md) and [restricted detour backend](quiescent-detour.md).
Run `tools/test-macos-detour.sh` for the 92-check x86_64 detour fixture. These never start HOI4.
The cross-architecture offline runner currently passes 1,171 checks per architecture
(2,342 on this Apple Silicon host); the detour checks are a separate suite.
The shared localisation test also passed AddressSanitizer and UndefinedBehaviorSanitizer
on arm64. No Windows regression result is implied by these macOS runs.

**These commands do not build CAPIE dylibs.** There is no supported macOS dylib
build command yet. Do not compile the Windows composition roots by merely removing
`__fastcall` or replacing `windows.h`: engine layouts, hidden returns, command
records and calls differ, as the maps show.

For fast independent disassembly of one range, use Apple llvm-objdump without
`--macho` (that mode does not provide the same bounded disassembly behavior):

```sh
/Library/Developer/CommandLineTools/usr/bin/llvm-objdump -d \
  --x86-asm-syntax=intel --start-address=0x10288d420 --stop-address=0x10288d4d6 \
  "$HOME/Library/Application Support/Steam/steamapps/common/Hearts of Iron IV/hoi4.app/Contents/MacOS/hoi4"
```

The preceding exported symbol label is not a recovered name for a stripped function.
Use the function-start metadata, instruction addresses and corroborating data flow.
The optional `re/hoi4`, `re/dis.txt`, `re/dis.idx` files are ignored local research
inputs; the evidence checker does not need those symlinks or the full dump.

## Installation

There is currently **no native CAPIE installation step on macOS**. Do not set up an
injection environment variable or copy an unvalidated library into the game bundle.
The required production loader must preserve the Paradox playset flow and CAPIE
switch (ADR-0004/0030), verify the exact game image and all required pins, and let
vanilla HOI4 start when checks fail. This has not been demonstrated on macOS.

Rename Anything's separate repository has `tools/install-macos.sh` and
`docs/install-macos.md` for its script-only installation. That installer does not
install CAPIE or establish native support. Its file operations were previously
tested in a temporary folder; macOS gameplay remains unverified.

## Checked versus not checked

- Checked offline: artifact identity, the retained instruction evidence, the
  explicitly listed portable protocol tests and the evidence checker's negative tests.
- Static/inferred only: semantic identities and proposed interception points in
  the maps. Missing or ambiguous entries remain unavailable for activation.
- Checked in a controlled real-game launch: the read-only diagnostic dylib loaded,
  exact image identity and three localisation entry prefixes matched and the game
  reached its menu. A second diagnostic launch passed all 240 checks covering
  218 static ranges plus 22 relative-target guards. See [diagnostic result](read-only-diagnostic.md).
  This used temporary opt-in data, not the real mod playset or complete Steam chain.
- Not checked: CAPIE runtime activation, in-game detours, CAPIE GUI,
  Apply/Cancel/Enter, names, flags, portraits, console, save/load and language reload.
- Not checked: Windows compilation/regression suite in this macOS environment,
  multiplayer, hot join and Ironman. Existing Windows source remains the reference.

Before native installation can be documented as working, finish the map and ABI
adapters, test the hook backend and fail-closed loader on fixtures, run Windows
regression checks, then record the actual in-game acceptance results. File-pin
verification alone cannot substitute for any of those checks.

## Shared Windows and macOS release

The completed port must ship in the same versioned GitHub Release as Windows.
That release must retain the existing Windows configuration and package and add
the macOS configuration and package. Follow the installation instructions for your
operating system; both sets of instructions must point to the same release.
This is a release requirement, not a claim that native macOS artifacts are available today.

## Native launcher component and shared packaging

The native parent/child now build without a Python runtime dependency:
`tools/build-macos-launcher.sh /absolute/new-output-directory`. They are not a
complete CAPIE macOS installation. A tested component packager preserves executable
permissions and labels its output `runtime_ready:false` with no provided API level.
See [the shared Windows/macOS release layout](shared-release-layout.md) for exact
artifact boundaries and remaining signing, installer and runtime requirements.
