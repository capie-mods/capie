"""Probe protections of owned x86_64 fixture pages; never starts or patches the game."""

# Only system tools and owned temporary fixtures are executed.
# ruff: noqa: S603
import hashlib
import json
import platform
import shutil
import subprocess
import tempfile
from pathlib import Path


def main() -> None:
    """Record protection failures, signals, signatures and restoration outcomes.

    Raises:
        SystemExit: If the platform is not macOS or compilation/signing fails.
    """
    if platform.system() != "Darwin":
        message = "macOS required"
        raise SystemExit(message)
    root = Path(__file__).resolve().parents[1]
    output = Path(tempfile.mkdtemp(prefix="capie-owned-patch-"))
    original = output / "unsigned"
    print(f"Artifacts: {output}", flush=True)
    subprocess.run(
        [
            "/usr/bin/xcrun",
            "clang++",
            "-std=c++20",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-arch",
            "x86_64",
            str(root / "plugin/capie/tests/macos_patch/probe.cpp"),
            "-o",
            str(original),
        ],
        check=True,
    )
    initial = subprocess.run(
        ["/usr/bin/codesign", "-d", "--verbose=4", str(original)],
        capture_output=True,
        text=True,
        check=False,
    )
    if initial.returncode == 0:
        subprocess.run(["/usr/bin/codesign", "--remove-signature", str(original)], check=True)
    results = []
    signatures = {}
    for signing in ["unsigned", "adhoc", "hardened"]:
        binary = output / signing
        if signing != "unsigned":
            shutil.copy2(original, binary)
            options = ["--options", "runtime"] if signing == "hardened" else []
            subprocess.run(
                ["/usr/bin/codesign", "--force", "--sign", "-", *options, str(binary)], check=True
            )
        signature = subprocess.run(
            ["/usr/bin/codesign", "-d", "--verbose=4", str(binary)],
            capture_output=True,
            text=True,
            check=False,
        )
        signatures[signing] = {"exit": signature.returncode, "details": signature.stderr}
        file_digest = hashlib.sha256(binary.read_bytes()).hexdigest()
        for mapping in ["text", "anonymous"]:
            for method in ["mach-copy", "posix"]:
                run = subprocess.run(
                    [str(binary), mapping, method],
                    check=False,
                    capture_output=True,
                    text=True,
                    timeout=20,
                )
                result = {
                    "signing": signing,
                    "mapping": mapping,
                    "method": method,
                    "exit": run.returncode,
                    "stdout": run.stdout,
                    "stderr": run.stderr,
                    "disk_bytes_unchanged": hashlib.sha256(binary.read_bytes()).hexdigest()
                    == file_digest,
                }
                results.append(result)
                print(f"{signing} {mapping} {method}: exit={run.returncode}", flush=True)
    report = {
        "host": platform.platform(),
        "host_machine": platform.machine(),
        "signatures": signatures,
        "results": results,
    }
    (output / "results.json").write_text(json.dumps(report, indent=2) + "\n")
    print("Recorded 12 experiments; nonzero exits are measurements, not success.")


if __name__ == "__main__":
    main()
