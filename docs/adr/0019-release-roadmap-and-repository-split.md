# ADR-0019: Order of work to the first release, and the repository split

- Status: Accepted. Steps 1 and 2 are done; step 3 is in progress.
- Date: 2026-10-11
- Source: [API decision log](https://github.com/SirCypkowskyy/hoi4-rename-anything/blob/monorepo-before-split-20261014/docs/evidence/decision-log/API-DECISIONS.md), "Kolejność prac do
  pierwszego wydania" and "Repozytoria (utworzone 2026-10-11)"

## Context

Several streams of work were running at once in one repository: the mod, the native code, the
launcher, the installer, the design system. They need an order, and they will end up in separate
repositories.

## Decision

Order of work up to the first release:

1. Integrate the editors and counters into `mod/` and test them in the game with CAPIE loaded
   (caption bridge and country DLL), still in this repository.
2. Rename the binaries to `capie_*.dll` as a separate stage: rebuild, hashes, manifests, review
   ([ADR-0020](0020-native-code-quality-bar.md)).
3. A versioned contract between CAPIE and mods (models, fields, selectors, capabilities);
   Rename Anything pins a CAPIE version.
4. Split into repositories with history kept (`git filter-repo`): CAPIE (`plugin/`, launcher,
   bootstrap) and Rename Anything (`mod/`, `tools/`, `probes/`). Evidence goes with its component.
   The design system becomes a shared folder or repository of its own.
5. Move the repositories to a new GitHub organisation. The maintainer creates the organisation;
   the transfer happens only after the maintainer confirms it.

Four empty private repositories were created on 2026-10-11 under the maintainer's account
(`SirCypkowskyy`):

| Repository | Content |
|---|---|
| `capie` | DLLs, launcher, bootstrap, CAPIE documentation (`sites/capie`) |
| `capie-script-sdk` | the CAPIE script SDK, its own Workshop item ([ADR-0014](0014-script-sdk-separate-item.md)) |
| `capie-design` | the shared design system (`docs/design/`) |
| `hoi4-rename-anything` | the mod (unchanged) |

## Consequences

- Folders are kept self-contained so they can move without edits: `sites/capie/`,
  `sites/rename-anything/`, `docs/design/`, `sdk/capie-script-sdk/`.
- The split happens at step 4 and the move to the organisation after it is finished.
