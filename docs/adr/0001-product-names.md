# ADR-0001: Rename Anything is the mod, CAPIE is the native library

- Status: Accepted
- Date: 2026-10-11
- Source: [API decision log](../evidence/decision-log/API-DECISIONS.md), section "Nazewnictwo (2026-10-11)"

## Context

The project started as one mod with an experimental native component. The native part grew
into something other mods could use: free text names for countries, a text field bridge, status
keys. One name for both would tie the library to a single mod.

## Decision

- **Rename Anything** is the mod that renames things. It ships through the Steam Workshop as
  script and GUI files.
- **CAPIE (Cyprian's API Extender)** is the native DLL library that extends the HOI4 modding API.
  Rename Anything is its first consumer.
- Binary file names that already exist (`rename_anything_*.dll`, the injectors, the debug
  controller) are pinned in manifests, tests and reviews. Renaming them needs a rebuild, new
  hashes, new manifests and a new review, so it happens as a separate stage of the roadmap
  ([ADR-0019](0019-release-roadmap-and-repository-split.md), [ADR-0020](0020-native-code-quality-bar.md)),
  not in passing.

## Consequences

- Documentation never says "Rename Anything API". The library is CAPIE.
- Until the rename stage, current builds carry legacy file names, and documents that mention
  them say so.
- The design system treats the two as one family with two names
  ([DESIGN-SYSTEM.md](../design/DESIGN-SYSTEM.md), "Two names, one system").
