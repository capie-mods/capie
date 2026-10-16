"""Build and exercise the bootstrap only against fifteen owned fixture targets."""

# Fixed tools and owned temporary artifacts; never accepts a game path.
# ruff: noqa: S603
import hashlib
import json
import os
import struct
import subprocess
import tempfile
from pathlib import Path


def run(args: list[str], **kwargs: object) -> str:
    """Run a bounded owned build/test command.

    Returns:
        Captured standard output after successful completion.
    """
    return subprocess.run(
        args, check=True, capture_output=True, text=True, timeout=30, **kwargs
    ).stdout


def initializer_count(binary: Path) -> int:
    """Count actual Mach-O initializer entries.

    Returns:
        Number of pointer or modern offset initializer entries.
    """
    raw = binary.read_bytes()
    segment64 = 0x19
    offset, initializers = 32, 0
    for _ in range(struct.unpack_from("<I", raw, 16)[0]):
        command, size = struct.unpack_from("<II", raw, offset)
        if command == segment64:
            for section in range(struct.unpack_from("<I", raw, offset + 64)[0]):
                position = offset + 72 + section * 80
                name = raw[position : position + 16].rstrip(b"\0")
                width = {b"__mod_init_func": 8, b"__init_offsets": 4}.get(name)
                if width:
                    initializers += struct.unpack_from("<Q", raw, position + 40)[0] // width
        offset += size
    return initializers


def validate_binary(binary: Path) -> int:
    """Verify minimal bootstrap linkage and initialization.

    Returns:
        The verified initializer count.

    Raises:
        RuntimeError: Binary has unexpected dependencies or initializers.
    """
    initializers = initializer_count(binary)
    if initializers != 1:
        message = "Production bootstrap must have exactly one initializer"
        raise RuntimeError(message)
    if "libc++" in run(["/usr/bin/otool", "-L", str(binary)]):
        message = "Unexpected libc++ dependency"
        raise RuntimeError(message)
    return initializers


