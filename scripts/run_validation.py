#!/usr/bin/env python3
"""Run SkyrimBridge's offline validation suite and write an evidence receipt.

Portable mode runs validators that need only the repository, Python, and the
Release build. Full mode additionally runs validators against an operator-
supplied Skyrim mod corpus:

    python scripts/run_validation.py --mode portable
    python scripts/run_validation.py --mode full --mods-root "E:\path\to\mods"
"""

from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time


ROOT = Path(__file__).resolve().parents[1]
TESTS_DIR = ROOT / "tests"
MODS_ROOT_ENV = "SKYRIMBRIDGE_TEST_MODS_ROOT"
CORPUS_TESTS = frozenset(
    {
        "validate_bc45_codec.py",
        "validate_bc7_codec.py",
        "validate_bcn_codec.py",
        "validate_cmsd.py",
        "validate_collision_gen.py",
        "validate_foliage_mips.py",
        "validate_model_codec.py",
        "validate_png_codec.py",
        "validate_tree_wind.py",
    }
)


def sha256_text(value: str) -> str:
    return hashlib.sha256(value.encode("utf-8", errors="replace")).hexdigest()


def git_revision() -> str | None:
    result = subprocess.run(
        ["git", "rev-parse", "HEAD"],
        cwd=ROOT,
        text=True,
        capture_output=True,
    )
    return result.stdout.strip() if result.returncode == 0 else None


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--mode",
        choices=("portable", "full"),
        default="portable",
        help="validation scope (default: %(default)s)",
    )
    parser.add_argument(
        "--mods-root",
        type=Path,
        help=f"external mod corpus; sets {MODS_ROOT_ENV} for child tests",
    )
    parser.add_argument(
        "--receipt-dir",
        type=Path,
        default=ROOT / "build" / "validation",
        help="JSON receipt and raw log directory (default: %(default)s)",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    environment = os.environ.copy()
    mods_root = args.mods_root
    if mods_root is None and environment.get(MODS_ROOT_ENV):
        mods_root = Path(environment[MODS_ROOT_ENV])
    if args.mode == "full":
        if mods_root is None:
            raise SystemExit(
                f"error: full mode requires --mods-root or {MODS_ROOT_ENV}"
            )
        mods_root = mods_root.expanduser().resolve()
        if not mods_root.is_dir():
            raise SystemExit(f"error: mod corpus is not a directory: {mods_root}")
        environment[MODS_ROOT_ENV] = str(mods_root)

    tests = sorted(TESTS_DIR.glob("validate_*.py"), key=lambda path: path.name)
    if not tests:
        raise SystemExit(f"error: no validators found under {TESTS_DIR}")
    unknown_corpus = CORPUS_TESTS - {path.name for path in tests}
    if unknown_corpus:
        raise SystemExit(
            "error: corpus test inventory is stale: "
            + ", ".join(sorted(unknown_corpus))
        )

    args.receipt_dir.mkdir(parents=True, exist_ok=True)
    log_path = args.receipt_dir / f"validation-{args.mode}.log"
    receipt_path = args.receipt_dir / f"validation-{args.mode}.json"
    started = datetime.now(timezone.utc)
    suite_start = time.perf_counter()
    results: list[dict] = []
    log_parts: list[str] = []

    for test in tests:
        if args.mode == "portable" and test.name in CORPUS_TESTS:
            print(f"SKIP {test.name} (requires external mod corpus)")
            results.append(
                {
                    "test": test.name,
                    "scope": "corpus",
                    "status": "skipped",
                    "returncode": None,
                    "duration_seconds": 0.0,
                    "output_sha256": None,
                }
            )
            continue

        print(f"RUN  {test.name}")
        test_start = time.perf_counter()
        result = subprocess.run(
            [sys.executable, str(test)],
            cwd=ROOT,
            env=environment,
            text=True,
            capture_output=True,
            errors="replace",
        )
        duration = time.perf_counter() - test_start
        output = result.stdout + result.stderr
        if output:
            print(output, end="" if output.endswith("\n") else "\n")
        status = "passed" if result.returncode == 0 else "failed"
        if result.returncode == 77:
            status = "failed" if args.mode == "full" else "skipped"
        print(f"{status.upper():4} {test.name} ({duration:.2f}s)")
        log_parts.append(
            f"===== {test.name} [{status}, rc={result.returncode}] =====\n"
            f"{output}"
            + ("" if output.endswith("\n") or not output else "\n")
        )
        results.append(
            {
                "test": test.name,
                "scope": "corpus" if test.name in CORPUS_TESTS else "portable",
                "status": status,
                "returncode": result.returncode,
                "duration_seconds": round(duration, 6),
                "output_sha256": sha256_text(output),
            }
        )

    duration = time.perf_counter() - suite_start
    log_path.write_text("".join(log_parts), encoding="utf-8", newline="\n")
    passed = sum(item["status"] == "passed" for item in results)
    failed = sum(item["status"] == "failed" for item in results)
    skipped = sum(item["status"] == "skipped" for item in results)
    receipt = {
        "schema_version": 1,
        "suite": "SkyrimBridge offline validation",
        "mode": args.mode,
        "revision": git_revision(),
        "started_utc": started.isoformat(),
        "duration_seconds": round(duration, 6),
        "python": sys.version,
        "corpus": {
            "configured": mods_root is not None,
            "environment_variable": MODS_ROOT_ENV,
            "path_recorded": False,
        },
        "summary": {
            "total": len(results),
            "passed": passed,
            "failed": failed,
            "skipped": skipped,
        },
        "raw_log": log_path.name,
        "raw_log_sha256": hashlib.sha256(log_path.read_bytes()).hexdigest(),
        "results": results,
    }
    receipt_path.write_text(
        json.dumps(receipt, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
        newline="\n",
    )

    print(
        f"SUMMARY {passed} passed, {failed} failed, {skipped} skipped "
        f"({duration:.2f}s)"
    )
    print(f"RECEIPT {receipt_path}")
    print(f"LOG     {log_path}")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
