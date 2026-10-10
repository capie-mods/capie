#!/usr/bin/env python3
"""Attach a guarded observational Frida probe; never spawn the game or write its data.

Frida instrumentation changes process instructions transiently. This script performs
no gameplay-state writes and does not modify the game installation on disk.
"""
import argparse
import ctypes
from ctypes import wintypes
import hashlib
import json
from pathlib import Path
import time

PIN = "7dc947be34970da1e1c787bcddbe7f62b610ff258aebf8f06aa8b348f3a031d5"


def process_image(pid):
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    kernel.OpenProcess.restype = wintypes.HANDLE
    kernel.QueryFullProcessImageNameW.argtypes = [wintypes.HANDLE, wintypes.DWORD, wintypes.LPWSTR, ctypes.POINTER(wintypes.DWORD)]
    kernel.QueryFullProcessImageNameW.restype = wintypes.BOOL
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    handle = kernel.OpenProcess(0x1000, False, pid)
    if not handle:
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        buf = ctypes.create_unicode_buffer(32768)
        count = wintypes.DWORD(len(buf))
        if not kernel.QueryFullProcessImageNameW(handle, 0, buf, ctypes.byref(count)):
            raise ctypes.WinError(ctypes.get_last_error())
        return Path(buf.value)
    finally:
        kernel.CloseHandle(handle)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--pid", required=True, type=int)
    parser.add_argument("--seconds", default=120, type=int)
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--script", type=Path, default=Path(__file__).with_name("verify.js"))
    args = parser.parse_args()
    image = process_image(args.pid)
    digest = hashlib.sha256(image.read_bytes()).hexdigest()
    if image.name.lower() != "hoi4.exe" or digest != PIN:
        raise SystemExit(f"Refused unsupported image: {image} sha256={digest}")
    import frida
    args.out.parent.mkdir(parents=True, exist_ok=True)
    with args.out.open("w", encoding="utf-8", buffering=1) as log:
        def emit(line):
            stamped = f"{time.strftime('%Y-%m-%dT%H:%M:%S%z')} {line}"
            log.write(stamped + "\n")
            print(stamped, flush=True)
        emit(f"image={image} sha256={digest} pid={args.pid}; observational instrumentation only")
        session = frida.attach(args.pid)
        script = None
        try:
            preamble = "const RN_VERIFIED_IMAGE = " + json.dumps(str(image)) + ";\n"
            script = session.create_script(preamble + args.script.read_text(encoding="utf-8-sig"))
            script.on("message", lambda message, data: emit(json.dumps(message, ensure_ascii=False)))
            script.load()
            deadline = time.monotonic() + args.seconds
            while time.monotonic() < deadline:
                time.sleep(min(0.25, max(0, deadline-time.monotonic())))
        except KeyboardInterrupt:
            emit("interrupted")
        finally:
            if script:
                try: script.unload()
                except frida.InvalidOperationError: pass
            try: session.detach()
            except frida.InvalidOperationError: pass
            emit("detached")


if __name__ == "__main__":
    main()
