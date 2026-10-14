# Country naming: investigation and native candidate, 2026-10-11

Target: `hoi4.exe` 1.19.3.0, SHA-256
`7DC947BE34970DA1E1C787BCDDBE7F62B610FF258AEBF8F06AA8B348F3A031D5`.
Original executable and `steam_api64.dll` are untouched on disk. Analysis reads the
original file or an already-existing exact-hash copy. Runtime changes are confined
to the already-running game process and test user data.

## Evidence and corrected assumptions

- **Observed static, new targeted Ghidra pass:** the root agent decompiled
  `CCountry_BuildNameKey` (RVA `0x6ECFF0`), `CCountry_UpdateNames` (`0x716580`),
  `PdxLoc_LookupKeyN` (`0x239EE20`), `PdxLoc_LookupKeyZ` (`0x239ED40`), and
  `PdxLoc_KeyExists` (`0x239D0F0`) from an exact-hash Ghidra project, read-only.
  Retained source output: [country-targeted-decompile.txt](evidence/session-20261011/country-targeted-decompile.txt).
- **Correction of the first Ghidra interpretation:** raw Capstone instructions show
  LookupKeyN/Z calculate FNV-1a and tail-jump to FindByHash (N at `0x239EE6A`, Z at
  `0x239ED88`); KeyExists calls FindByHash. Ghidra folded the tail-called body into
  its pseudocode, which initially led to an incorrect inline-search interpretation.
  A FindByHash hook could cover the lookups if it had a payload-to-hash registry.
  The implemented N/Z hooks instead access the original payload directly.
  [localization-abi-capstone.txt](evidence/session-20261011/localization-abi-capstone.txt)
- **Observed static, bounded Capstone pass:** the BuildNameKey lookup wrapper at
  `0x2244840` is `mov rcx,[rcx]; jmp 0x239D0F0`. Its eight bytes are
  `48 8B 09 E9 A8 88 15 00`. Therefore the KeyExists hook covers those checks.
- **Observed static:** UpdateNames passes the cosmetic tag at `country+0x1488`
  into BuildNameKey, obtains three keys, localizes them into the country's cached
  name/adjective/definite name, and marks map-name data dirty when the texts change.
- **Observed static, fresh focused Ghidra pass:** the country writer at `0x7191B0`
  calls `Writer_WriteString(writer,0x372F,country+0x1488,0)`; the reader at `0x705CE0`
  handles token `0x372F` by reading a string into `country+0x1488`. The setter at
  `0x711260` copies the full `std::string` size to that field and refreshes names;
  no country-tag length truncation appears in these three functions. Generic reader/
  writer limits are not established by this bounded pass. The reader does not directly
  call UpdateNames in the cosmetic-tag case; the exact later refresh caller is unknown.
  A subsequent independent full-restart/load test confirmed the final displayed name.
  [country-save-decompile.txt](evidence/session-20261011/country-save-decompile.txt),
  [read-only Ghidra run](evidence/session-20261011/country-save-ghidra.log).
- **Bounded static follow-up, exact postload caller remains unknown:** CCountry vtable slot8 points
  to `0x6FE1D0`, a postload candidate, which does not directly call UpdateNames.
  Separate routines at `0x6FF1F0` (when its mask bit0 is set) and `0x6EAB50`
  call UpdateNames. Their lifecycle roles were not established by this pass.
  [country-postload-candidates.txt](evidence/session-20261011/country-postload-candidates.txt)
  preserves those decompilations with candidate names rather than claiming execution.
- **REA attempt:** open_binary accepted the original file, provider Ghidra 12.1.4,
  and returned the exact target hash. The first batch_decompile request timed out
  after 300 seconds during default analysis. REA does not expose partial native
  results. This failure is not treated as completed static analysis; the separate
  targeted Ghidra output and bounded disassembly above provide the static evidence.
