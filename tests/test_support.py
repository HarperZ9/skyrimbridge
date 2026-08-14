"""Shared environment contracts for SkyrimBridge validation scripts."""

from __future__ import annotations

import os
from pathlib import Path
import sys


MODS_ROOT_ENV = "SKYRIMBRIDGE_TEST_MODS_ROOT"


def require_mods_root() -> Path:
    """Return the explicitly configured external mod corpus.

    Exit code 77 is the conventional Automake "skipped test" status. The
    validation runner never treats this as a full-corpus pass: full mode
    requires and validates the path before launching any tests.
    """
    raw = os.environ.get(MODS_ROOT_ENV)
    if not raw:
        print(
            f"SKIP: set {MODS_ROOT_ENV} to a Skyrim mod-directory corpus",
            file=sys.stderr,
        )
        raise SystemExit(77)
    root = Path(raw).expanduser().resolve()
    if not root.is_dir():
        print(
            f"SKIP: {MODS_ROOT_ENV} is not a directory: {root}",
            file=sys.stderr,
        )
        raise SystemExit(77)
    return root


def require_fixture(relative: str) -> Path:
    fixture = require_mods_root() / Path(relative)
    if not fixture.is_file():
        print(f"SKIP: required corpus fixture not found: {fixture}", file=sys.stderr)
        raise SystemExit(77)
    return fixture
