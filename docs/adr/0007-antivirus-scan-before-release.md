# ADR-0007: Scan every published binary on VirusTotal before release

- Status: Accepted
- Date: 2026-10-11
- Source: [distribution decision log](../evidence/decision-log/DISTRIBUTION-DECISIONS.md), decisions 6 and 7

## Context

CAPIE loads itself into another process, which is also what some malware does. Heuristic
antivirus warnings are likely, and players need a way to check the files.

## Decision

- Every published binary (launcher, DLLs, injectors) is scanned on VirusTotal before release,
  and the links to the results go into the release notes. This is mandatory.
- Uploading to VirusTotal makes the file public. It is done only for the final release
  artifacts, and each upload needs the maintainer's consent. Consent was given on 2026-10-11 for
  uploading final release artifacts when a release is ready; it covers release files only, and
  every upload is recorded in the release notes.
- During development, binaries are scanned locally only, without publishing them, with Windows
  Defender (`MpCmdRun -Scan -ScanType 3`).

## Consequences

- On 2026-10-11 a local Defender custom scan of five candidates (country capability, inline
  editor debug bridge, inline editor render bridge, launcher candidate, caption refresh
  candidate) reported "found no threats". That is one engine without cloud heuristics, not a
  VirusTotal result.
- The release checklist has a VirusTotal step ([releasing](../contributing/releasing.md)).
