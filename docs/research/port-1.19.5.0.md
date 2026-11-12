# Port to HOI4 1.19.5.0

Hotfix port of CAPIE from HOI4 1.19.4.2 to 1.19.5.0 (CAPIE 1.6.1 → 1.6.2, branch `hotfix-1.6.2`
from `main` at tag `v1.6.1`). Method and tools as in [port-1.19.4.2.md](port-1.19.4.2.md) and
`tools/port/README.md`; the old → new map by pin id is `tools/port/map_1.19.5.0.json`.

- Old build: 1.19.4.2, `hoi4.exe` SHA-256 `BCCC62909B6A1EC2C1D5A46EF18608E4D0C7CDC89AD341B7402A23C40F7A8037`,
  built 3 November 2026. A private copy was kept outside the repository, so this port could diff
  the old and the new file byte for byte (the 1.19.4.1 → 1.19.4.2 port could not).
- New build: 1.19.5.0 (launcher "Operation Postern v1.19.5.0.7c6c (c184)"), `hoi4.exe` SHA-256
  `709F916AFBEBCFE64A26678702691AEB40945D206074C2A80DE8E9D601FCE148`, built 5 November 2026,
  installed by Steam on 12 November. A private copy is kept for the next port.
- Addresses are RVAs from image base `0x140000000`.

## Result

Despite the minor version number, 1.19.5.0 is a rebuild of the 1.19.4.2 code. Nothing CAPIE
pins changed: no address, signature byte, instruction pin, RIP/rel32 target, structure offset,
vtable, vtable slot, token value or RTTI class name. The port only re-pins the build (hash,
version, accepted table) and the CAPIE release (1.6.2, build number 10602). Every layer stays on;
nothing was turned off.

## Byte diff of the two executables

Same image size (`0x38A3000`), entry point (`25A019C`) and section table (VA and virtual size of
`.text`, `.rdata`, `.data`, `.pdata`, `.rsrc`, `.reloc`). 3,646 bytes differ in the mapped
image, 6 of them in the PE header (timestamp `6AE9F874` → `6AEC984B`, checksum):

| Section | Bytes | What changed |
|---|---|---|
| `.text` | 416 | The linker swapped two neighbouring functions. Old `124B940` (0x67 bytes) and `124B9B0` (0x13C bytes) are now `124BA80` and `124B940`. Their instructions are the same; only the rel32 of two calls and one RIP displacement differ, and they decode to the same targets (`1257810`, `124B390`, the global `312AA68`). Their two callers changed only the rel32 of the call: `123D9EC` (in `123D970`) and `1256FF4` (in `1256F70`). |
| `.pdata` | 11 | The unwind entries of the two swapped functions. |
| `.rdata` | 281 | Version strings ("Operation Postern v1.19.5.0.7c6c"), build dates, the git commit hashes and dates of the game and its libraries, a CI token string, the debug directory timestamps (`2C21634`, `2C21650`, `2C2166C`), and the PDB GUID (`2E77BA4`). |
| `.data` | 2,932 | 397 runs of 6 to 8 characters: the per-file hash in the mangled names of anonymous-namespace types (`?A0x79e081f2@@` → `?A0x9be76c08@@`), which MSVC derives from the source path and build. |
| `.rsrc`, `.reloc` | 0 | |

None of the CAPIE items touches these bytes:

- None of the 179 pins of the sources (`port.py triage`, `port_scan.py`) lies in or near the
  changed code: the pattern, its hit list and its hit count are the same in both files. Triage's
  "moved", "short" and "not found" rows are the usual kinds of row, not changes: the console
  constants have no old RVA, short patterns have more than one global hit, and
  `host.get_session` / `rebuild.case_*` are a host constant and jump-table data.
- Every value of the map (331 addresses: signatures, instruction pins, RIP pin sites and
  targets, `rva::` constants, host and console constants), and every RVA-like literal in
  `plugin/capie/src`, `plugin/capie/tests` and `plugin/launcher` (352), was checked against the
  changed bytes, with a window of 0x80 bytes (0x400 for vtables, which covers every pinned slot):
  no overlap.
- `port.py locate --check` on the new file: all 115 anchors resolve and equal the header.
- RTTI: no CAPIE class name lies in an anonymous namespace (no `?A0x` in the sources), and
  `rtti_exe_test` passes on the new file. The vtables and their slots are in unchanged `.rdata`.
- Token values (`0x3AB2`, `0x3A70`, `0x3A6E`) and the defines, gamestate, focus manager, sprite
  database, map, idler and console globals are in unchanged `.rdata`/`.data`.

The two swapped functions (`124B940` and `124BA80`) and their callers are not CAPIE pins. Their
instructions and call targets are the same as before, so their behaviour did not change either.

## Steps

- `map_1.19.5.0.json`: the identity map, every value equal to its 1.19.4.2 value, keyed by the
  1.6.1 pin ids (`map_1.19.4.2.json` is keyed by the 1.6.0 ids; the 31 signatures renamed to
  their 1.19.4.2 address in 1.6.1 were rekeyed by RVA).
- `port.py --map map_1.19.5.0.json generate` on the new file: all 47 table entries equal
  `accepted-1.19.4.2.json` (RVA and bytes), every signature unique in `.text`, no DIR64
  relocation in any pattern or instruction pin. `apply` changed only line wrapping and dropped
  the trailing partial instruction of `colour.map_colors` / `colour.refresh`, so its output
  was not kept: the pattern text stays byte for byte, as in the 1.19.4.2 port.
- New accepted table `tools/port/accepted-1.19.5.0.json` (the 1.19.4.2 entries with the new
  hash), pinned in `check_signature_table.py` by the SHA-256 of the CRLF working-copy file
  (`797A4A72…`, `core.autocrlf=true`, as before).
- `game_build.hpp`: hash, `game_version` 1.19.5.0, `capie_release` 1.6.2,
  `capie_build_number` 10602; the launcher's `GameSha256` (`plugin/launcher/build.ps1`); the
  pinned-build strings of `capie_core` and the injector; the `CAPIE_BUILD` / `capie_ver`
  expectations of the tests; the changelog and the supported-build lines.

## Multiple builds

As before, the DLL accepts one `hoi4.exe`. CAPIE 1.6.2 supports 1.19.5.0 only and stays off on
1.19.4.2, although every pin would still match there.

## Open

- In-game test on 1.19.5.0: `docs/contributing/test-session-1.6.2.md`. The offline and
  `*_exe_test` checks pass on the new file.
- Release pipeline: reviewed binary hashes (`deploy/bundle-inputs.json`), installer pins
  (`tools/bootstrap`), Workshop texts and the GitHub release are written at release time and
  were not changed here.
