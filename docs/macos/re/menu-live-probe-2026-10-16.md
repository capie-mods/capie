# Read-only menu probe attempt, 2026-10-16

**Result: read-port access denied. No live pin or token validation occurred.**
The user left HOI4 at the menu and authorized read-only loader/port investigation.
The target PID was 52442. The separately observed image base was 0x100000000.

The standalone `tools/probe-macos-live.cpp` checked the process executable path,
process start timestamp before/after file work, and exact on-disk SHA-256:
`09419b087f8b4656c00df337bf838ff34c9b1d816b76cef5383ae6a4ac15b1fb`.
All passed. It prevalidated the generated TSV's 218 byte ranges against that file.
It then requested only a read task port, once:

```text
PASS pid_path_starttime_sha256 pid=52442 sha256=09419b087f8b4656c00df337bf838ff34c9b1d816b76cef5383ae6a4ac15b1fb
DENIED task_read_for_pid status=-1
```

The initial build did not capture errno, so this evidence does not claim a more
specific denial reason. The source was subsequently corrected to retain errno for
future explicit runs, but the access request was **not repeated**. Exit code was 3.

No loaded-header, byte-pin, vtable, or runtime token reads followed the refusal.
The probe never requests `task_for_pid`, attaches a debugger, suspends threads,
writes memory, injects a library, changes security settings or closes the game.
The available `vmmap` inspection does not establish permission to obtain a read
port for `mach_vm_read_overwrite`.

## Local SDK and tool checks

The installed SDK's `mach/mach_vm.h` declares `mach_vm_read_overwrite` with
`vm_map_read_t target_task`. `usr/lib/system/libsystem_kernel.tbd` exports
`_task_read_for_pid`; `sys/syscall.h` records syscall539. Public SDK headers do not
supply its declaration, so the standalone probe declares its three-argument ABI
explicitly. It intentionally has no broader task-port fallback.

The probe builds with C++20, `-Wall -Wextra -Werror`. Five negative argument/identity
tests passed and all reject before requesting any task port. The probe is bounded
to a one-GiB file, at most300 pin ranges, at most4096 bytes per read, and two fixed
runtime token DWORDs. It supports only the observed zero-slide image for this test;
other loaded layouts fail the header comparison. No player names or player data
are logged; only identity checks, hash, pin failures and the two token values would
be emitted after successful access.

A future authorized run needs a permitted read mechanism or a separately reviewed
in-process diagnostic. This failed attempt proves neither menu pin compatibility
nor runtime ABI correctness.
