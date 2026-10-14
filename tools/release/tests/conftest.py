"""Shared fixtures: a throw-away signing key and the synthetic repository."""

from __future__ import annotations

from typing import TYPE_CHECKING

import pytest

from fakes import FakeRunner, Harness, KeyPair, make_key, make_repo

if TYPE_CHECKING:
    from pathlib import Path

KEY_SEED = 20261014
"""Seed of the session's test key."""


@pytest.fixture(scope="session")
def key() -> KeyPair:
    """A deterministic RSA key, generated once per session.

    Returns:
        The key.
    """
    return make_key(KEY_SEED)


@pytest.fixture
def harness(tmp_path: Path, key: KeyPair) -> Harness:
    """Synthetic repository with a signing key outside it.

    Args:
        tmp_path: pytest temporary folder.
        key: Session key.

    Returns:
        The harness.
    """
    root = tmp_path / "repo"
    config = make_repo(root, key)
    key_file = tmp_path / "keys" / "release.private.xml"
    key_file.parent.mkdir()
    key_file.write_text(key.xml(private=True), encoding="utf-8")
    return Harness(root=root, config=config, key_file=key_file, runner=FakeRunner(root, key))
