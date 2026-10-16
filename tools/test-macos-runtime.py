"""Build the late runtime and verify inert/refused startup in owned foreign processes."""

# Only explicit local compilers and owned fixtures execute; no game is launched.
# ruff: noqa: S603
import json
import os
import subprocess
import tempfile
from pathlib import Path


def main() -> None:
    """Require safe startup and a private, bounded refusal report.

    Raises:
        SystemExit: If refusal or report permissions differ from the contract.
    """
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
    reports = output / "reports"
    reports.mkdir(mode=0o700)
    environment["CAPIE_RUNTIME_REPORT_DIR"] = str(reports)
    subprocess.run(
        [str(fixture), str(build / "capie_runtime.dylib"), "3"], env=environment, check=True
    )
    records = list(reports.glob("capie-runtime-*.jsonl"))
    if len(records) != 1 or records[0].stat().st_mode & 0o777 != 0o600:  # noqa: PLR2004 -- private output mode
        message = "expected one private runtime report"
        raise SystemExit(message)
    events = [json.loads(line) for line in records[0].read_text().splitlines()]
    if [event["stage"] for event in events] != ["runtime-loaded", "image-or-allocator-refused"]:
        message = "runtime refusal did not report the exact failed gate"
        raise SystemExit(message)
    environment.pop("CAPIE_EXPERIMENTAL_RUNTIME")
    environment["CAPIE_RUNTIME_MODE"] = "observe"
    subprocess.run(
        [str(fixture), str(build / "capie_runtime.dylib"), "3"], env=environment, check=True
    )
    print(
        "PASS: inert startup, private refusal report, observe-mode foreign refusal. "
        f"Artifacts: {output}"
    )


if __name__ == "__main__":
    main()
