# macOS port checkpoint — 2026-10-16

This checkpoint is work in progress, not a supported macOS release or a claim of
full Windows parity. Windows and macOS must ultimately ship under one release
tag/version, with separate platform assets and installation instructions.

## Implemented

- Exact-build evidence inventory, 157 numeric native mappings, native string,
  record, variable, widget and owned-string adapters, shared country and editor
  composition. Constructor, faction and portrait SysV differences are explicit.
- Minimal first-inserted bootstrap with 15 forwarding/dispatch stubs, 240 static
  profile checks and four allocator checks before patching, bounded rollback and
  an explicit poisoned state. Unknown image/order/policy is refused. The default
  build without generated policy cannot activate hooks.
- Late C++ runtime and native parent/child launcher, external launcher-settings
  staging, playset checks and a deterministic launcher-component package. The
  component package is explicitly not a ready CAPIE runtime release.

## Validation at checkpoint

All 28 integrated offline suites passed on arm64 and x86_64.
Native sanitizer fixtures, Intel/Rosetta fixtures,
bootstrap ABI/refusal fixtures and standalone runtime refusal tests are recorded
in the associated macOS reports. Release tooling tests pass (49 tests). The old
Rename Anything Workshop item was removed from CAPIE's release configuration;
that content belongs to its separate repository after the split.

The real game has previously reached its menu with a read-only diagnostic and
240 matching static-profile checks. A real Paradox Launcher recorder run used
the correct playset without changing its content. Those observations do not test
active native features. The production Windows build was not run on this Mac.

## Remaining acceptance gates

The late runtime requires `CAPIE_EXPERIMENTAL_RUNTIME=1` as well as a permitted
bootstrap. Its first initialized scripted-window Tick is currently used to select
the GUI owner thread. Tokens and a model vtable alone are not a complete proof of
thread ownership or object lifetime; this is an explicit experimental assumption,
not an established safety property. It needs focused runtime validation before
supported installation. Layer activation is sequential, not a global atomic switch.

The optional two-library launcher mode is implemented and tested; its option
documentation and independent review remain unfinished at this checkpoint.

Full native activation, editing/applying names, flags and portraits, localisation
refresh, language changes, save/load and the complete Steam-to-game launch chain
remain to be checked. No game hooks were activated by this checkpoint work, no
campaign was edited and no release was published. A permanent OS failure during
patch rollback remains an explicit catastrophic-failure limitation (POISONED),
not a successful fail-closed result.

## Follow-up checkpoint: bootstrap compatibility and observation

The bootstrap now accepts the main-image index permutation used by HOI4's
macOS 10.14 deployment target. It pins the three bundled dependencies, handles
bounded FAT32/FAT64 x86 slices and rejects unknown non-system loaded images.
Modern and legacy owned bootstrap fixtures pass, including dependency corruption.

A forward-only GUI recorder and a selectable observe runtime mode are implemented.
The runtime report uses a separate private raw JSONL writer. Standalone refusal
and report-permission tests pass. Observation of a real own GUI and its owner
thread is still pending; the observe path has not run in HOI4.

The developer bundle builder defaults to an inert bootstrap. An enabled profile
requires matching lifecycle evidence, a source-closure fingerprint including
headers and build scripts, exact dependency identities and post-build Mach-O checks.
The inert bundle was built successfully. The ready bundle and in-game activation
remain unchecked. This is still a development checkpoint, not a supported release.
