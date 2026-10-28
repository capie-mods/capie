# Porting CAPIE to a new game build

CAPIE pins one `hoi4.exe` by SHA-256 and checks every address, byte pattern and structure offset
before it enables anything. After a game update it stays off until it is ported. This folder holds
the tools of the 1.19.3.0 → 1.19.4.0 port (`docs/research/port-1.19.4.0.md`) and the procedure
for the next one.

## Step 0: locate by anchors (seconds)

```powershell
uv run --no-project python -I tools/port/port.py locate --exe <new hoi4.exe>
uv run --no-project python -I tools/port/port.py locate --check   # self-check on the pinned build
```

`anchors.json` holds, for every RVA in `namespace rva` of `core/game_layout.hpp` (functions,
vtables, globals) and for a few helper functions, an anchor that survives a rebuild: an MSVC RTTI
class name (vtable, then a slot), a unique string literal the function references, or a
caller/callee relation to such an anchor. `locate.py` (pefile, capstone, numpy) evaluates them in
the given executable, prints `name  new-RVA` and writes `out/locate.json`. Function bounds come
from `.pdata` (chained unwind entries are followed; leaf functions end at the int3 padding).

- With `--check` each result is compared with the header; any `MISMATCH`, `FAIL` or `NO ANCHOR`
  exits 1. On 1.19.4.0 all 67 constants resolve and match (95 entries with the helpers). Run it
  after every change of `anchors.json` or `game_layout.hpp`; a new constant without an anchor
  fails the check.
- On a new build the printed RVAs are the first guess. `triage` (below) then checks the byte
  pins against them; a pin whose unique hit differs from the anchor's RVA is the one to read.
- A `FAIL` names the step that broke (`window_updater: 3 candidates [...]`). Fix the anchor, not
  the result: find the same function by its meaning (table below) and write the new relation.

Anchor format (one entry per name; `layout: false` marks a helper that is not in the header):

| Kind | Example | Result |
|---|---|---|
| `rtti` (+ `offset`) | `{"rtti": "CEditBox", "offset": 128}` | vtable of that (sub)object |
| `slot` + `vtable` | `{"slot": "0x50", "vtable": "vt_execute_effect"}` | function in that slot |
| `string` | `{"string": "window for %s is destroyed.\n"}` | functions referencing the literal |
| `callees` / `callers` / `refs` | `{"callees": "window_rebuild"}` | direct call targets / callers / users |
| `ctor` | `{"ctor": "vt_politics_view"}` | functions storing the vtable, minus its destructors |
| `near_string` / `near_call` | `{"near_string": "/reply ", "skip": 1}` | the call (or `"what": "rip"` operand) next to the site |
| `rip_in` | `{"rip_in": "country_of", "then_disp": "0x310"}` | RIP operand targets (globals) |
| `intersect`, `add` | | set intersection; `base` + offset |

`where` filters candidates (`string`, `refs`, `calls`, `jumps`, `calls_any`, `imm`, `disp`,
`getter`, `max_insns`, `not`), `nth` picks one, `check` must hold for the result and every
`confirm` expression must give the same RVA. A list result must have exactly one element.
Entries that use `disp`, `imm` or `then_disp` (focus switch, observer consume, view refresh,
game state global) depend on a structure offset: after a layout change they fail rather than
point elsewhere. `nth`/`skip` entries (build flag candidates, leader portrait write, view root)
depend on call order inside one function and are the first to check.

## Day 1: triage (minutes)

```powershell
uv run --no-project python -I tools/port/port.py triage
```

It scans every pin of the sources in the installed `hoi4.exe` and writes `out/triage.md`:

- **unchanged / moved**: a long pattern with exactly one hit. Safe.
- **moved (short, check context)**: a short instruction pin that matches somewhere. Never trust a
  global unique hit of a short pattern (two such hits were megabytes off in 1.19.4.0); place it
  inside its function with `shortpins.py`.
- **bytes changed**: the best approximate match differs in a few bytes. Disassemble old against
  new (`fuzzy.json` holds both). A different displacement means a **layout change**, not just
  moved code; the offset in `game_layout.hpp` changes too.
