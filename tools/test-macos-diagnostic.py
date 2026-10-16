"""Build an opt-in diagnostic and verify refusals using only an owned foreign executable."""

# Only system tools and owned temporary fixtures are executed.
# ruff: noqa: S603
import json
import os
import subprocess
import tempfile
from pathlib import Path

REPORT_MODE = 0o600
REPORT_BOUND = 1024


def main() -> None:
    """Verify foreign-executable refusal, opt-in and private-output requirements.

    Raises:
        SystemExit: If the bounded diagnostic report or refusal behavior differs.
    """
    root = Path(__file__).resolve().parents[1]
    output = Path(tempfile.mkdtemp(prefix="capie-diagnostic-test-"))
    build = output / "build"
    subprocess.run(
        ["/bin/bash", str(root / "tools/build-macos-diagnostic.sh"), str(build)], check=True
    )
    child = output / "foreign"
    subprocess.run(
        [
            "/usr/bin/xcrun",
            "clang++",
            "-arch",
            "x86_64",
            str(root / "plugin/capie/tests/macos_startup/foreign.cpp"),
            "-o",
            str(child),
        ],
        check=True,
    )
    environment = {
        key: value
        for key, value in os.environ.items()
        if not key.startswith("DYLD_") and key != "CAPIE_DIAGNOSTIC_DIR"
    }
    environment["DYLD_INSERT_LIBRARIES"] = str(build / "capie_diagnostic.dylib")
    subprocess.run([str(child)], env=environment, check=True)
    private = output / "private"
    private.mkdir(mode=0o700)
    environment["CAPIE_DIAGNOSTIC_DIR"] = str(private)
    subprocess.run([str(child)], env=environment, check=True)
    reports = list(private.glob("capie-probe-*.json"))
    if len(reports) != 1:
        message = "Expected one private diagnostic report"
        raise SystemExit(message)
    report_text = reports[0].read_text()
    if str(build) in report_text or str(child) in report_text or str(root) in report_text:
        message = "Diagnostic report leaked an executable or library path"
        raise SystemExit(message)
    report = json.loads(report_text)
    if (
        report["status"] != "foreign_or_unreadable_executable"
        or report["exact_sha"]
        or report["loaded_identity"]
        or any(report["lookup_prefix_matches"])
        or report["token_expected"] != "not_checked_after_init"  # noqa: S105 - game registry token, no secret
        or any(item["read"] for item in report["token_reads"])
        or report["profile_checks"] != 0
        or report["profile_matches"] != 0
        or report["profile_first_failure"] != -1
    ):
        message = "Foreign process was not refused before pin/token reads"
        raise SystemExit(message)
    if (
        reports[0].stat().st_mode & 0o777 != REPORT_MODE
        or reports[0].stat().st_size >= REPORT_BOUND
    ):
        message = "Report permissions or bound differ"
        raise SystemExit(message)
    public = output / "public"
    public.mkdir(mode=0o755)
    public.chmod(0o755)
    environment["CAPIE_DIAGNOSTIC_DIR"] = str(public)
    subprocess.run([str(child)], env=environment, check=True)
    link = output / "link"
    link.symlink_to(private, target_is_directory=True)
    environment["CAPIE_DIAGNOSTIC_DIR"] = str(link)
    subprocess.run([str(child)], env=environment, check=True)
    if list(public.iterdir()) or len(list(private.iterdir())) != 1:
        message = "Unsafe output directory was not refused"
        raise SystemExit(message)
    print(
        "PASS: 4 owned-child launches; foreign identity refused, bounded0600report, "
        f"unsafe directories refused. Artifacts: {output}"
    )


if __name__ == "__main__":
    main()
