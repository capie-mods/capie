"""Build a private developer bundle with an inert default bootstrap policy."""

# Fixed compiler paths and explicitly selected local source/artifacts only.
# ruff: noqa: S603, PLR2004  # fixed Mach-O ABI field sizes and tags
from __future__ import annotations

import argparse
import hashlib
import json
import struct
import subprocess
from pathlib import Path

from release.releasekit.versions import capie_problems, read_capie_versions

TARGET_SHA = "09419b087f8b4656c00df337bf838ff34c9b1d816b76cef5383ae6a4ac15b1fb"
"""Exact supported native HOI4 executable identity."""


def digest(path: Path) -> str:
    """Hash a local artifact in bounded chunks.

    Returns:
        Lowercase SHA-256 hex digest.
    """
    result = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            result.update(block)
    return result.hexdigest()


def canonical(path: Path, *, exists: bool = True) -> Path:
    """Resolve an absolute path suitable for a colon-separated loader list.

    Returns:
        Canonical absolute path.

    Raises:
        ValueError: If the path has a delimiter, is relative or is not a regular input.
    """
    if not path.is_absolute() or any(c in str(path) for c in "\0\r\n:"):
        message = "expected an absolute path without loader delimiters"
        raise ValueError(message)
    result = path.resolve(strict=exists)
    if any(c in str(result) for c in "\0\r\n:"):
        message = "resolved path contains a loader delimiter"
        raise ValueError(message)
    if exists and not result.is_file():
        message = "expected a regular input file"
        raise ValueError(message)
    return result


def source_fingerprint(root: Path) -> str:
    """Fingerprint sorted relative paths and contents of all native source/build inputs.

    Returns:
        SHA-256 of an unambiguous JSON path/content-digest list.
    """
    paths = [
        p
        for p in (root / "plugin/capie/src").rglob("*")
        if p.is_file() and p.suffix in {".h", ".hpp", ".cpp", ".mm"}
    ]
    paths.extend(
        root / "tools" / name
        for name in ("build-macos-runtime.sh", "build-macos-launcher.sh", "build-macos-bundle.py")
    )
    entries = [(str(p.relative_to(root)), digest(p)) for p in paths]
    payload = json.dumps(sorted(entries), ensure_ascii=True, separators=(",", ":"))
    return hashlib.sha256(payload.encode("utf-8")).hexdigest()


