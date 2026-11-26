# Port to HOI4 1.20.1.0

Port of CAPIE from HOI4 1.20.0.0 to 1.20.1.0 (CAPIE 1.7.2 → 1.7.3, branch `port-1.20.1`). Method and
tools as in [port-1.20.0.0.md](port-1.20.0.0.md) and `tools/port/README.md`.

- Old build: 1.20.0.0, `hoi4.exe` SHA-256 `1BEB2A38FE2D747E5B7ADB9B9648E350B0B851845DD228FFB87D2D2AC1B63A9F`.
- New build: 1.20.1.0 (launcher "Operation Postern v1.20.1.0.0fd1"), `hoi4.exe` SHA-256
  `B6EAC19F0EBF966A2BF2025C0FD1BAA187468E4078AD15BCFBB712A26F65E432`, the file the Steam install held on
  26 November 2026 (build time in the file: 25 November 2026, 12:46). A private copy is kept for the next port.
- Addresses are RVAs from image base `0x140000000`.

## Result

1.20.1.0 is a hotfix, and for CAPIE it is the same program: the two files have the same size, the same
section table and image size, and **`.text` is byte-identical**. Nothing moved, so no pin, constant,
offset, vtable slot or token changed. The port re-pins the build (hash, version, accepted table) and the
CAPIE release (1.7.3, build number 10703, no feature change).

## Byte-level picture

Of 56,556,664 bytes 1,334 differ, in 24 clusters:

| Where | What |
|---|---|
| PE header (`0x150`, `0x1A0`) | time stamp and checksum |
| `.rdata`, 15 clusters | the version strings (`v1.20.0.0.8873` → `v1.20.1.0.0fd1`), the build id (`8873ec772…` → `0fd124c53…`), the checksum string, the build date and time (17 Nov 14:21 → 25 Nov 12:46), one 4-byte value in the build info, the PDB signature (RSDS GUID) |
| after the last section (`0x35ED471` …) | the code signature (certificate and hash blocks) |

No difference lies in `.text`, `.pdata`, `.data` or `.reloc`, so no function, jump table, vtable or global
that CAPIE reads or hooks can differ. The only data CAPIE pins in `.rdata` (the strings of its signatures'
RIP targets: `politics_tab`, `parent_window_token`, the `Getting continent for %d` format, the console
message) is outside the 15 clusters.

## Method

1. `port.py locate --exe NEW`: 136 constants resolve, 0 problems; `locate --check` compares them with
   `game_layout.hpp` and finds no difference.
2. `port.py triage`: 62 unchanged, 10 moved (the console signatures, which the tool lists without an old
   RVA), 155 moved (short), 4 not found, none "bytes changed". The 4 are the same four as in 1.20.0.0
   (the three `rebuild.case_*` jump-table entries and `host.get_session`, which the tool cannot scan for
   by bytes); their bytes are in the identical `.text`. The short pins are never trusted on a global hit;
   with an identical `.text` they sit at their old addresses.
3. Whole-file byte comparison (above), which makes steps 1 and 2 a confirmation rather than the evidence.
4. `plugin/capie/build.ps1`: every offline test and every `*_exe_test` against the real 1.20.1.0 file,
   0 failures; the signature table equals the accepted table.

`tools/port/accepted-1.20.1.0.json` is the 1.20.0.0 table with the new executable hash (its pinned
SHA-256 is in `check_signature_table.py`); `map_1.20.0.0.json` is still the map of the sources, no new
map was needed.

## In-game test

On 1.20.1.0 (main menu "Operation Postern v1.20.1.0.0fd1"), single player, German Reich 1936, the
"CAPIE" switch mod and the Rename Anything dev mod, `capie_core.dll` and `capie_editor.dll` (debug)
injected by `capie_injector.exe`:

- the injector accepted the executable and both DLLs initialised;
- the Politics panel shows the editor pencils; the editor opened (Name, Flag, Colour tabs);
- a new country name appeared in the panel title and on the map label;
- a colour picked in the Colour tab changed the country's colour on the map.

Not tested in this run: the flag and portrait layers, a total conversion on 1.20.1.0, multiplayer.