- **not found**: the function was rewritten; find it by meaning (below).

Post the triage result before promising players a date: a pure move takes an evening, a layout
change in the GUI or edit box takes a day.

## Analysis: find each item by meaning

Use the evidence type that survives a rebuild, in this order:

| Item | How it was found in 1.19.4.0 | Tool |
|---|---|---|
| vtables | MSVC RTTI class name → complete object locator → vtable | `rtti.py`, `rtti_all.py` |
| owner of a vtable / embedded object | the constructor that stores it (`lea rax,[rip+vt]; mov [reg+off],rax`) | `ripref.py`, `vtstores.py` |
| virtual methods | read the slot of the RTTI vtable | `vtscan.py` |
| functions that call each other | call pairs (`call A` … `call B`, or a call followed by a pinned instruction) | `callers.py`, `pair.py` |
| globals | the RIP target of a load in an anchored function | `ripref.py`, `rippins.py` |
| registration sites (tokens, keys) | the string literal they reference, never the code template | `ripref.py str:` |
| struct offsets | the displacement in the instruction the pin names | `dis.py at` |
| type of a widget map / list | the per-type helper it calls (clone/create function of a known class) | `callers.py`, `disp.py` |

Record every finding with its instruction address in `docs/research/port-<version>.md` while you
go, and the old → new mapping in `map_<version>.json`.

Lessons from 1.19.4.0:

- Patch notes matter: "reduced string allocations in GUI system" was the `FindX` rewrite (name
  maps, `{const char*, int32}` names). Read them first.
- Template code repeats: token registrations matched hundreds of identical sites; pin by literal.
- A changed object size moves fields of every object that embeds it (window `+0x80`, view `+0x80`,
  focus manager `-0x20`, session `-0x18`).
- New null-object returns (`CNullEditBox`) must be rejected by type, not by `nullptr`.
- Test fixtures encode sizes: windows, roots, edit boxes and the fake image grew.

## Generate, apply, verify

```powershell
uv run --no-project python -I tools/port/port.py generate   # patterns from the new exe (unique, no DIR64)
uv run --no-project python -I tools/port/port.py apply      # sources + test literals
uv run --no-project python -I tools/port/port.py verify     # signature table vs the accepted table
.\plugin\capie\build.ps1 -MinHookDir <minhook>             # all offline tests and *_exe_test on the real exe
.\plugin\capie\mutation_check.ps1 -MinHookDir <minhook>    # no survivor
```

Then by hand: `game_build.hpp` (hash, version, `capie_release`, `capie_build_number`), a new
`accepted-<version>.json` with its SHA-256 in `check_signature_table.py`, the CHANGELOG, the
user-facing "supported build" lines, and the in-game test (`rename-anything/tools/test-session.ps1`
and `capie_injector.exe` with the debug DLLs).

Write the new map as `map_<version>.json` and pass it with `port.py --map map_<version>.json
generate` / `apply` (the helpers read it from `CAPIE_PORT_MAP`; the default is
`map_1.19.4.0.json`). The helpers' Python packages are pinned in `requirements.txt`.

## Rules that keep the next port cheap

- Addresses live only in `game_layout.hpp`, the pin tables and the tests. Comments and log
  messages name functions (`RequestViewRefresh`, `FocusSwitch`), not RVAs.
  `tools/lint/check_comment_addresses.py` (run by `build.ps1`) fails on a hex address in a comment
  of the DLL sources unless the code pins that value, so a port that moves an address also flags
  every comment that still names the old one.
- A new pin comes with its meaning: the class, string, caller or slot that finds it again. Write
  it in the pin's comment, and add the same relation to `anchors.json` (`port.py locate
  --check` fails on an RVA constant without one).
- Keep the old executable. Ask Steam for the previous depot before an update overwrites it
  (`download_depot 394360 394361 <manifest>` in the Steam console), or copy `hoi4.exe` aside when
  a patch is announced. A byte diff of old and new halves the analysis.
