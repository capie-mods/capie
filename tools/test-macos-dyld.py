"""Test dyld insertion across an owned hardened parent; never starts HOI4 or dowser.

Only temporary fixture binaries are signed ad-hoc with hardened runtime. No system
configuration, game files, entitlements, or installed applications are changed.
Artifacts and JSON results remain in the printed temporary directory for inspection.
"""

# Only system tools and fixtures built in our private temporary directory are executed.
# ruff: noqa: S603

import json
import os
import platform
import shutil
import subprocess
import tempfile
from pathlib import Path


def main() -> None:
    """Build owned fixtures and require expected results at each process boundary.

    Raises:
        SystemExit: If the platform or observed dyld behavior differs from the fixture contract.
    """
    if platform.system() != "Darwin":
        message = "error: macOS is required"
        raise SystemExit(message)
    source = Path(__file__).resolve().parents[1] / "plugin/capie/tests/macos_dyld"
    output = Path(tempfile.mkdtemp(prefix="capie-dyld-chain-"))
    print(f"Artifacts: {output}", flush=True)
    architectures = ["arm64", "x86_64"] if platform.machine() == "arm64" else ["x86_64"]
    results = []
    for architecture in architectures:
        folder = output / architecture
        folder.mkdir()
        library = folder / "marker.dylib"
        parent = folder / "parent"
        child = folder / "child"
        compiler = [
            "/usr/bin/xcrun",
            "clang++",
            "-std=c++20",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-arch",
            architecture,
        ]
        subprocess.run(
            [*compiler, "-dynamiclib", str(source / "marker.cpp"), "-o", str(library)], check=True
        )
        subprocess.run([*compiler, str(source / "process.cpp"), "-o", str(parent)], check=True)
        shutil.copy2(parent, child)
        subprocess.run(
            ["/usr/bin/codesign", "--force", "--sign", "-", "--options", "runtime", str(parent)],
            check=True,
        )
        subprocess.run(["/usr/bin/codesign", "--verify", "--strict", str(parent)], check=True)
        signature = subprocess.run(
            ["/usr/bin/codesign", "-d", "--verbose=4", str(parent)],
            check=True,
            text=True,
            capture_output=True,
        ).stderr
        if "runtime" not in signature:
            message = "FAIL: parent does not report hardened runtime"
            raise SystemExit(message)
        environment = {
            key: value for key, value in os.environ.items() if not key.startswith("DYLD_")
        }
        environment["DYLD_INSERT_LIBRARIES"] = str(library)
        cases = [
            ("direct", [str(child)], "child loaded=1 env=1\n"),
            (
                "inherited",
                [str(parent), "inherited", str(child), str(library)],
                "parent loaded=0 env=0\nchild loaded=0 env=0\n",
            ),
            (
                "explicit",
                [str(parent), "explicit", str(child), str(library)],
                "parent loaded=0 env=0\nchild loaded=1 env=1\n",
            ),
        ]
        for label, command, expected in cases:
            result = subprocess.run(
                command, check=False, env=environment, text=True, capture_output=True, timeout=30
            )
            passed = result.returncode == 0 and result.stdout == expected and not result.stderr
            results.append(
                {
                    "architecture": architecture,
                    "case": label,
                    "passed": passed,
                    "exit": result.returncode,
                    "stdout": result.stdout,
                    "stderr": result.stderr,
                    "parent_signature": signature,
                }
            )
            (output / "results.json").write_text(json.dumps(results, indent=2) + "\n")
            print(
                f"{'PASS' if passed else 'FAIL'} {architecture} {label}: {result.stdout.strip()}",
                flush=True,
            )
            if not passed:
                message = f"unexpected dyld behavior; inspect {output / 'results.json'}"
                raise SystemExit(message)
    print(f"PASS: {len(results)} dyld chain scenarios; no game or launcher executed")


if __name__ == "__main__":
    main()