- **Frida attempt:** the guarded observational runner selected live PID25684 and
  verified the exact image hash at 00:31:56 Europe/Warsaw. Attach did not finish;
  the host was stopped at about00:35 to isolate UI input. No hook-ready lines or
  callback executions were observed. [country-passive.log](evidence/qa-20261011/country-passive.log)
  proves target selection, not live function semantics. No gameplay-state write
  was attempted by this probe.
- **Observed live, native initialization:** the bounded injector loaded the final
  DLL into fresh PID43732 in under one second and returned0. Initialization checks
  the EXE SHA-256, unique signatures at the three exact expected RVAs, and successful
  MinHook installation. Thus the three entrypoints are verified in the running
  build; UI semantics still require the independent game tests.
  [native-inject-43732.log](evidence/session-20261011/native-inject-43732.log)

## Implemented candidate

`plugin/src/country.cpp` hooks exactly three functions: LookupKeyN, LookupKeyZ,
and KeyExists. They recognize `RN1|<name>|<adjective>|` with only an empty suffix,
`_ADJ`, or `_DEF`. `_ADJ` yields the adjective; the other two yield the name.
BuildNameKey's static fallback checks cosmetic_tag plus the base suffix after
ideology-specific attempts fail, so claiming ideology-specific keys is unnecessary.
This preserves vanilla cache assignment and map
refresh. The plugin does not overwrite CCountry fields and needs neither custom
network commands nor custom save fields. The raw payload lives in cosmetic_tag,
which the scripted GUI sets through meta_effect.

The DLL validates the on-disk EXE hash and unique runtime signatures at exact RVAs
before enabling any hook. The injector separately validates the target image,
loads the DLL outside the game directory, initializes it outside DllMain, and
bounds each remote call to30seconds. Unsupported binaries or signature changes
fail closed. The corrected parser passes31 checks covering field splitting, strict
suffix selection, decision/focus-key regression cases, Polish UTF-8, length bounds,
malformed UTF-8, markup and invalid payloads. The unsupported-process guard returns10.

Build completed successfully using VS2022 x64 and the user-supplied MinHook source.
The first candidate DLL had SHA-256
`2AE7708696C62E8614AF748FBAC376E87FFCE721E95530186BBCEE69D55F2A38`
and is superseded by the corrected build below.
Build outputs are excluded from Git. [plugin/README.md](../plugin/README.md) describes
building, injection, removal, refusal codes and compatibility constraints.

## Native source review, 2026-10-11

Two reviews found initialization lifetime issues and the source was corrected:

- The build links the MSVC runtime with `/MT`; `DllMain` now leaves thread notifications
  enabled. The installed MSVC 14.44.35207 source at
  `crt/src/vcruntime/dll_dllmain.cpp:221-222` dispatches CRT thread attach/detach handling.
- MinHook's `src/hook.c`, `EnableAllHooksLL`, can enable earlier hooks before a later
  activation fails, then resume threads. Calling `MH_Uninitialize` at that point could
  free a trampoline used by an in-flight callback. The failure path now disables hooks
  best-effort but retains trampolines until process exit. Overrides require `active`,
  so partial initialization forwards to vanilla and cannot display the ACTIVE status.

PID43732 uses the earlier successfully initialized DLL, whose hash is recorded above.
The corrected build has SHA-256
`43012D6C0F1A94AE15723515A1AC718F4469812BAF2DBC462C8DE3BC40864CA5`
and initialized successfully in fresh PID32656 and PID42732. Partial
activation failure was reviewed from source; it has not been fault-injected into the game.

## Decision localization regression and cause, 2026-10-11

The user reported that the first loaded DLL displayed the custom country name but
also replaced every decision title/description with that name. This invalidates a
release claim for that DLL. The cause is the original parser's broad suffix acceptance,
not a pointer-wrapper ABI mismatch.

