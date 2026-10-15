# Distribution decisions: where they live now

The distribution decisions are recorded as [architecture decision records](adr/README.md). The
original log is archived unchanged (in Polish) in
[monorepo archive: docs/evidence/decision-log/DISTRIBUTION-DECISIONS.md](https://github.com/SirCypkowskyy/hoi4-rename-anything/blob/monorepo-before-split-20261014/docs/evidence/decision-log/DISTRIBUTION-DECISIONS.md).

| Original number | Record |
|---|---|
| 1: Workshop for the mod, GitHub Releases for the launcher | [ADR-0002](adr/0002-distribution-model.md) |
| 2: launcher in Steam launch options or a shortcut | [ADR-0004](adr/0004-launcher-through-paradox-launcher.md) |
| 3: public code, MIT (preliminary) | [ADR-0003](adr/0003-open-source-mit.md) |
| 4: unknown game build starts without the DLL | [ADR-0005](adr/0005-unknown-game-build.md) |
| 5: mode without the DLL | [ADR-0006](adr/0006-script-only-fallback.md) |
| Note after 5: multiplayer not verified, test after single player | [ADR-0012](adr/0012-multiplayer-level-l0.md) |
| 6: antivirus scan before release | [ADR-0007](adr/0007-antivirus-scan-before-release.md) |
| 7: consent to upload release files to VirusTotal | [ADR-0007](adr/0007-antivirus-scan-before-release.md) |
| 8: PowerShell bootstrap | [ADR-0008](adr/0008-powershell-bootstrap.md) |
| 9: always through the Paradox Launcher, inject only into own child | [ADR-0004](adr/0004-launcher-through-paradox-launcher.md) |
| 10 and its update: proxy DLL loader | [ADR-0024](adr/0024-proxy-dll-loader.md) |
| 2026-10-14: installation paths and the CAPIE playset switch | [ADR-0030](adr/0030-installation-paths-and-playset-switch.md) |
| 2026-10-14: file signing | [ADR-0031](adr/0031-trust-without-paid-code-signing.md) |

New decisions go into [`adr/`](adr/README.md), not into this file.
