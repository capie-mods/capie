# ADR-0021: Two DLLs, and a release variant without diagnostic hooks

- Status: Accepted
- Date: 2026-10-12
- Source: [API decision log](https://github.com/SirCypkowskyy/hoi4-rename-anything/blob/monorepo-before-split-20261014/docs/evidence/decision-log/API-DECISIONS.md), "Decyzje etapu 2 (2026-10-12)"

## Context

Stage 2 ([ADR-0020](0020-native-code-quality-bar.md)) reorganised the native code. Country names
and the editors have different risks and different hooks, and the diagnostic hooks used during
development should not run on players' machines.

## Decision

1. Two DLLs: `capie_core.dll` (country names, status, capabilities) and `capie_editor.dll` (the
   editors).
2. The release variant has no diagnostic hooks. It installs four hooks: Tick, Bind, Dispatch and
   the effect constructor. Full diagnostics exist only in the debug variant. This was to be
   confirmed in the game.
3. clang-tidy and clang-format come from the Visual Studio component "C++ Clang tools for
   Windows", which the maintainer agreed may be installed on the development machine.

## Consequences

- The release and debug variants both passed the in-game test on 2026-10-12; the release build
  writes no keyboard or text logs.
- Later features add hooks (the name layer of CAPIE 1.2 adds three to `capie_core.dll`); the
  rule that diagnostics stay in the debug variant still holds.
