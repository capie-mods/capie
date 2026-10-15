# ADR-0003: Public source code under the MIT license (preliminary)

- Status: Accepted (license preliminary)
- Date: 2026-10-11
- Source: [distribution decision log](https://github.com/SirCypkowskyy/hoi4-rename-anything/blob/monorepo-before-split-20261014/docs/evidence/decision-log/DISTRIBUTION-DECISIONS.md), decision 3

## Context

CAPIE runs inside the game process. Players and antivirus vendors have good reason to be wary of
that. Free code-signing programmes for open source projects (Certum Open Source, SignPath)
require public code.

## Decision

The code will be public. The intended license is MIT; the choice is preliminary.

## Consequences

- Open code is part of the trust model ([ADR-0031](0031-trust-without-paid-code-signing.md)).
- Until a `LICENSE` file is added, all rights are reserved. The README says so.
- The repository is private today and becomes public when the mod is ready
  ([ADR-0030](0030-installation-paths-and-playset-switch.md)).
