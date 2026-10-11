#!/usr/bin/env python3
"""Attach an observational Frida script to an already running HOI4 process.

The script never starts the game and never writes game data or the game
installation. It only attaches to a process the user names with ``--pid``.

Before attaching it asks Windows for the full image path of that process
(``OpenProcess`` with ``PROCESS_QUERY_LIMITED_INFORMATION`` and
``QueryFullProcessImageNameW``), hashes the image file on disk and refuses to
continue unless the file is called ``hoi4.exe`` and its SHA-256 equals the
pinned build. Only then is ``frida`` imported and attached.

The JavaScript probe (``verify.js`` next to this file by default, or
``--script``) is loaded with a one-line preamble that defines
``RN_VERIFIED_IMAGE`` as the verified image path. Every message the probe
sends is serialized to JSON, prefixed with a local timestamp, appended to the
``--out`` log (line buffered, UTF-8) and echoed to stdout. The session runs for
``--seconds`` (default 120) or until Ctrl+C, then unloads the script, detaches
and logs ``detached``.

Frida instrumentation does change process instructions while attached, so
"observational" here means no gameplay-state writes by this script. Whether
the probe itself is read-only depends on the JavaScript that is loaded.

Usage::

    python plugin/tools/observe.py --pid 1234 --out run.log [--seconds 120] [--script verify.js]
"""

import argparse
import contextlib
import ctypes
import hashlib
import json
import time
from ctypes import wintypes
from pathlib import Path

PIN = "7dc947be34970da1e1c787bcddbe7f62b610ff258aebf8f06aa8b348f3a031d5"
"""SHA-256 (lower case) of the only ``hoi4.exe`` build this probe may attach to."""

GAME_IMAGE_NAME = "hoi4.exe"
"""File name (compared case-insensitively) the attached process must run from."""

DEFAULT_SECONDS = 120
"""Default session length in seconds (``--seconds``)."""

QUERY_NATIVE_PATH = 0
"""``dwFlags`` of ``QueryFullProcessImageNameW``: Win32 path format."""

PROCESS_QUERY_LIMITED_INFORMATION = 0x1000
"""Access right passed to ``OpenProcess``; enough to query the image name only."""

IMAGE_PATH_BUFFER_CHARS = 32768
"""Size, in UTF-16 characters, of the buffer for the process image path."""

POLL_SECONDS = 0.25
"""Longest sleep between deadline checks while the probe runs."""


def process_image(pid: int) -> Path:
    """Return the full image path of a running process.

    Args:
        pid: Process ID to query.

    Returns:
        Path of the executable the process was started from.

    Raises:
        WinError: If the process cannot be opened or queried (an ``OSError``).
    """
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    kernel.OpenProcess.restype = wintypes.HANDLE
    kernel.QueryFullProcessImageNameW.argtypes = [
        wintypes.HANDLE,
        wintypes.DWORD,
        wintypes.LPWSTR,
        ctypes.POINTER(wintypes.DWORD),
    ]
    kernel.QueryFullProcessImageNameW.restype = wintypes.BOOL
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    # Positional WinAPI call: bInheritHandle=False (FBT003 suppressed).
    handle = kernel.OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, False, pid)  # noqa: FBT003
    if not handle:
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        buf = ctypes.create_unicode_buffer(IMAGE_PATH_BUFFER_CHARS)
        count = wintypes.DWORD(len(buf))
        if not kernel.QueryFullProcessImageNameW(
            handle, QUERY_NATIVE_PATH, buf, ctypes.byref(count)
        ):
            raise ctypes.WinError(ctypes.get_last_error())
        return Path(buf.value)
    finally:
        kernel.CloseHandle(handle)


def main() -> None:
    """Verify the target image, attach Frida and log probe messages until the deadline.

    Raises:
        SystemExit: The process image is not the pinned ``hoi4.exe`` build.
    """
    parser = argparse.ArgumentParser()
    parser.add_argument("--pid", required=True, type=int)
    parser.add_argument("--seconds", default=DEFAULT_SECONDS, type=int)
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--script", type=Path, default=Path(__file__).with_name("verify.js"))
    args = parser.parse_args()
    image = process_image(args.pid)
    digest = hashlib.sha256(image.read_bytes()).hexdigest()
    if image.name.lower() != GAME_IMAGE_NAME or digest != PIN:
        msg = f"Refused unsupported image: {image} sha256={digest}"
        raise SystemExit(msg)
    # Imported only after the image check so a missing or broken Frida install
    # cannot matter for a refused target.
    import frida  # noqa: PLC0415

    args.out.parent.mkdir(parents=True, exist_ok=True)
    with args.out.open("w", encoding="utf-8", buffering=1) as log:

        def emit(line: str) -> None:
            """Write one timestamped line to the log file and to stdout.

            Args:
                line: Message without timestamp.
            """
            stamped = f"{time.strftime('%Y-%m-%dT%H:%M:%S%z')} {line}"
            log.write(stamped + "\n")
            print(stamped, flush=True)

        emit(f"image={image} sha256={digest} pid={args.pid}; observational instrumentation only")
        session = frida.attach(args.pid)
        script = None
        try:
            preamble = "const RN_VERIFIED_IMAGE = " + json.dumps(str(image)) + ";\n"
            script = session.create_script(preamble + args.script.read_text(encoding="utf-8-sig"))
            script.on(
                "message",
                lambda message, _data: emit(json.dumps(message, ensure_ascii=False)),
            )
            script.load()
            deadline = time.monotonic() + args.seconds
            while time.monotonic() < deadline:
                time.sleep(min(POLL_SECONDS, max(0, deadline - time.monotonic())))
        except KeyboardInterrupt:
            emit("interrupted")
        finally:
            if script:
                with contextlib.suppress(frida.InvalidOperationError):
                    script.unload()
            with contextlib.suppress(frida.InvalidOperationError):
                session.detach()
            emit("detached")


if __name__ == "__main__":
    main()
