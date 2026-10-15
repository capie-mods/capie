# Inline debug controller — development candidate

`inline_debug_control.exe PID snapshot|dump|pause|resume` sends one scalar control
to an **already loaded** `rename_anything_inline_editor_debug_bridge.dll`. It cannot
start HOI4, load a remote DLL, initialize the bridge, choose another DLL/export,
send text, rename anything, inject input, retry, terminate or unload the target.
It is a developer diagnostic tool, not the end-user launcher.

Commands map to opcodes `1/2/3/4`. The only entry is
`DWORD WINAPI RNDebugRequest(void*)`; the pointer-sized argument carries the
integer opcode, with no remote allocation. Return `0` means **QUEUED**, not that a
snapshot/dump has completed. Inspect the bridge's native diagnostic output for
actual consumption. `41` means inactive, `42` invalid request and `43` serial exhausted.
The controller never retries a refusal or an uncertain request automatically.

Root's build must generate sibling include `inline_debug_manifest.hpp` containing:

```cpp
inline constexpr wchar_t rn_debug_dll_name[] =
    L"rename_anything_inline_editor_debug_bridge.dll";
inline constexpr wchar_t rn_debug_dll_sha256[] = L"<64 hexadecimal SHA256 characters>";
```

The basename is also enforced by a compile-time literal comparison. SHA256 must
identify the reviewed final debug DLL, not another variant. Place controller and
that DLL in the same output directory. No DLL path/function CLI options exist.
The offline `controller-review` build uses an all-zero placeholder digest and is
not a runtime package. Root must build against the accepted real hash later.

The controller pins the sibling DLL and selected target executable through
read-only `FILE_SHARE_READ` handles and hashes those same handles with BCrypt.
Only exact pinned `hoi4.exe` passes. It retains the captured process handle,
creation time and PID, and rechecks canonical final path/file identity and liveness.
Module enumeration must find exactly one expected basename at the same canonical
disk path and file identity as the pinned sibling DLL. Missing/wrong modules fail
closed; nothing is injected to make the check pass.

The local DLL is mapped with `LoadLibraryEx(DONT_RESOLVE_DLL_REFERENCES)` for PE and
export inspection only; imports/DllMain/RNInitialize are not executed. Both local
and remote PE headers must be AMD64 DLLs, with matching timestamp/SizeOfImage and
export-directory bounds. Bounded name/ordinal/function lookup must find one direct
`RNDebugRequest` RVA in each image. Forwarded/out-of-range exports are rejected.
GetProcAddress must agree locally; the first32 code bytes must match the remote
mapping via guarded `ReadProcessMemory`, and the destination must be an executable
MEM_IMAGE page from that module. Relocated/hooked first32 bytes may conservatively
reject a target; the controller does not patch, normalize or guess their meaning.

Exactly one `CreateRemoteThread` invokes that fixed validated export. OpenProcess
requests the documented CreateRemoteThread rights, including VM_OPERATION and
VM_WRITE, plus QUERY_INFORMATION for VirtualQueryEx and SYNCHRONIZE for its wait.
No remote memory-write/allocation API, remote LoadLibrary or arbitrary function
dispatch is used. The
normal wait is at most30 seconds. On timeout/wait/result-read uncertainty a message
is printed first, then the controller holds process/file pins quietly until its
own remote thread finishes **or** the captured target exits. It does not retry,
kill, or free anything remotely. Local mapping cleanup is local only. This guardian
can wait indefinitely for a hung request in a still-running game; externally
terminating the controller defeats its file-pin guarantee.

Use one bridge/editor hook variant in a fresh process. The controller does not
make co-loading variants safe, solve loader/world-lifetime races, or bypass the
bridge's own initialized/active checks. Root must separately review the bridge's
module pinning and request contract before runtime. PE/code comparison is a guard,
not proof of an atomic remote loader snapshot. The diagnostic export must remain
scalar/queued and must never execute GUI mutation in the remote request thread.

`--help`, invalid PID and invalid opcode branches return before file/process access.
Offline checks cover those branches only. No HOI4/controller runtime request has
been accepted. Proposed build: Windows x64 C++20 `/MT /W4 /WX`, link `bcrypt.lib`.
No build script, active native source, fixture, mod/config or accepted output is
changed by this source candidate. Report:
[monorepo archive: docs/evidence/session-20261011/day2-inline-debug-control-review.txt](https://github.com/SirCypkowskyy/hoi4-rename-anything/blob/monorepo-before-split-20261014/docs/evidence/session-20261011/day2-inline-debug-control-review.txt).
