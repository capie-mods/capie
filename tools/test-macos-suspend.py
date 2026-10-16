"""Reproduce the owned-child IPC experiment; never accepts a target PID or game path."""

# Only fixed system tools and freshly compiled owned fixtures are executed.
# ruff: noqa: S603
import json
import subprocess
import tempfile
from pathlib import Path

REQUIRED_CYCLES = 100


def main() -> None:
    """Report both architectures and fail unless each completes all 100 cycles.

    Raises:
        SystemExit: A child failed or did not complete the experiment.
    """
    root = Path(__file__).resolve().parents[1]
    results = []
    with tempfile.TemporaryDirectory(prefix="capie-owned-suspend-") as directory:
        for architecture in ("arm64", "x86_64"):
            executable = Path(directory) / architecture
            subprocess.run(
                [
                    "/usr/bin/xcrun",
                    "clang++",
                    "-std=c++20",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-arch",
                    architecture,
                    str(root / "plugin/capie/tests/macos_suspend/probe.cpp"),
                    "-o",
                    str(executable),
                ],
                check=True,
            )
            result = subprocess.run(
                [str(executable)], capture_output=True, text=True, timeout=25, check=False
            )
            results.append(
                {
                    "architecture": architecture,
                    "returncode": result.returncode,
                    "report": json.loads(result.stdout) if result.stdout else None,
                }
            )
    print(json.dumps(results, indent=2))
    if any(
        item["returncode"] != 0
        or not isinstance(item["report"], dict)
        or item["report"].get("status") != "pass"
        or item["report"].get("cycles") != REQUIRED_CYCLES
        for item in results
    ):
        raise SystemExit(1)


if __name__ == "__main__":
    main()
