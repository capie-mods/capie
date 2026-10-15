# ADR-0031: Build trust without a paid code-signing certificate

- Status: Accepted
- Date: 2026-10-14
- Source: [distribution decision log](https://github.com/SirCypkowskyy/hoi4-rename-anything/blob/monorepo-before-split-20261014/docs/evidence/decision-log/DISTRIBUTION-DECISIONS.md),
  "2026-10-14 — podpis plików"

## Context

Unsigned native files trigger SmartScreen and antivirus warnings. A paid certificate costs money
every year for a free mod. [ADR-0003](0003-open-source-mit.md) already mentions free programmes
for open source (Certum Open Source, SignPath).

## Decision

- No paid certificate.
- Trust comes from: open code; builds in CI (GitHub Actions); a release manifest with SHA-256
  hashes, signed with the project's own key; VirusTotal before release
  ([ADR-0007](0007-antivirus-scan-before-release.md)); a clear description of what the files do;
  instructions for the SmartScreen warning.
- Once the repository is public, apply to the SignPath Foundation (free for open source).
- Caveat from the maintainer: if players complain about installation, the alternative is the
  proxy loader (a DLL in the game folder, pull request #1; [ADR-0024](0024-proxy-dll-loader.md)).

## Consequences

- The release process has steps for the manifest, its signature and the SmartScreen notes
  ([releasing](../contributing/releasing.md)).
