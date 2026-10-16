# Core signature-table coverage on macOS

`core-pins.json` accounts for **all 56 entries** of Windows `core/signature_table.hpp`:
3 country, 11 editor, 25 caption, 8 focus, 6 vtable slots, 2 runtime token DWORDs
and the focus-manager RIP pin. Addresses are RVAs unless the field explicitly says VA.
The exact target SHA-256 is stored in the manifest. This is static evidence, not an
activation-ready profile; no game execution or runtime mutation was performed.

## What was checked

Every code snippet was decoded directly from the current Mach-O with llvm-objdump,
then matched byte-for-byte against the file. Complete decoded instructions, file and
`__text` occurrence counts are retained. The 11 editor records reference the existing
`editor-pins.json`, whose extra constructor-body pin remains required.
All six vtable slot targets were read from the Mach-O and compared with independently
mapped RTTI address points and functions. Runtime pointer checks must apply ASLR.

Some small instruction sequences repeat. These are **fixed-RVA pins**, never independent
scanner patterns. Their manifest lists required unique function/vtable identity
companions. The constructor thunk also needs its exact branch target and body pin.
No entry establishes that overwriting a prologue or relocating it into a trampoline
is safe. Disassembler nearest-export labels are irrelevant and can be misleading;
the numeric addresses and instruction bytes are the evidence.

## Newly completed semantic links

The GUI frame function `0x1020ecf20` contains the loop `0x1020ed510–0x1020ed535`:
iterate indexes 0..22, load GUI+0xc8+8*i, test view+0x3c, call
`0x102389150(view+0x28)`. This proves the static dirty-consumer scheduling chain;
actual game timing is still untested.

Manager update `0x100b43310` reads its idler+0x18, invokes virtual+0xd8 for GUI and
uses map+0x70. `0x100b437aa–0x100b437f4` obtains GUI view 12, its root, and stores
that pointer into token 0x3ab2's map entry+0x18.

The model parser is CScriptedWindowTemplate virtual+0x28, `0x100b3df60`. Its switch
subtracts 0x3a6b and uses table `0x100b3f9bc`. Signed entry 3 at `0x100b3f9c8` targets
`0x100b3ecfa`; that block copies parser+0xc0 into model+0x24. The corresponding
registry initializer `0x1024d6c2f–0x1024d6c4a` associates the name
`parent_window_token` with token 0x3a6e. The dispatch snippet and table word are retained
as supplemental pins. No guessed model-parser offset remains.

## Runtime token limitation

`politics_tab` and `player_context` are initialized by startup code; the expected
DWORDs are 0x3ab2 and 0x3a70. The manifest includes their exact runtime addresses and
initializer evidence. These values must be read **after initialization**, through a
readable-virtual data check. They cannot be established by comparing raw file bytes
with the initialized value. An uninitialized or mismatched token must disable the
feature. `BytesPin` deliberately requires file-backed source bytes. The separate
`RuntimeU32Pin` / `after_init_values_match` API now checks readable virtual DWORDs,
including BSS, without confusing these values with file pins.

All 56 Windows roles now have static counterpart evidence. ABI adapters, complete
runtime profile assembly and startup ordering remain prerequisites; in-game feature
checks remain outstanding.


## Initialization order: Windows evidence and macOS requirement

Windows `src/dll/capie_editor.cpp::initialize` first checks the executable SHA and
text section, then editor signatures, then `caption_verify` (caption bytes, vtable
slots, **live token DWORDs**), then focus pins. Hook preparation/enabling occurs only
after those checks. `RNInitialize` allows one attempt; an initialization failure
cannot be retried in that process. There is no token-readiness wait inside it.
`DllMain` of the editor only stores its module handle.

There are two distinct Windows callers. `src/launcher/readiness.hpp` waits for a
responsive window, two fresh `Executing History from` log markers and a 15-second
settle period, then invokes the injector. `src/proxy/proxy_main.cpp` instead creates
an asynchronous worker; `src/proxy/loader.hpp::load_payload` explicitly has no game
readiness heuristic, and invokes core then editor initializers after validating both
modules. Its worker being outside the loader lock is **not** evidence that the game's
own token registry has finished initializing. Neither caller changes the editor's
fail-closed one-attempt token checks.

The macOS loader must therefore separate these stages:

1. Discover the actual loaded main image and slide, verify its exact file profile,
   and validate all required static code/target/slot pins without enabling features.
2. Reach a separately proven post-registry initialization checkpoint. A dylib
   constructor or an elapsed delay alone does not prove that checkpoint. Its concrete
   macOS mechanism is still unresolved; no loader hook is introduced by this helper.
3. Call `after_init_values_match` for the complete required token set. It performs
   full guarded four-byte reads from readable virtual ranges (BSS allowed), compares
   exact values and fails on empty sets, wrong values, unreadable ranges or overflow.
4. Revalidate remaining activation prerequisites before enabling anything. Any
   failure leaves the feature disabled; this helper never waits, retries or writes.

Matching these two token DWORDs proves their values only, not completion of all game
initialization, GUI readiness, nor permission to run feature callbacks off-thread.