def verify_bootstrap_bytes(data: bytes, *, ready: bool = True) -> None:
    """Check the built thin dylib dependency list and initializer count.

    Raises:
        ValueError: If bounded Mach-O structural checks reject the bootstrap.
    """  # noqa: DOC502 -- nested require raises ValueError

    def require(condition: bool) -> None:  # noqa: FBT001 -- assertion predicate
        if not condition:
            message = "bootstrap must be x86_64 dylib, libSystem-only, with one initializer"
            raise ValueError(message)

    require(len(data) >= 32)
    magic, cpu, _, kind, count, size, _, _ = struct.unpack_from("<8I", data)
    require((magic, cpu, kind) == (0xFEEDFACF, 0x1000007, 6))
    require(size <= len(data) - 32 and count <= size // 8)
    end, offset, initializers = 32 + size, 32, 0
    libraries = []
    for _ in range(count):
        require(offset + 8 <= end)
        command, length = struct.unpack_from("<II", data, offset)
        require(length >= 8 and length % 8 == 0 and length <= end - offset)
        block = data[offset : offset + length]
        if command in {12, 0x80000018, 0x8000001F, 0x20, 0x80000023}:
            require(length >= 24)
            name = struct.unpack_from("<I", block, 8)[0]
            require(24 <= name < length and b"\0" in block[name:])
            libraries.append(block[name:].split(b"\0", 1)[0])
        if command == 25:
            require(length >= 72)
            sections = struct.unpack_from("<I", block, 64)[0]
            require(sections <= (length - 72) // 80 and length == 72 + sections * 80)
            for index in range(sections):
                section = 72 + index * 80
                flags = struct.unpack_from("<I", block, section + 64)[0]
                if flags & 0xFF == 9:
                    section_size = struct.unpack_from("<Q", block, section + 40)[0]
                    require(section_size % 8 == 0)
                    initializers += section_size // 8
        offset += length
    require(
        offset == end
        and libraries == [b"/usr/lib/libSystem.B.dylib"]
        and initializers == int(ready)
    )


def policy(
    paths: list[Path], hashes: list[str], *, ready: bool, dependencies: list[dict] | None = None
) -> str:
    """Render immutable ordered policy data.

    Returns:
        C++ header text; paths must already have been validated.

    Raises:
        ValueError: If inputs are incomplete or ambiguous.
    """
    if not 2 <= len(paths) <= 8 or len(paths) != len(hashes) or len(set(paths)) != len(paths):
        message = "expected two to eight distinct ordered libraries with matching hashes"
        raise ValueError(message)
    if not ready:
        return "#define CAPIE_BOOTSTRAP_POLICY_READY 0\n"
    joined = ":".join(map(str, paths))
    values = ", ".join(json.dumps(str(p), ensure_ascii=False) for p in paths)
    digests = ", ".join(json.dumps(h) for h in hashes)
    dependencies = dependencies or []
    dependency_paths = ", ".join(json.dumps(d["path"], ensure_ascii=False) for d in dependencies)
    dependency_hashes = ", ".join(json.dumps(d["sha256"]) for d in dependencies)
    quoted = json.dumps(joined, ensure_ascii=False)
    return (
        f"#define CAPIE_BOOTSTRAP_POLICY_READY {int(ready)}\n"
        f"static constexpr char capie_bootstrap_expected_inserted[] = {quoted};\n"
        f"static constexpr const char* capie_bootstrap_inserted_paths[] = {{{values}}};\n"
        f"static constexpr const char* capie_bootstrap_inserted_hashes[] = {{{digests}}};\n"
        f"#define CAPIE_BOOTSTRAP_DEPENDENCY_COUNT {len(dependencies)}\n"
        "static constexpr const char* capie_bootstrap_dependency_paths[] = "
        f"{{{dependency_paths}}};\n"
        "static constexpr const char* capie_bootstrap_dependency_hashes[] = "
        f"{{{dependency_hashes}}};\n"
    )


def reviewed(evidence: Path | None, root: Path, external: list[Path]) -> tuple[bool, list[dict]]:
    """Require a matching explicit lifecycle audit before compiling an enabled profile.

    Returns:
        False without evidence; True for an exact reviewed source/dependency profile.

    Raises:
        ValueError: If evidence does not authorize the current profile.
    """
    if evidence is None:
        return False, []
    record = json.loads(canonical(evidence).read_text(encoding="utf-8"))
    required = {
        "target_sha256": TARGET_SHA,
        "lifecycle_ready": True,
        "bootstrap_source_sha256": digest(root / "plugin/capie/src/bootstrap/macos_bootstrap.cpp"),
        "runtime_source_sha256": digest(root / "plugin/capie/src/dll/macos_runtime.cpp"),
        "source_closure_sha256": source_fingerprint(root),
        "external_libraries": [{"path": str(p), "sha256": digest(p)} for p in external],
    }
    if any(record.get(key) != value for key, value in required.items()):
        message = "lifecycle evidence does not authorize this exact source/library profile"
        raise ValueError(message)
    dependencies = record.get("dependencies", [])
    required_names = {"libsteam_api.dylib", "libPDXSDK.dylib", "libtbb.dylib"}
    if (
        len(dependencies) != len(required_names)
        or {Path(d["path"]).name for d in dependencies} != required_names
    ):
        message = "lifecycle evidence must pin the three bundled dependencies"
        raise ValueError(message)
    for dependency in dependencies:
        path = canonical(Path(dependency["path"]))
        if str(path) != dependency["path"] or digest(path) != dependency["sha256"]:
            message = "bundled dependency identity differs from lifecycle evidence"
            raise ValueError(message)
    return True, dependencies


def main() -> None:
    """Compile an owned bundle without launching or changing the installed game."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("--inserted-library", type=Path, action="append", default=[])
    parser.add_argument("--lifecycle-evidence", type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    output = canonical(args.output, exists=False)
    external = [canonical(path) for path in args.inserted_library]
    ready, dependencies = reviewed(args.lifecycle_evidence, root, external)
    versions = read_capie_versions(
        (root / "plugin/capie/src/core/game_build.hpp").read_text(encoding="utf-8")
    )
    if capie_problems(versions):
        parser.error("inconsistent shared CAPIE release version")
    if len(set(external)) != len(external) or len(external) > 6:
        parser.error("at most six distinct additional libraries are permitted")
    output.mkdir(mode=0o700)
    subprocess.run(
        ["/bin/bash", str(root / "tools/build-macos-runtime.sh"), str(output / "runtime")],
        check=True,
    )
    subprocess.run(
        ["/bin/bash", str(root / "tools/build-macos-launcher.sh"), str(output / "launcher")],
        check=True,
    )
    generated = output / "policy"
    generated.mkdir(mode=0o700)
    bootstrap = output / "capie_bootstrap.dylib"
    runtime = output / "runtime/capie_runtime.dylib"
    paths = [bootstrap, *external, runtime]
    hashes = ["", *(digest(p) for p in external), digest(runtime)]
    (generated / "macos_bootstrap_policy.h").write_text(
        policy(paths, hashes, ready=ready, dependencies=dependencies), encoding="utf-8"
    )
    subprocess.run(
        [
            "/usr/bin/xcrun",
            "clang++",
            "-std=c++20",
            "-arch",
            "x86_64",
            "-O2",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-dynamiclib",
            "-nostdlib++",
            "-fno-exceptions",
            "-fno-rtti",
            "-Wno-deprecated-declarations",
            "-I",
            str(generated),
            str(root / "plugin/capie/src/bootstrap/macos_bootstrap.cpp"),
            "-o",
            str(bootstrap),
        ],
        check=True,
    )
    verify_bootstrap_bytes(bootstrap.read_bytes(), ready=ready)
    files = [bootstrap, runtime, output / "launcher/capie-parent", output / "launcher/capie-child"]
    manifest = {
        "kind": "developer-bundle",
        "version": versions.release,
        "target_api_level": versions.api_level,
        "target_sha256": TARGET_SHA,
        "bootstrap_policy_ready": ready,
        "runtime_ready": False,
        "fixed_install_location": str(output),
        "inserted_order": list(map(str, paths)),
        "files": {
            str(p.relative_to(output)): {"sha256": digest(p), "size": p.stat().st_size}
            for p in files
        },
    }
    (output / "developer-bundle.json").write_text(
        json.dumps(manifest, indent=2) + "\n", encoding="utf-8"
    )
    print(f"Built {output}; bootstrap_policy_ready={ready}; runtime_ready=false")


if __name__ == "__main__":
    main()
