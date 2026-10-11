#!/usr/bin/env python3
"""Require a docstring under every module-level constant and complete function docstrings.

Ruff has no rule that asks for documentation of module constants, so this
script fills that gap. It also closes a second gap: ruff's ``D417`` checks the
parameter names only when a docstring already has an ``Args:`` section, and
the pydoclint ``DOC201`` rule only looks at ``return`` statements. Here every
function or method (nested ones included) whose signature has parameters other
than ``self``/``cls`` must have an ``Args:`` section, and every function whose
return annotation is not ``None`` must have a ``Returns:`` or ``Yields:``
section. Together with ``D417`` and ``DOC*`` that makes every parameter and
return value documented. A constant is a module-level assignment (plain ``=`` or
annotated) whose target name is written in UPPER_CASE, optionally with one
leading underscore. The statement right after it must be a bare string
literal, the "attribute docstring" form from PEP 257 that Sphinx and most
editors pick up::

    TIMEOUT_SECONDS = 10
    \"\"\"How long to wait for the controller.\"\"\"

Only the top level of each module is inspected; assignments inside functions,
classes or ``if`` blocks are ignored, and so are subscript or attribute
targets such as ``TABLE[1] = ...``. For tuple targets every UPPER_CASE name in
the tuple counts as a constant.

With no arguments the script walks the repository (the parent of ``tools/``),
skips folders whose name starts with a dot and ``__pycache__``, and skips every
path listed under ``[tool.ruff] extend-exclude`` in ``pyproject.toml`` so it
checks the same files as ``ruff check``. File or folder arguments replace that
default set (the excludes still apply to folders, not to files named
explicitly).

Each missing docstring is printed as ``path:line: NAME has no docstring``, each
incomplete function docstring as ``path:line: name() docstring has no Args
section`` (or ``Returns``), followed by a summary line. The exit code is 1
when something is missing or a file cannot be parsed, and 0 otherwise.

Usage::

    uv run python tools/check_constant_docstrings.py [PATH ...]
"""

from __future__ import annotations

import argparse
import ast
import re
import sys
import tomllib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
"""Repository root (the parent of ``tools/``)."""

CONSTANT_NAME = re.compile(r"_?[A-Z][A-Z0-9_]*")
"""Pattern a target name must match in full to count as a constant."""

SKIPPED_DIR_NAMES = frozenset({"__pycache__"})
"""Folder names never searched, in addition to every folder starting with a dot."""

IMPLICIT_PARAMETERS = frozenset({"self", "cls"})
"""Parameters that never need an ``Args:`` entry."""

ARGS_SECTION = "Args:"
"""Google-style docstring section that documents parameters."""

RESULT_SECTIONS = ("Returns:", "Yields:")
"""Google-style docstring sections that document a return value."""


def ruff_excludes(root: Path) -> list[str]:
    """Read ``[tool.ruff] extend-exclude`` from the repository's ``pyproject.toml``.

    Args:
        root: Repository root.

    Returns:
        The exclude entries as POSIX-style relative paths without a trailing slash.
    """
    pyproject = root / "pyproject.toml"
    if not pyproject.is_file():
        return []
    config = tomllib.loads(pyproject.read_text(encoding="utf-8"))
    entries = config.get("tool", {}).get("ruff", {}).get("extend-exclude", [])
    return [str(entry).strip("/") for entry in entries]


def is_excluded(relative: str, excludes: list[str]) -> bool:
    """Return True when a relative path equals or lies inside an exclude entry.

    Args:
        relative: POSIX-style path relative to the repository root.
        excludes: Entries from :func:`ruff_excludes`.

    Returns:
        Whether the path is excluded.
    """
    return any(relative == entry or relative.startswith(entry + "/") for entry in excludes)


def python_files(paths: list[Path], root: Path, excludes: list[str]) -> list[Path]:
    """Expand the requested paths into a sorted list of Python files.

    Args:
        paths: Files or folders to check; an empty list means the whole repository.
        root: Repository root, used for exclude matching.
        excludes: Entries from :func:`ruff_excludes`.

    Returns:
        Python files to check, without duplicates.
    """
    found: set[Path] = set()
    for path in paths or [root]:
        if path.is_file():
            found.add(path.resolve())
            continue
        for candidate in path.rglob("*.py"):
            resolved = candidate.resolve()
            try:
                relative = resolved.relative_to(root)
            except ValueError:
                relative = candidate.relative_to(path)
            parts = relative.parts[:-1]
            if any(part.startswith(".") or part in SKIPPED_DIR_NAMES for part in parts):
                continue
            if is_excluded(relative.as_posix(), excludes):
                continue
            found.add(resolved)
    return sorted(found)