def main() -> None:
    """Check all stubs, ABI forwarding, publication and pre-mutation refusals.

    Raises:
        RuntimeError: A bootstrap binary violates its dependency or initializer contract.
    """
    source = Path(__file__).resolve().parent
    output = Path(tempfile.mkdtemp(prefix="capie-bootstrap-15-")).resolve()
    child, later, bootstrap = [
        output / name for name in ("child", "later.dylib", "bootstrap.dylib")
    ]
    compiler = [
        "/usr/bin/xcrun",
        "clang++",
        "-std=c++20",
        "-arch",
        "x86_64",
        "-O2",
        "-nostdlib++",
        "-fno-exceptions",
        "-fno-rtti",
        "-Wno-deprecated-declarations",
    ]
    run([*compiler, str(source / "child.cpp"), str(source / "targets.s"), "-o", str(child)])
    run(
        [
            "/usr/bin/xcrun",
            "clang",
            "-arch",
            "x86_64",
            "-dynamiclib",
            str(source / "later.c"),
            "-o",
            str(later),
        ]
    )
    listing = run(["/usr/bin/nm", "-g", str(child)])
    addresses = {
        line.split()[-1]: int(line.split()[0], 16)
        for line in listing.splitlines()
        if " _target" in line
    }
    prefixes = (
        "#include <stddef.h>\n#include <stdint.h>\n"
        "struct capie_macos_hook_prefix {const char* id;uint64_t rva;"
        "size_t length;unsigned char bytes[32];};\n"
        "static const capie_macos_hook_prefix capie_macos_hook_prefixes[]={\n"
    )
    for i in range(15):
        prefixes += (
            f'{{"target{i}",{addresses[f"_target{i}"] - 0x100000000:#x},14,{{'
            + ",".join(["0x90"] * 14)
            + "}},\n"
        )
    prefixes += "};\nstatic const size_t capie_macos_hook_prefix_count=15;\n"
    (output / "fixture_prefixes.h").write_text(prefixes)
    (output / "fixture_identity.h").write_text(
        'static constexpr char fixture_hash[]="'
        + hashlib.sha256(child.read_bytes()).hexdigest()
        + '";\n'
    )
    policy = "#define CAPIE_BOOTSTRAP_POLICY_READY 1\n"
    policy += "static constexpr char capie_bootstrap_expected_inserted[]="
    policy += json.dumps(f"{bootstrap}:{later}") + ";\n"
    policy += "static constexpr const char* capie_bootstrap_inserted_paths[]={"
    policy += json.dumps(str(bootstrap)) + "," + json.dumps(str(later)) + "};\n"
    policy += 'static constexpr const char* capie_bootstrap_inserted_hashes[]={"","'
    policy += hashlib.sha256(later.read_bytes()).hexdigest() + '"};\n'
    (output / "macos_bootstrap_policy.h").write_text(policy)
    run(
        [
            *compiler,
            "-dynamiclib",
            f"-I{output}",
            '-DCAPIE_BOOTSTRAP_FIXTURE_PREFIXES="fixture_prefixes.h"',
            str(source / "harness.cpp"),
            "-o",
            str(bootstrap),
        ]
    )
    env = {key: value for key, value in os.environ.items() if not key.startswith("DYLD_")}
    env["DYLD_INSERT_LIBRARIES"] = f"{bootstrap}:{later}"
    results = {"forward_arm_restore": run([str(child)], env=env)}
    for name, inserted in [
        ("wrong_order", f"{later}:{bootstrap}"),
        ("unknown_insert", f"{bootstrap}:{later}:/usr/lib/libz.1.dylib"),
    ]:
        negative = dict(env, DYLD_INSERT_LIBRARIES=inserted, EXPECT_REFUSAL="1")
        results[name] = run([str(child)], env=negative)
    results["unknown_override"] = run(
        [str(child)], env=dict(env, DYLD_FORCE_FLAT_NAMESPACE="1", EXPECT_REFUSAL="1")
    )
    # A mismatch in the final prefix must refuse before modifying any earlier target.
    (output / "fixture_prefixes.h").write_text(
        prefixes.rsplit("0x90", 1)[0] + "0xcc" + prefixes.rsplit("0x90", 1)[1]
    )
    run(
        [
            *compiler,
            "-dynamiclib",
            f"-I{output}",
            '-DCAPIE_BOOTSTRAP_FIXTURE_PREFIXES="fixture_prefixes.h"',
            str(source / "harness.cpp"),
            "-o",
            str(bootstrap),
        ]
    )
    results["last_prefix_mismatch"] = run([str(child)], env=dict(env, EXPECT_REFUSAL="1"))
    (output / "fixture_prefixes.h").write_text(prefixes)
    run(
        [
            *compiler,
            "-dynamiclib",
            f"-I{output}",
            '-DCAPIE_BOOTSTRAP_FIXTURE_PREFIXES="fixture_prefixes.h"',
            str(source / "harness.cpp"),
            "-o",
            str(bootstrap),
        ]
    )
    # Alter only owned fixture data. The executable still runs, its pinned SHA changes.
    with child.open("ab") as stream:
        stream.write(b"fixture hash mismatch")
    results["wrong_main_hash"] = run([str(child)], env=dict(env, EXPECT_REFUSAL="1"))
    dependencies = run(["/usr/bin/otool", "-L", str(bootstrap)])
    if "libc++" in dependencies:
        message = "Unexpected libc++ dependency"
        raise RuntimeError(message)
    # Compile the actual bootstrap (no fixture substitutions), check exactly one ctor.
    production = output / "production.dylib"
    run(
        [
            *compiler,
            "-dynamiclib",
            f"-I{output}",
            str(source / "../../src/bootstrap/macos_bootstrap.cpp"),
            "-o",
            str(production),
        ]
    )
    initializers = validate_binary(production)
    results["production_initializer_count"] = str(initializers)
    results["production_foreign_refusal"] = run(
        [str(child)], env=dict(env, DYLD_INSERT_LIBRARIES=str(production), EXPECT_REFUSAL="1")
    )
    print(
        json.dumps(
            {"output": str(output), "cases": results, "dependencies": dependencies}, indent=2
        )
    )


if __name__ == "__main__":
    main()
