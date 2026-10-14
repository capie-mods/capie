# ADR-0002: Mod files on the Workshop, native files on GitHub Releases

- Status: Accepted
- Date: 2026-10-11
- Source: [distribution decision log](../evidence/decision-log/DISTRIBUTION-DECISIONS.md), decision 1
- Options considered: [distribution-options.txt](../evidence/session-20261011/claude-distribution/distribution-options.txt)

## Context

Players expect to get HOI4 mods from the Steam Workshop. Subscribing to a Workshop item does not
run native code, and nothing confirms that the HOI4 uploader accepts our DLLs
([Workshop and native code research](../research/workshop-native-code.md)).

## Decision

The mod's script and interface files are distributed through the Steam Workshop. The native
part (launcher, DLLs, installer) is distributed through GitHub Releases.

## Consequences

- A player who wants the features that need CAPIE installs two things. Keeping that second step
  small is a recurring requirement ([ADR-0008](0008-powershell-bootstrap.md),
  [ADR-0030](0030-installation-paths-and-playset-switch.md)).
- Release files need the trust measures of [ADR-0007](0007-antivirus-scan-before-release.md) and
  [ADR-0031](0031-trust-without-paid-code-signing.md).