def constant_names(statement: ast.stmt) -> list[str]:
    """Return the UPPER_CASE names a module-level statement assigns.

    Args:
        statement: One top-level statement.

    Returns:
        Constant names bound by the statement, in source order; empty for
        anything that is not an assignment to plain names.
    """
    if isinstance(statement, ast.Assign):
        targets = statement.targets
    elif isinstance(statement, ast.AnnAssign) and statement.value is not None:
        targets = [statement.target]
    else:
        return []
    names: list[str] = []
    for target in targets:
        elements = target.elts if isinstance(target, ast.Tuple | ast.List) else [target]
        names.extend(
            element.id
            for element in elements
            if isinstance(element, ast.Name) and CONSTANT_NAME.fullmatch(element.id)
        )
    return names


def is_docstring(statement: ast.stmt | None) -> bool:
    """Return True when a statement is a bare string literal.

    Args:
        statement: The statement following an assignment, or None at the end of a module.

    Returns:
        Whether the statement can serve as an attribute docstring.
    """
    return (
        isinstance(statement, ast.Expr)
        and isinstance(statement.value, ast.Constant)
        and isinstance(statement.value.value, str)
    )


def missing_docstrings(tree: ast.Module) -> list[tuple[int, str]]:
    """Find module-level constants that are not followed by a docstring.

    Args:
        tree: Parsed module.

    Returns:
        (line number, constant name) pairs in source order.
    """
    missing: list[tuple[int, str]] = []
    body = tree.body
    for index, statement in enumerate(body):
        following = body[index + 1] if index + 1 < len(body) else None
        if is_docstring(following):
            continue
        missing.extend((statement.lineno, name) for name in constant_names(statement))
    return missing


def parameter_names(function: ast.FunctionDef | ast.AsyncFunctionDef) -> list[str]:
    """Return the parameters of a function that must be documented.

    Args:
        function: Function or method definition.

    Returns:
        Parameter names in signature order, without ``self`` and ``cls``.
    """
    arguments = function.args
    names = [arg.arg for arg in [*arguments.posonlyargs, *arguments.args, *arguments.kwonlyargs]]
    names += [arg.arg for arg in (arguments.vararg, arguments.kwarg) if arg is not None]
    return [name for name in names if name not in IMPLICIT_PARAMETERS]


def returns_value(function: ast.FunctionDef | ast.AsyncFunctionDef) -> bool:
    """Return whether a function's annotation promises a value.

    Args:
        function: Function or method definition.

    Returns:
        False for a missing annotation and for ``-> None``, True otherwise.
    """
    annotation = function.returns
    if annotation is None:
        return False
    return not (isinstance(annotation, ast.Constant) and annotation.value is None)


def incomplete_docstrings(tree: ast.Module) -> list[tuple[int, str, str]]:
    """Find functions whose docstring lacks an Args or Returns section.

    Args:
        tree: Parsed module.

    Returns:
        (line number, function name, missing section) triples in source order.
    """
    found: list[tuple[int, str, str]] = []
    functions = [
        node for node in ast.walk(tree) if isinstance(node, ast.FunctionDef | ast.AsyncFunctionDef)
    ]
    for function in sorted(functions, key=lambda node: node.lineno):
        docstring = ast.get_docstring(function) or ""
        if parameter_names(function) and ARGS_SECTION not in docstring:
            found.append((function.lineno, function.name, "Args"))
        if returns_value(function) and not any(s in docstring for s in RESULT_SECTIONS):
            found.append((function.lineno, function.name, "Returns"))
    return found


def display_path(path: Path, root: Path) -> str:
    """Return a path relative to the repository root when possible.

    Args:
        path: Absolute file path.
        root: Repository root.

    Returns:
        A POSIX-style relative path, or the path unchanged if it lies outside the root.
    """
    try:
        return path.relative_to(root).as_posix()
    except ValueError:
        return str(path)


def main() -> int:
    """Check the requested files and print every undocumented constant.

    Returns:
        0 when every constant is documented, 1 otherwise.
    """
    parser = argparse.ArgumentParser(
        description=(
            "Require a docstring under every module-level UPPER_CASE constant and "
            "Args/Returns sections in every function docstring."
        )
    )
    parser.add_argument("paths", nargs="*", type=Path, help="files or folders (default: repo)")
    args = parser.parse_args()
    excludes = ruff_excludes(ROOT)
    files = python_files(args.paths, ROOT, excludes)
    problems = 0
    for path in files:
        shown = display_path(path, ROOT)
        try:
            tree = ast.parse(path.read_text(encoding="utf-8"), filename=str(path))
        except (SyntaxError, UnicodeDecodeError) as exc:
            print(f"{shown}: cannot parse: {exc}")
            problems += 1
            continue
        for line, name in missing_docstrings(tree):
            print(f"{shown}:{line}: {name} has no docstring")
            problems += 1
        for line, name, section in incomplete_docstrings(tree):
            print(f"{shown}:{line}: {name}() docstring has no {section} section")
            problems += 1
    print(f"{len(files)} files checked, {problems} problems")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