Fresh targeted Ghidra evidence identifies `0x72E360` as the decision localization
resolver. It reads `country+0x1488` (cosmetic tag), appends `_` and the decision key,
then checks KeyExists. The decision title caller at `0x72D770` passes the title key
from decision+`0x120` (or alternate+`0x148`). The description path at `0x72BD70`
appends `_desc` before calling the same resolver. Thus the initial hooks claimed
`RN1|Name|Adj|_<decision_id>` and `..._<decision_id>_desc`, selected those country
overrides, and returned Name. Category headings can remain normal because their
lookup path differs.

The root agent tightened the parser to exact empty/`_ADJ`/`_DEF` suffixes and added
regression checks. This makes country-specific decision probes fall through to
vanilla. Raw assembly and the Localize consumer confirm the lookup return is a raw
NUL-terminated C string from blob+signed offset, and KeyExists returns bool in AL;
no wrapper/shared-return-buffer hypothesis is needed. Independent QA applied
`qaąęłż` / `testowy` in fresh PID32656 and verified the original visible decision
titles plus the full Looming Peasants' Strike tooltip.
[Before](evidence/qa-20261011/10-fixed-decision-before.jpg),
[after](evidence/qa-20261011/13-fixed-decision-after.jpg). This covers the reproduced
regression; it does not exhaustively test every decision.

Primary static evidence:

- [decision-cosmetic-resolver-decompile.txt](evidence/session-20261011/decision-cosmetic-resolver-decompile.txt)
- [decision-title-caller-decompile.txt](evidence/session-20261011/decision-title-caller-decompile.txt)
- [decision-localization-helpers-decompile.txt](evidence/session-20261011/decision-localization-helpers-decompile.txt)
- [localization-consumers-decompile.txt](evidence/session-20261011/localization-consumers-decompile.txt)
- [localization-abi-capstone.txt](evidence/session-20261011/localization-abi-capstone.txt)

## Verification ledger (updated after live tests)

The independent Polish run loaded the user's unmodified `POL_1936_01_01_12.hoi4`
after a full game restart, with the corrected DLL already active. No new Apply
preceded the checks. The custom country name appeared on the map, in diplomacy
and in its tooltip. The mod panel retained the saved adjective. This proves the
observed persistence path while leaving the exact postload refresh caller unknown.
[Map](evidence/qa-20261011/16-pl-load-country-map.jpg),
[fields](evidence/qa-20261011/17-pl-load-country-fields.jpg),
[diplomacy/tooltip](evidence/qa-20261011/18-pl-load-diplomacy-tooltip.jpg).

| Question | Current evidence | Status |
|---|---|---|
| Exact three function signatures | 42/42 static signatures rescanned; native init0 in fresh corrected processes | static and runtime entrypoints confirmed |
| Custom country name via UI | independent QA Apply `qaąęłż`; country map and control decision titles/description | PASS for tested contexts |
| Adjective | UTF-8 parser; saved `testowy` visible in mod panel | native GetAdjective event context pending |
| Map/diplomacy/tooltips | independent restart/load screenshots 16–18 | PASS for tested contexts |
| Division/template/party names | vanilla uses literal names such as Dywizja Piechoty and Sanacja | unchanged by country adjective; documented scope limitation |
| Save -> restart -> load | unmodified user save, DLL loaded before load, no re-Apply | PASS for country name and stored adjective; other target cases separate |
| Polish letters | independent EN Apply plus PL restart/load of `qaąęłż` | PASS in observed country contexts; complete font coverage not claimed |
| New errors in error.log | independent bounded EN and PL deltas both 593 -> 593 | no new errors in those action ranges; complete final run pending |
| Two-client synchronization | normal scripted command architecture only; user has no second client | unverified; no MP claim |
| Startup/load without native DLL | vanilla localization fallback inferred | game test pending |
| Later ideology changes/puppet names | base fallback statically confirmed; arbitrary suffixes rejected | game test pending; puppet-specific names unsupported |

Successful compilation alone does not establish gameplay behavior or multiplayer
synchronization. The full target matrix and action boundaries are maintained in
[test-report.md](test-report.md).
