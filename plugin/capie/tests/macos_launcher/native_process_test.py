"""Build native launchers and execute only an owned fake dowser; never starts HOI4."""

# Every subprocess is a system compiler, the build script or an owned fixture.
# ruff: noqa: S603
import json
import os
import platform
import subprocess
import sys
import tempfile
from pathlib import Path

CONFIGURATION_ERROR = 64
PREPARATION_ERROR = 71

repo = Path(__file__).resolve().parents[4]
with tempfile.TemporaryDirectory(prefix="capie-native-parent-") as directory:
    base = Path(directory).resolve()
    output = base / "build"
    subprocess.run([str(repo / "tools/build-macos-launcher.sh"), str(output)], check=True)
    architectures = ["arm64", "x86_64"] if platform.machine() == "arm64" else ["x86_64"]
    for architecture in architectures:
        test = base / f"staging-{architecture}"
        subprocess.run(
            [
                "/usr/bin/clang++",
                "-std=c++20",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-fobjc-arc",
                "-framework",
                "Foundation",
                "-arch",
                architecture,
                str(repo / "plugin/capie/tests/macos_launcher/native_parent_test.mm"),
                "-o",
                str(test),
            ],
            check=True,
        )
        subprocess.run([str(test)], check=True)
    game = base / "fake game"
    game.mkdir()
    settings = {
        "gameId": "hoi4",
        "distPlatform": "steam",
        "rawVersion": "1.19.3.0",
        "gameDataPath": str(base / "userdata"),
        "exePath": "./hoi4.app/Contents/MacOS/hoi4",
        "exeArgs": ["space argument"],
    }
    source = game / "launcher-settings.json"
    source.write_text(json.dumps(settings))
    report = base / "report.json"
    dowser = game / "dowser"
    dowser.write_text(
        "#!" + sys.executable + "\nimport os,sys,json\n"
        "open(os.environ['CAPIE_PARENT_REPORT'],'x').write(json.dumps({"
        "'argv':sys.argv,'cwd':os.getcwd(),'steam':os.environ['SteamAppId']}))\n"
    )
    dowser.chmod(0o700)
    destination = base / "stage"
    args = [
        str(output / "capie-parent"),
        str(game),
        str(output / "capie-child"),
        "/owned/not-loaded.dylib",
        "a" * 64,
        str(destination),
    ]
    environment = dict(os.environ, CAPIE_PARENT_REPORT=str(report), SteamAppId="394360")
    subprocess.run(args, env=environment, check=True)
    observed = json.loads(report.read_text())
    expected = {
        "argv": [str(dowser), "--pdxlGameDir", str(destination), "--gameDir", str(destination)],
        "cwd": str(game),
        "steam": "394360",
    }
    if observed != expected:
        message = "Native parent did not preserve original dowser argv/cwd/Steam environment"
        raise RuntimeError(message)
    staged = json.loads((destination / "launcher-settings.json").read_text())
    if staged["exeArgs"][3] != str(base / "userdata/dlc_load.json"):
        message = "Native parent routed a different playset"
        raise RuntimeError(message)
    first = subprocess.run(args, env=environment, check=False)
    if first.returncode != PREPARATION_ERROR:
        message = "Existing staging was not rejected"
        raise RuntimeError(message)
    pair_stage = base / "pair-stage"
    pair_environment = dict(environment, CAPIE_PARENT_REPORT=str(base / "pair-report.json"))
    subprocess.run(
        [*args[:-1], str(pair_stage), "--capie-runtime", "/owned/runtime.dylib", "b" * 64],
        env=pair_environment,
        check=True,
    )
    pair_settings = json.loads((pair_stage / "launcher-settings.json").read_text())
    if pair_settings["exeArgs"][6:10] != [
        "--capie-runtime",
        "/owned/runtime.dylib",
        "b" * 64,
        "--",
    ]:
        message = "Native parent did not preserve paired runtime configuration"
        raise RuntimeError(message)
    for index, malformed in enumerate(
        [
            '{"gameId":"hoi4","gameId":"hoi4"}',
            json.dumps(dict(settings, alternativeExecutables=[{"exePath": "/unknown"}])),
            json.dumps(dict(settings, gameDataPath="relative/path")),
        ]
    ):
        source.write_text(malformed)
        target = base / f"rejected-{index}"
        result = subprocess.run([*args[:-1], str(target)], env=environment, check=False)
        if result.returncode != CONFIGURATION_ERROR or target.exists():
            message = "Invalid settings were not rejected before staging/exec"
            raise RuntimeError(message)
    print(
        "PASS: native parent legacy/paired exec + existing-stage rejection "
        "+ 3 invalid configurations"
    )
