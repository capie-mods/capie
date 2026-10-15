# ADR-0020: Quality bar for the native code when it becomes CAPIE

- Status: Accepted, implemented in stage 2
- Date: 2026-10-11
- Source: [API decision log](https://github.com/SirCypkowskyy/hoi4-rename-anything/blob/monorepo-before-split-20261014/docs/evidence/decision-log/API-DECISIONS.md), "Etap 2 — wymagania jakości kodu C++"

## Context

The native code grew as a series of probes and candidates. Renaming the binaries to `capie_*.dll`
needs a rebuild and a new review anyway, so it is the moment to bring the code to the standard a
public library needs.

## Decision

The rename to `capie_*.dll` is combined with a clean-up of the native code:

- DRY; a clean architecture with a core, features and engine adapters;
- named constants instead of magic numbers and strings;
- every function, class and constant documented with Doxygen comments, including parameters and
  return values;
- a thorough English description at the top of every module, edited for plain language;
- clang-format and clang-tidy.

Behaviour must stay equivalent: every existing offline test, fingerprint and mutation check
passes, then an independent review and a full in-game test follow.

## Consequences

- The result is `plugin/capie/` ([README](https://github.com/capie-mods/capie/blob/main/plugin/capie/README.md)). Stage 2 passed review,
  clang-tidy and clang-format with zero findings, and the in-game test in release and debug
  variants ([in-game test record](../contributing/in-game-test-record.md)).
