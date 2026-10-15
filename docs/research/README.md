# Research

Reverse engineering and background research behind the mod and CAPIE. All of it concerns Hearts
of Iron IV 1.19.3.0 for Windows x64 (`hoi4.exe` SHA-256
`7DC947BE34970DA1E1C787BCDDBE7F62B610FF258AEBF8F06AA8B348F3A031D5`). Addresses are RVAs from
image base `0x140000000`, and function names are analyst labels, not recovered symbols.

| Document | Topic |
|---|---|
| [naming-internals.md](naming-internals.md) | How the game stores, localises, saves and refreshes country, character and state names; the edit box; the command queue |
| [naming-signatures.json](naming-signatures.json) | Byte signatures of the functions in naming-internals, as data |
| [country-re-findings.md](country-re-findings.md) | Country names: why script alone is not enough, and the native design |
| [state-view-internals.md](state-view-internals.md) | The selected-state scripted GUI context and its parent window |
| [typed-state-variable-findings.md](typed-state-variable-findings.md) | Why a state scope in a variable is not a state ID, and the city lookup |
| [workshop-native-code.md](workshop-native-code.md) | Steam Workshop and native DLLs: policy, precedents, risks |

Each document says what was observed statically, what was observed in the game, and what is still
open. Raw evidence (decompilations, logs, screenshots) is in [[monorepo archive: docs/evidence](https://github.com/SirCypkowskyy/hoi4-rename-anything/tree/monorepo-before-split-20261014/docs/evidence)](https://github.com/SirCypkowskyy/hoi4-rename-anything/blob/monorepo-before-split-20261014/docs/evidence/README.md).

The first research stage (prior art, script reconnaissance, two probe mods) and a review of an
unrelated globe-rendering mod are archived in Polish under
[[monorepo archive: docs/evidence/research](https://github.com/SirCypkowskyy/hoi4-rename-anything/tree/monorepo-before-split-20261014/docs/evidence/research)](https://github.com/SirCypkowskyy/hoi4-rename-anything/tree/monorepo-before-split-20261014/docs/evidence/research).
