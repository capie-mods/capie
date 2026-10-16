"""Build the late runtime and verify inert/refused startup in owned foreign processes."""

# Only explicit local compilers and owned fixtures execute; no game is launched.
# ruff: noqa: S603
import os
import subprocess
import tempfile
from pathlib import Path


def main() -> None:
    """Require safe runtime loading without a bootstrap and refusal of false identity."""
    root = Path(__file__).resolve().parents[1]
    output = Path(tempfile.mkdtemp(prefix="capie-runtime-refusal-"))
    build = output / "build"
    subprocess.run(
        ["/bin/bash", str(root / "tools/build-macos-runtime.sh"), str(build)], check=True
    )
    sources = root / "plugin/capie/tests/macos_runtime"
    fixture = output / "foreign"
    bootstrap = output / "false-bootstrap.dylib"
    subprocess.run(
        [
            "/usr/bin/xcrun",
            "clang++",
            "-std=c++20",
            "-arch",
            "x86_64",
            "-Wall",
            "-Wextra",
            "-Werror",
            str(sources / "refusal.cpp"),
            "-o",
            str(fixture),
        ],
        check=True,
    )
    subprocess.run(
        [
            "/usr/bin/xcrun",
            "clang",
            "-arch",
            "x86_64",
            "-dynamiclib",
            "-Wall",
            "-Wextra",
            "-Werror",
            str(sources / "false_bootstrap.c"),
            "-o",
            str(bootstrap),
        ],
        check=True,
    )
    environment = {
        key: value for key, value in os.environ.items() if not key.startswith(("DYLD_", "CAPIE_"))
    }
    subprocess.run(
        [str(fixture), str(build / "capie_runtime.dylib"), "0"], env=environment, check=True
    )
    environment["DYLD_INSERT_LIBRARIES"] = str(bootstrap)
    environment["CAPIE_EXPERIMENTAL_RUNTIME"] = "1"
    subprocess.run(
        [str(fixture), str(build / "capie_runtime.dylib"), "3"], env=environment, check=True
    )
    print(f"PASS: absent bootstrap inert; false identity refused. Artifacts: {output}")


if __name__ == "__main__":
    main()
