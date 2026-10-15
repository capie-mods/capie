This directory contains a disposable QA observer, separate from the released
country localization DLL. It is not an input implementation or a Workshop file.

`editor_observer.cpp` builds to a separately named `rename_anything_editor_observer.dll`
with x64 MSVC, C++20, `/MT /EHsc /utf-8`, the existing MinHook sources and
`bcrypt.lib`, using `build-observer.ps1`. Its output directory is
`plugin/build/editor-observer`; the separate injector is `inject_editor_observer.exe`.
It exports the usual `RNInitialize(void*)` entry point. Do not change
`plugin/build.ps1`, overwrite either current country DLL/injector, rebuild a loaded
DLL, or unload this observer from a running game. Start a fresh process for QA.
The production injector accepts only the country DLL filename. The disposable
`inject_observer.cpp` compiles the same pinned-image and duplicate-module guards
with the single fixed observer filename. Root owns review, building and loading.

Initialization requires the exact executable SHA-256 from `image_guard.hpp`,
AMD64 PE headers, and unique `.text` fingerprints at B867D0 and 22BAD40. Only
B867D0 is detoured. Failed initialization leaves observation disabled; restart
before retry. MinHook enable failure explicitly disables that particular hook;
its trampoline and module remain resident to protect any in-flight callback.
The observer never uses `MH_ALL_HOOKS`, so it does not enable/disable country hooks.

It observes dispatches only when a validated context/template has the exact GUI
model name `rn_native_probe_gui`. It does not read the selector argument. For each
dispatch it freshly looks up `rn_native_edit_unique` in that context's validated
root container, checks the exact CEditBox/TextBuffer vtables, and copies at most
128 UTF32 codepoints from readable memory. Invalid/missing controls or oversized
text produce a rejection line and preserve the original dispatch. No widget
pointer survives the callback. No observer/Enter handler is attached.

`editor_observer.log`, created beside this DLL, contains the native UTF32 length,
capacity, UTF8 byte length, escaped UTF8 text and all codepoints. Controls,
quotes and backslashes are escaped to keep each snapshot on one line. This is
an observation only: no text, temporary variable, target, command, Apply effect,
save or native game file is edited. Both disposable probe buttons can log a
snapshot, then run their original scripted effect unchanged. The separate party
button therefore retains its explicit scripted party-name test behavior.

Use `probes/rn_native_input` only under root/QA control in a fresh process. Initial
`Zażółć QA` should produce length 9 and codepoints
`005A 0061 017C 00F3 0142 0107 0020 0051 0041`. Next verify that normal keyboard
editing changes a logged snapshot. Focus, Enter/save, target transport and a
final in-place user flow remain unimplemented and unverified by this observer.

Static proof and remaining boundaries:
[monorepo archive: docs/evidence/session-20261011/day2-native-editor-contract.txt](https://github.com/SirCypkowskyy/hoi4-rename-anything/blob/monorepo-before-split-20261014/docs/evidence/session-20261011/day2-native-editor-contract.txt) and
`day2-native-editor-decompile.txt`. No UI automation is included here.
