# API decisions: where they live now

The API and architecture decisions are recorded as [architecture decision records](adr/README.md).
This page stays because code comments cite decisions by their number in the original log, which
is archived unchanged (in Polish) in
[evidence/decision-log/API-DECISIONS.md](evidence/decision-log/API-DECISIONS.md).

| Original section and number | Record |
|---|---|
| First list (2026-10-11), 1: keywords or variables, indicator snippet | [ADR-0009](adr/0009-script-contract-variant-b.md), [ADR-0010](adr/0010-in-game-capie-indicators.md) |
| First list, 2: text in the API | Research question, not a decision; context of [ADR-0009](adr/0009-script-contract-variant-b.md) |
| First list, 3: saves after uninstalling CAPIE, visible state | [ADR-0006](adr/0006-script-only-fallback.md), [ADR-0010](adr/0010-in-game-capie-indicators.md) |
| First list, 4: reject players without CAPIE | Superseded by [ADR-0012](adr/0012-multiplayer-level-l0.md) |
| First list, 5: versioning and safe degradation | [ADR-0011](adr/0011-versioning-and-graceful-degradation.md) |
| First list, 6: permissions, host and overlord exceptions | [ADR-0013](adr/0013-rename-permissions.md), then [ADR-0029](adr/0029-game-rules-event-and-restore.md) |
| First list, 7: main-menu indicator | [ADR-0010](adr/0010-in-game-capie-indicators.md) |
| First list, 8: minimal invasiveness, validating other mods' data | [ADR-0011](adr/0011-versioning-and-graceful-degradation.md) |
| Naming (2026-10-11) | [ADR-0001](adr/0001-product-names.md) |
| Order of work to the first release (2026-10-11) | [ADR-0019](adr/0019-release-roadmap-and-repository-split.md) |
| CAPIE architecture, 1: variant B | [ADR-0009](adr/0009-script-contract-variant-b.md) |
| CAPIE architecture, 2: multiplayer L0 | [ADR-0012](adr/0012-multiplayer-level-l0.md) |
| CAPIE architecture, 3: SDK as its own Workshop item and repository | [ADR-0014](adr/0014-script-sdk-separate-item.md) |
| CAPIE architecture, 4: "CAPIE loaded / missing" line | [ADR-0010](adr/0010-in-game-capie-indicators.md) |
| CAPIE architecture, 5: wider character set | [ADR-0015](adr/0015-character-set.md) |
| CAPIE architecture, 6: overlord over a player subject, no veto | [ADR-0013](adr/0013-rename-permissions.md) |
| CAPIE architecture, 7: host rights through a lobby game rule | [ADR-0013](adr/0013-rename-permissions.md) |
| CAPIE architecture, 8: corpus of game builds, Steam tools run by the maintainer | [ADR-0018](adr/0018-build-corpus-and-self-hosted-ci.md) |
| CAPIE architecture, 9: self-hosted CI | [ADR-0018](adr/0018-build-corpus-and-self-hosted-ci.md) |
| CAPIE architecture, 10 (option a): signature scan after a patch | [ADR-0016](adr/0016-game-updates-signature-scan.md) |
| CAPIE architecture, 11: `versions.json` | [ADR-0017](adr/0017-versions-json.md) |
| Corrections after reverse engineering (2026-10-11), on decision 4 | [ADR-0012](adr/0012-multiplayer-level-l0.md) |
| Corrections, on decision 6: no host trigger, rule only | [ADR-0013](adr/0013-rename-permissions.md) |
| Corrections: permissions checked in the effect | [ADR-0013](adr/0013-rename-permissions.md) |
| Repositories (2026-10-11) | [ADR-0019](adr/0019-release-roadmap-and-repository-split.md) |
| Stage 2, C++ code quality | [ADR-0020](adr/0020-native-code-quality-bar.md) |
| CAPIE backlog: character portraits | [ADR-0026](adr/0026-character-portraits-backlog.md) |
| Stage 2 decisions (2026-10-12), 1 to 3 | [ADR-0021](adr/0021-two-dlls-and-release-variant.md) |
| Roadmap decisions (2026-10-12), 1, 4 and 5 | [ADR-0022](adr/0022-capie-feature-roadmap.md) |
| Roadmap decisions, 2: display-only exception | [ADR-0023](adr/0023-display-only-features-switch-off-alone.md) |
| Roadmap decisions, 3: extended Latin in 1.1, Cyrillic later | [ADR-0015](adr/0015-character-set.md) |
| CAPIE console commands (2026-10-13) | [ADR-0025](adr/0025-console-commands.md) |
| 2026-10-13, subjects and the old editor: no SDK dependency, ruling party only | [ADR-0027](adr/0027-subjects-without-sdk-dependency.md) |
| 2026-10-13, subjects and the old editor: alphabet unchanged | [ADR-0015](adr/0015-character-set.md) |
| 2026-10-13, the player-given name is its own layer | [ADR-0028](adr/0028-custom-name-layer.md) |
| 2026-10-14, rename rights rule, factions, restore cooldown, news event | [ADR-0029](adr/0029-game-rules-event-and-restore.md) |
| 2026-10-14, player-chosen flag | [ADR-0032](adr/0032-custom-flag-layer.md) |
| 2026-10-14, flags: pool rule, cooldown, reset on cosmetic tag change | [ADR-0032](adr/0032-custom-flag-layer.md); open question in [ADR-0028](adr/0028-custom-name-layer.md) |

New decisions go into [`adr/`](adr/README.md), not into this file.
