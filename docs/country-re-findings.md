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
- **Correction to the inherited notes:** LookupKeyN and LookupKeyZ inline their own
  FNV-1a hash calculation and binary search. They do not call FindByHash. KeyExists
  calls FindByHash. A FindByHash-only hook cannot implement this country path.
- **Observed static, bounded Capstone pass:** the BuildNameKey lookup wrapper at
  `0x2244840` is `mov rcx,[rcx]; jmp 0x239D0F0`. Its eight bytes are
  `48 8B 09 E9 A8 88 15 00`. Therefore the KeyExists hook covers those checks.
- **Observed static:** UpdateNames passes the cosmetic tag at `country+0x1488`
  into BuildNameKey, obtains three keys, localizes them into the country's cached
  name/adjective/definite name, and marks map-name data dirty when the texts change.
- **Inherited static, not reverified in this pass:** the country writer serializes
  cosmetic_tag as a string and the reader restores it (RVA `0x7191B0` / `0x705CE0`).
  Live save/load testing is still required.
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
and KeyExists. They recognize `RN1|<name>|<adjective>|` followed by vanilla ASCII
identifier suffixes. Keys ending `_ADJ` yield the adjective; other accepted variants
yield the name, including `_DEF`. This preserves vanilla cache assignment and map
refresh. The plugin does not overwrite CCountry fields and needs neither custom
network commands nor custom save fields. The raw payload lives in cosmetic_tag,
which the scripted GUI sets through meta_effect.

The DLL validates the on-disk EXE hash and unique runtime signatures at exact RVAs
before enabling any hook. The injector separately validates the target image,
loads the DLL outside the game directory, initializes it outside DllMain, and
bounds each remote call to30seconds. Unsupported binaries or signature changes
fail closed. The parser passes18 checks covering field splitting, suffix selection,
Polish UTF-8, length bounds, malformed UTF-8, markup and invalid payloads.

Build completed successfully using VS2022 x64 and the user-supplied MinHook source.
The candidate DLL SHA-256 at00:49 was
`2AE7708696C62E8614AF748FBAC376E87FFCE721E95530186BBCEE69D55F2A38`.
Build outputs are excluded from Git. [plugin/README.md](../plugin/README.md) describes
building, injection, removal, refusal codes and compatibility constraints.

## Verification ledger (updated after live tests)

| Question | Current evidence | Status |
|---|---|---|
| Exact three function signatures | 40/40 static signatures rescanned; native init0 PID43732 | static and runtime entrypoints confirmed |
| Custom country name and adjective via UI | native and scripted implementations | runtime pending |
| Map/diplomacy/tooltips/division/government contexts | vanilla cache path statically observed | runtime pending |
| Save -> menu -> load | inherited cosmetic_tag serializer analysis | runtime pending |
| Polish letters | UTF-8 parser checks, earlier script probes | native game path pending |
| New errors in error.log | no native initialization yet | pending |
| Two-client synchronization | normal scripted command architecture only | unverified; no MP claim |
| Startup/load without native DLL | vanilla localization fallback inferred | game test pending |
| Later ideology changes/puppet names | suffix parser accepts ASCII identifiers | game test pending |

Remaining checks must be recorded as evidence; successful compilation does not
establish gameplay behavior or multiplayer synchronization.
