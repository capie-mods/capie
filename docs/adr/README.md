# Architecture decision records

Each file records one decision: its context, what was decided, what follows from it, its status
and its date. Records are not edited after the fact except for their status line; a change of
mind gets a new record that supersedes or amends the old one.

The original decision logs (in Polish) are kept unchanged in
[[monorepo archive: docs/evidence/decision-log](https://github.com/SirCypkowskyy/hoi4-rename-anything/tree/monorepo-before-split-20261014/docs/evidence/decision-log)](https://github.com/SirCypkowskyy/hoi4-rename-anything/tree/monorepo-before-split-20261014/docs/evidence/decision-log). Code comments that cite "decision 6" or
"decision 10(a)" refer to the numbering of those logs; [API-DECISIONS.md](../API-DECISIONS.md) and
[DISTRIBUTION-DECISIONS.md](https://github.com/capie-mods/capie/blob/main/docs/DISTRIBUTION-DECISIONS.md) map each old number to its record.

Status values: **Proposed**, **Accepted**, **Amended** (partly changed by a later record),
**Superseded** (replaced by a later record).

## Index

| ADR | Decision | Status | Date |
|---|---|---|---|
| [0001](0001-product-names.md) | Rename Anything is the mod, CAPIE is the native library | Accepted | 2026-10-11 |
| [0002](0002-distribution-model.md) | Mod files on the Workshop, native files on GitHub Releases | Accepted | 2026-10-11 |
| [0003](0003-open-source-mit.md) | Public source code under MIT (preliminary) | Accepted | 2026-10-11 |
| [0004](0004-launcher-through-paradox-launcher.md) | Launcher that always goes through the Paradox Launcher | Accepted, method changed by 0024 and 0030 | 2026-10-11 |
| [0005](0005-unknown-game-build.md) | Unknown game build: start without CAPIE | Accepted, extended by 0016 | 2026-10-11 |
| [0006](0006-script-only-fallback.md) | Without CAPIE, fall back to scripts and say so | Accepted | 2026-10-11 |
| [0007](0007-antivirus-scan-before-release.md) | VirusTotal scan of every published binary | Accepted | 2026-10-11 |
| [0008](0008-powershell-bootstrap.md) | One-line PowerShell bootstrap | Accepted, extended by 0030 | 2026-10-11 |
| [0009](0009-script-contract-variant-b.md) | Script contract over variables (variant B) | Accepted | 2026-10-11 |
| [0010](0010-in-game-capie-indicators.md) | Show in the game whether CAPIE is loaded | Accepted | 2026-10-11 |
| [0011](0011-versioning-and-graceful-degradation.md) | Versioned API, graceful degradation, minimal footprint | Accepted | 2026-10-11 |
| [0012](0012-multiplayer-level-l0.md) | Multiplayer L0: handshake, feature lock, banner | Accepted, supersedes the first MP decision | 2026-10-11 |
| [0013](0013-rename-permissions.md) | Who may rename what, checked in the effect | Accepted, amended by 0029 | 2026-10-11 |
| [0014](0014-script-sdk-separate-item.md) | Script SDK as its own Workshop item and repository | Accepted | 2026-10-11 |
| [0015](0015-character-set.md) | Extended Latin first, Cyrillic later | Accepted | 2026-10-11 |
| [0016](0016-game-updates-signature-scan.md) | Opt-in signature scan after a patch, all or nothing | Accepted, amended by 0023 | 2026-10-11 |
| [0017](0017-versions-json.md) | Signed `versions.json` in the repository and releases | Accepted | 2026-10-11 |
| [0018](0018-build-corpus-and-self-hosted-ci.md) | Private build corpus, self-hosted CI | Accepted | 2026-10-11 |
| [0019](0019-release-roadmap-and-repository-split.md) | Order of work to the first release, repository split | Accepted | 2026-10-11 |
| [0020](0020-native-code-quality-bar.md) | Quality bar for the native code | Accepted | 2026-10-11 |
| [0021](0021-two-dlls-and-release-variant.md) | Two DLLs, release variant without diagnostic hooks | Accepted | 2026-10-12 |
| [0022](0022-capie-feature-roadmap.md) | CAPIE feature roadmap for 1.1 and 1.2 | Accepted | 2026-10-12 |
| [0023](0023-display-only-features-switch-off-alone.md) | Display-only features may switch off on their own | Accepted, amends 0016 | 2026-10-12 |
| [0024](0024-proxy-dll-loader.md) | Proxy DLL in the game folder | Accepted, superseded in part by 0030 | 2026-10-12 |
| [0025](0025-console-commands.md) | Console commands ship together | Accepted | 2026-10-13 |
| [0026](0026-character-portraits-backlog.md) | Character portraits in the Characters list | Proposed (backlog) | 2026-10-11 |
| [0027](0027-subjects-without-sdk-dependency.md) | Renaming subjects; no dependency on the script SDK | Accepted | 2026-10-13 |
| [0028](0028-custom-name-layer.md) | A custom country name is its own layer | Accepted, one open question | 2026-10-13 |
| [0029](0029-game-rules-event-and-restore.md) | Lobby rules for rights and a news event; restore uses the cooldown | Accepted | 2026-10-14 |
| [0030](0030-installation-paths-and-playset-switch.md) | Two installation paths, CAPIE mod in the playset as the switch | Accepted | 2026-10-14 |
| [0031](0031-trust-without-paid-code-signing.md) | Trust without a paid code-signing certificate | Accepted | 2026-10-14 |
| [0032](0032-custom-flag-layer.md) | A player-chosen flag as its own attribute | Accepted, not implemented | 2026-10-14 |
| [0033](0033-host-recognition.md) | Recognise the host by comparing user names, fail closed | Accepted, not implemented | 2026-10-14 |
| [0034](0034-repository-split-evidence-and-plan.md) | Split into four repositories; evidence stays in the archived monorepo | Accepted, split not executed | 2026-10-14 |
| [0035](0035-local-release-script.md) | Releases are built and published locally for now | Accepted | 2026-10-14 |
| [0036](0036-leader-portrait-and-feature-switches.md) | Leader portrait choice and a host switch for every feature | Accepted, not implemented | 2026-10-14 |
| [0037](0037-installer-closes-steam-on-request.md) | The installer closes Steam on request (`steam.exe -shutdown`), never by force | Accepted | 2026-10-15 |

## Writing a new record

Copy this outline into `NNNN-short-title.md` with the next free number:

```markdown
# ADR-NNNN: Decision in one sentence

- Status: Proposed | Accepted | Amended by ADR-XXXX | Superseded by ADR-XXXX
- Date: YYYY-MM-DD
- Source: where the decision was made (issue, pull request, meeting notes)

## Context

## Decision

## Consequences
```
