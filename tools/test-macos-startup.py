"""Observe startup thread snapshots in owned children and injected libraries only."""

# Only system tools and freshly built owned fixtures are executed.
# ruff: noqa: S603
import json
import os
import platform
import re
import subprocess
import tempfile
from pathlib import Path

MIN_THREADS_WITH_WORKER = 2


def main() -> None:
    """Record initializer/main snapshots, including a deliberately prior worker.

    Raises:
        SystemExit: If macOS is unavailable or a fixture fails to report valid observations.
    """
    if platform.system() != "Darwin":
        message = "macOS required"
        raise SystemExit(message)
    root = Path(__file__).resolve().parents[1]
    source = root / "plugin/capie/tests/macos_startup"
    output = Path(tempfile.mkdtemp(prefix="capie-owned-startup-"))
    print(f"Artifacts: {output}", flush=True)
    architectures = ["arm64", "x86_64"] if platform.machine() == "arm64" else ["x86_64"]
    results = []
    for architecture in architectures:
        folder = output / architecture
        folder.mkdir()
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
        worker = folder / "worker.dylib"
        child = folder / "child"
        subprocess.run(
            [*compiler, "-dynamiclib", str(source / "worker.cpp"), "-o", str(worker)], check=True
        )
        subprocess.run([*compiler, str(source / "child.cpp"), "-o", str(child)], check=True)
        for mode in ["clean", "prior-worker"]:
            marker = folder / f"{mode}.dylib"
            options = ["-DWITH_WORKER", str(worker)] if mode == "prior-worker" else []
            subprocess.run(
                [*compiler, "-dynamiclib", str(source / "marker.cpp"), *options, "-o", str(marker)],
                check=True,
            )
            environment = {
                key: value for key, value in os.environ.items() if not key.startswith("DYLD_")
            }
            environment["DYLD_INSERT_LIBRARIES"] = str(marker)
            for iteration in range(3):
                run = subprocess.run(
                    [str(child)],
                    env=environment,
                    capture_output=True,
                    text=True,
                    timeout=20,
                    check=False,
                )
                observations = re.findall(
                    r"phase=(initializer|main) threads=(\d+) kr=0", run.stdout
                )
                if run.returncode != 0 or [phase for phase, _ in observations] != [
                    "initializer",
                    "main",
                ]:
                    message = (
                        f"Fixture failed: {architecture}/{mode}: {run.returncode} "
                        f"{run.stdout} {run.stderr}"
                    )
                    raise SystemExit(message)
                if mode == "prior-worker" and any(
                    int(count) < MIN_THREADS_WITH_WORKER for _, count in observations
                ):
                    message = "Prior-worker fixture did not observe its required worker"
                    raise SystemExit(message)
                results.append(
                    {
                        "architecture": architecture,
                        "mode": mode,
                        "iteration": iteration,
                        "exit": run.returncode,
                        "stdout": run.stdout,
                        "stderr": run.stderr,
                    }
                )
                print(f"{architecture} {mode} #{iteration + 1}: {observations}", flush=True)
    (output / "results.json").write_text(
        json.dumps({"host": platform.platform(), "results": results}, indent=2) + "\n"
    )
    print("Snapshots recorded; count=1 is not a quiescence guarantee.")


if __name__ == "__main__":
    main()
