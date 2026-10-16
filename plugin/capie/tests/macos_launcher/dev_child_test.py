"""Offline process checks: only a compiled recorder is exec'd, never HOI4."""

import json
import os
import subprocess
import tempfile
from pathlib import Path

configuration_error = 64

root = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix="capie-dev-child-") as tmp:
    p = Path(tmp)
    child = p / "child"
    subprocess.run(  # noqa: S603 -- only system compilers and this test's own fixtures
        [
            "/usr/bin/clang++",
            "-std=c++20",
            "-arch",
            "x86_64",
            "-Wno-deprecated-declarations",
            str(root / "src/launcher/macos_child_main.cpp"),
            "-o",
            str(child),
        ],
        check=True,
    )
    source = p / "record.c"
    source.write_text(r"""
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
int main(int argc,char**argv){
 char wd[4096];
 if(argc!=(getenv("CAPIE_RUNTIME_GAME_ARG")?7:5) || strcmp(argv[1],"--pdxfake") ||
    strcmp(argv[2],"nonsensitive") ||
    strcmp(argv[3],"space argument") || strcmp(argv[4],"--fixture"))return 10;
 if(getenv("CAPIE_RUNTIME_GAME_ARG") && (strcmp(argv[5],"--capie-runtime") ||
    strcmp(argv[6],"game-only")))return 14;
 if(!getcwd(wd,sizeof(wd)) || strcmp(wd,getenv("CAPIE_EXPECT_CWD")))return 11;
 const char* wanted=getenv("CAPIE_EXPECT_INSERTION");
 const char* actual=getenv("DYLD_INSERT_LIBRARIES");
 if(wanted){
  if(!actual || strcmp(wanted,actual) || !getenv("CAPIE_FOREIGN_LOADED"))return 13;
 }else if(actual)return 12;
 return 0;
}
""")
    record = p / "record"
    subprocess.run(  # noqa: S603 -- only system compilers and this test's own fixtures
        ["/usr/bin/clang", "-arch", "x86_64", str(source), "-o", str(record)], check=True
    )
    cwd = p / "game cwd"
    cwd.mkdir()
    playset = p / "dlc_load.json"
    env = dict(os.environ, CAPIE_EXPECT_CWD=str(cwd.resolve()))
    env.pop("DYLD_INSERT_LIBRARIES", None)
    env.pop("STEAM_DYLD_INSERT_LIBRARIES", None)
    cases = [
        None,
        "bad json",
        json.dumps({"enabled_mods": []}),
        json.dumps({"enabled_mods": ["mod/capie.mod"]}),
        "x" * (1024 * 1024 + 1),
    ]
    for content in cases:
        if content is not None:
            playset.write_text(content)
        subprocess.run(  # noqa: S603 -- only system compilers and this test's own fixtures
            [
                str(child),
                "--pdxfake",
                "nonsensitive",
                "--capie-dev-child",
                str(record),
                str(cwd),
                str(playset),
                str(p / "absent.dylib"),
                "0" * 64,
                "--",
                "space argument",
                "--fixture",
            ],
            env=env,
            check=True,
        )
    for args in [
        [],
        ["--capie-dev-child"],
        ["--capie-dev-child", "--capie-dev-child"],
        ["--capie-dev-child", str(record), str(cwd), str(playset), str(p / "x"), "0" * 64, "bad"],
    ]:
        result = subprocess.run(  # noqa: S603 -- this test's own compiled fixture
            [str(child), *args], env=env, capture_output=True, check=False
        )
        if result.returncode != configuration_error:
            message = (
                f"Malformed marker returned {result.returncode}, expected {configuration_error}"
            )
            raise RuntimeError(message)

    # A disabled CAPIE must not be reintroduced through an inherited alias.
    # These ordinary files are never loaded: a successful gate removes them first.
    own = p / "own.dylib"
    own.write_bytes(b"not a dylib; the disabled gate must remove this identity")
    symlink = p / "alias.dylib"
    symlink.symlink_to(own)
    hardlink = cwd / "hardlink.dylib"
    os.link(own, hardlink)
    playset.write_text(json.dumps({"enabled_mods": []}))
    for alias in [str(symlink), str(hardlink), "hardlink.dylib"]:
        alias_env = dict(env, STEAM_DYLD_INSERT_LIBRARIES=alias)
        subprocess.run(  # noqa: S603 -- only this test's own fixtures
            [
                str(child),
                "--pdxfake",
                "nonsensitive",
                "--capie-dev-child",
                str(record),
                str(cwd),
                str(playset),
                str(own),
                "0" * 64,
                "--",
                "space argument",
                "--fixture",
            ],
            env=alias_env,
            check=True,
            capture_output=True,
        )
    foreign_source = p / "foreign.c"
    foreign_source.write_text(
        "#include <stdlib.h>\n"
        "__attribute__((constructor)) static void init(void) {"
        'setenv("CAPIE_FOREIGN_LOADED", "1", 1);}\n'
    )
    foreign = cwd / "own.dylib"  # Same basename, different file identity must survive.
    subprocess.run(  # noqa: S603 -- only the system compiler and own fixture
        [
            "/usr/bin/clang",
            "-arch",
            "x86_64",
            "-dynamiclib",
            str(foreign_source),
            "-o",
            str(foreign),
        ],
        check=True,
    )
    foreign_env = dict(
        env,
        STEAM_DYLD_INSERT_LIBRARIES=f"{symlink}:{foreign}:{hardlink}",
        CAPIE_EXPECT_INSERTION=str(foreign),
    )
    foreign_env.pop("CAPIE_FOREIGN_LOADED", None)
    subprocess.run(  # noqa: S603 -- only this test's own fixtures
        [
            str(child),
            "--pdxfake",
            "nonsensitive",
            "--capie-dev-child",
            str(record),
            str(cwd),
            str(playset),
            str(own),
            "0" * 64,
            "--",
            "space argument",
            "--fixture",
        ],
        env=foreign_env,
        check=True,
        capture_output=True,
    )
    runtime = p / "runtime.dylib"
    runtime.write_bytes(b"runtime must not be loaded in disabled fixture")
    runtime_alias = cwd / "runtime-alias.dylib"
    os.link(runtime, runtime_alias)
    pair_env = dict(
        env, STEAM_DYLD_INSERT_LIBRARIES=f"{symlink}:{runtime_alias}", CAPIE_RUNTIME_GAME_ARG="1"
    )
    pair_args = [
        str(child),
        "--pdxfake",
        "nonsensitive",
        "--capie-dev-child",
        str(record),
        str(cwd),
        str(playset),
        str(own),
        "0" * 64,
        "--capie-runtime",
        str(runtime),
        "0" * 64,
        "--",
        "space argument",
        "--fixture",
        "--capie-runtime",
        "game-only",
    ]
    for enabled_pair in (False, True):
        playset.write_text(json.dumps({"enabled_mods": ["mod/capie.mod"] if enabled_pair else []}))
        subprocess.run(pair_args, env=pair_env, check=True, capture_output=True)  # noqa: S603
    malformed_pair = [*pair_args[:11], "--"]
    result = subprocess.run(malformed_pair, env=pair_env, check=False, capture_output=True)  # noqa: S603
    if result.returncode != configuration_error:
        message = "Missing runtime digest did not reject configuration"
        raise RuntimeError(message)
    print(
        "PASS: missing/invalid/disabled/wrong-build/oversize use clean child; "
        "prepended/appended argv and cwd preserved; malformed markers rejected; "
        "three own aliases removed; distinct foreign library loaded; "
        "paired aliases and game args preserved"
    )
