# Documentation

Start with the [README](../README.md) for what the project is and what works today.

## Players

The player guides live on the two documentation sites, whose sources are in `sites/`:

- Rename Anything: [overview](../sites/rename-anything/docs/index.md),
  [how to use](../sites/rename-anything/docs/how-to-use.md),
  [install](../sites/rename-anything/docs/install.md),
  [known issues](../sites/rename-anything/docs/known-issues.md),
  [FAQ](../sites/rename-anything/docs/faq.md). Polish:
  [po polsku](../sites/rename-anything/docs/pl/index.md).
- CAPIE: [overview](../sites/capie/docs/index.md), [safety](../sites/capie/docs/safety.md),
  [install](../sites/capie/docs/install.md), [multiplayer](../sites/capie/docs/multiplayer.md),
  [FAQ](../sites/capie/docs/faq.md).

## Modders

- [CAPIE script contract](../plugin/capie/CONTRACT.md): the versioned contract (API level 7) and
  [the console part](../plugin/capie/CONTRACT-console.md).
- [CAPIE script SDK](../sdk/capie-script-sdk/README.md), with demo mods in
  `sdk/capie-script-sdk/examples/`.
- The modder guide on the CAPIE site: [for modders](../sites/capie/docs/modders/index.md).

## Contributors

- [Architecture](contributing/architecture.md): how the mod, CAPIE, the loaders and the SDK fit
  together.
- [Building and testing](contributing/building-and-testing.md): generator, checks, lint, native
  builds, mutation tests, sites, the in-game test helper.
- [In-game test record](contributing/in-game-test-record.md): what has been checked in the game,
  per feature.
- [Releasing](contributing/releasing.md): the release checklist.
- [Release script](contributing/release-script.md): local build, GitHub release, VirusTotal and
  Steam Workshop upload with `tools/release/release.py`.
- [Proxy loader](contributing/proxy-loader.md) and [multiplayer test plan](contributing/multiplayer-test-plan.md).
- [CAPIE native code](../plugin/capie/README.md), [installer](../tools/bootstrap/README.md),
  [deployment notes](../plugin/capie/deploy/DEPLOYMENT.md).
- [Design system](design/DESIGN-SYSTEM.md) and its [tokens](design/tokens.json): the colours,
  type and rules shared by the game UI, diagrams, sites, launcher and CLI.

## Decisions and research

- [Architecture decision records](adr/README.md). The old numbered decision lists map to them in
  [API-DECISIONS.md](API-DECISIONS.md) and [DISTRIBUTION-DECISIONS.md](DISTRIBUTION-DECISIONS.md).
- [Research](research/README.md): how HOI4 1.19.3.0 handles names, worked out by reverse
  engineering, and a study of native DLLs on the Steam Workshop (policy, precedents, risks).
- [Evidence archive](evidence/README.md): raw logs, screenshots, run notes and the original
  session notes behind the documents above.
