This is a separate QA transport prototype. Root reviewed and built it with
`/W4 /WX` for its own C++ sources; it has not been loaded or runtime-tested.
Activate it after the observer's native text read succeeds in a different process.
Never load observer and bridge dispatch-hook DLLs into the same process. Bridge
initialization rejects a loaded `rename_anything_editor_observer.dll`.

`build-bridge.ps1` outputs only
`plugin/build/editor-bridge/rename_anything_editor_bridge.dll` and
`inject_editor_bridge.exe`. The separate injector accepts that single fixed DLL
name and keeps the pinned-image and duplicate-module guards. Production country
sources, its build script and its DLL outputs are unchanged by this prototype.

The exact executable hash and three unique entry fingerprints guard initialization:
B867D0 (dispatch, 40 bytes), 22BAD40 (called editbox lookup, 53 bytes), and
1144840 (command constructor, 42 bytes). Only dispatch and constructor are hooked.
Failed initialization leaves transport disabled; explicit targets are disabled
after an enable failure. Restart before retry and never unload the DLL in-game.

Only model `rn_native_probe_gui` with selector `rn_native_probe_commit_click`
can enter transport. Every other dispatch and every unrelated constructor call
forwards unchanged, including the separate party-name probe button. The exact
own commit is dropped before original dispatch if native-field reading or bounded
record preparation fails. Input is copied once into a POD snapshot; no widget or
text pointer survives. Native UTF32 text is logged as escaped UTF8 and codepoints.

The strict `editor_input.hpp` converter accepts at most32 CHARS84 codepoints.
Successful transport supplies temporary `rn_native_ok=1`, `rn_native_len` and
all32 `rn_native_0`..`rn_native_31` indexes. Empty, unsupported or otherwise
invalid text supplies ok0,len0 and all32 zeros, with a logged converter error.
Every numeric field is a signed qword multiplied by100000, the engine's fixed
point representation. The input helper itself returns logical indexes and is
never passed as an engine string/vector.

The bridge validates at most256 original tempvar records, preserves all unrelated
records byte-for-byte, and removes only exact old transport keys before appending
the complete34-record result. Original keys remain borrowed during the one call.
A nested-safe TLS origin ties ctor args to the original context/selector/scope/
tempvector addresses. It replaces only the const source-view argument. Engine
constructor1144840 deep-copies all keys/values into its own native allocation;
the DLL destroys only POD staging storage after dispatch. Logs report whether
command record storage is independent. Nothing edits the original GUI vector,
the input buffer, rename target, save or native game files.

There is a deliberate cancellation boundary: preparation failures can drop the
own dispatch before entering the engine. A started native constructor cannot be
safely canceled, retried or replaced with nullptr; its original return is always
preserved. Unexpected ctor identities forward unchanged and an unexpected ctor
count is logged. This disposable commit effect only logs values. Before any
product Apply integration, its scripted effect must explicitly require transported
ok1, validate all lengths/indexes, and reconstruct the usual buffer before normal
guarded rename effects. Missing payload must never fall back to stale Apply input.
No product Apply, Enter callback, focus/prefill, native-party input or MP behavior
is implemented by this prototype.

The current probe's script log exposes length and slots0/31, not its ok marker or
all slots; root may extend that separate fixture while the game is closed for
runtime validation. Do not mistake compilation or static proofs for a passing
transport test. See `day2-native-command-contract.txt`, decompile and assembly in
`docs/evidence/session-20261011` for native ABI/ownership proof and its boundaries.
