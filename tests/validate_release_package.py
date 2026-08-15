#!/usr/bin/env python3
"""Validate the public SkyrimBridge release archive.

This is an integration test: build both Release DLL targets before running it.
The test invokes the packager twice in independent output directories and
requires byte-for-byte identical archives, an exact public manifest, verified
payload hashes, and matching SHA-256 sidecars.
"""

from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import zipfile


ROOT = Path(__file__).resolve().parents[1]
VERSION = "3.0.0"
ARCHIVE_NAME = f"SkyrimBridge-{VERSION}.zip"

EXPECTED_PAYLOAD = {
    "CHANGELOG.md",
    "GPU.md",
    "LICENSE",
    "README.md",
    "SPEC-ENGINE-EXPOSURE.md",
    "THIRD_PARTY_NOTICES.md",
    "USER-GUIDE.md",
    "VALIDATION-PROTOCOL.md",
    "parameters.md",
}
EXPECTED_PAYLOAD = {f"Docs/{name}" for name in EXPECTED_PAYLOAD} | {
    "Optional-GPU-Proxy/READ-ME.txt",
    "Optional-GPU-Proxy/d3d11.dll",
    "SKSE/Plugins/SkyrimBridge.dll",
    "SKSE/Plugins/SkyrimBridge/GPU.ini",
    "SKSE/Plugins/SkyrimBridge/Sky.ini",
    "SKSE/Plugins/SkyrimBridge/SkyrimBridge.ini",
    "SKSE/Plugins/SkyrimBridge/WeatherParams.ini",
    "SKSE/Plugins/SkyrimBridge/WeatherRouting.example.ini",
    "SKSE/Plugins/SkyrimBridge/WriteBackConfig.ini",
    "Shaders/SkyrimBridge.fxh",
    "Shaders/SkyrimBridge_CB.fxh",
    "Shaders/enbUI_SkyrimBridge.fxh",
    "Tools/SkyrimBridgeClient.h",
    "Tools/blender/skyrimbridge_push.py",
    "Tools/sb_command_client.py",
    "Tools/sb_smoke_tour.py",
}
EXPECTED_ARCHIVE = EXPECTED_PAYLOAD | {"MANIFEST.json"}


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def run_packager(output_dir: Path) -> Path:
    result = subprocess.run(
        [
            sys.executable,
            str(ROOT / "scripts" / "package.py"),
            "--dist-dir",
            str(output_dir),
            "--quiet",
        ],
        cwd=ROOT,
        text=True,
        capture_output=True,
    )
    if result.returncode:
        raise AssertionError(
            "packager failed:\n"
            f"stdout:\n{result.stdout}\n"
            f"stderr:\n{result.stderr}"
        )
    archive = output_dir / ARCHIVE_NAME
    assert archive.is_file(), f"missing archive: {archive}"
    return archive


def main() -> int:
    required_binaries = (
        ROOT / "build" / "Release" / "SkyrimBridge.dll",
        ROOT / "build" / "Release" / "d3d11.dll",
    )
    missing = [str(path) for path in required_binaries if not path.is_file()]
    assert not missing, "build Release targets first; missing: " + ", ".join(missing)

    with tempfile.TemporaryDirectory(prefix="skyrimbridge-package-test-") as temp:
        temp_root = Path(temp)
        archive_a = run_packager(temp_root / "a")
        archive_b = run_packager(temp_root / "b")
        bytes_a = archive_a.read_bytes()
        bytes_b = archive_b.read_bytes()

        assert bytes_a == bytes_b, "identical inputs produced different ZIP bytes"

        digest = sha256(bytes_a)
        for archive in (archive_a, archive_b):
            sidecar = archive.with_suffix(archive.suffix + ".sha256")
            expected_sidecar = f"{digest}  {archive.name}\n"
            assert sidecar.read_text(encoding="ascii") == expected_sidecar

        with zipfile.ZipFile(archive_a) as release_zip:
            names = release_zip.namelist()
            assert names == sorted(EXPECTED_ARCHIVE), (
                "archive manifest mismatch:\n"
                f"expected={sorted(EXPECTED_ARCHIVE)!r}\nactual={names!r}"
            )
            assert len(names) == len(set(names)), "archive contains duplicate paths"
            assert all(not name.startswith("/") for name in names)
            assert all("\\" not in name and ".." not in Path(name).parts for name in names)

            manifest = json.loads(release_zip.read("MANIFEST.json"))
            assert manifest["schema_version"] == 1
            assert manifest["package"] == "SkyrimBridge"
            assert manifest["version"] == VERSION
            entries = manifest["files"]
            assert [entry["path"] for entry in entries] == sorted(EXPECTED_PAYLOAD)

            for entry in entries:
                payload = release_zip.read(entry["path"])
                assert entry["size"] == len(payload), entry["path"]
                assert entry["sha256"] == sha256(payload), entry["path"]

            for info in release_zip.infolist():
                assert info.date_time == (2000, 1, 1, 0, 0, 0), info.filename

            check_no_native_replacements(release_zip)

    print(
        "PASS: deterministic public release package "
        f"({len(EXPECTED_PAYLOAD)} payload files + manifest)"
    )
    return 0


# Markers unique to the Kitsuune-derived native replacement suite. Each is a
# class name or a config filename that only that suite emits, so a hit means a
# build with SKYRIMBRIDGE_NATIVE_REPLACEMENTS=ON reached the archive.
#
# "ENBWorldspaceWeatherlists" is deliberately NOT on this list: that string is
# CompatDetect's detection record for Kitsuune's own plugin, and it is present
# in both configurations by design. The distributable build still has to
# recognise their plugins in order to defer to them.
NATIVE_SUITE_MARKERS = (
    b"SkyLighting",
    b"EnbLightInventoryFix",
    b"KreateProfile",
    b"KreateRecords",
    b"EditorIDCache",
    b"LoadKreateProfile",
    b"WeatherRouting.ini",
)


def check_no_native_replacements(release_zip: zipfile.ZipFile) -> None:
    """Report whether the packaged binary carries the permission-gated suite.

    Distributing it needs Kitsuune's permission, which has not been granted;
    see CREDITS.md. A runtime toggle is not sufficient, because an inert copy
    in the binary is still a copy, so this inspects the shipped bytes rather
    than trusting the build configuration.

    The default build is the full private one, and it is meant to contain the
    suite, so finding it here is only a defect when an actual release is being
    cut. Pass --release, or set SKYRIMBRIDGE_RELEASE=1, to make it fatal. The
    release path must set one of those; without it this only warns, and a
    warning nobody reads is how the suite would end up shipped.
    """
    strict = "--release" in sys.argv or os.environ.get("SKYRIMBRIDGE_RELEASE") == "1"

    for info in release_zip.infolist():
        if not info.filename.lower().endswith((".dll", ".exe")):
            continue
        blob = release_zip.read(info.filename)
        found = [m.decode() for m in NATIVE_SUITE_MARKERS if m in blob]
        if not found:
            continue
        message = (
            f"{info.filename} contains the Kitsuune-derived native replacement "
            f"suite: {', '.join(found)}. Configure with "
            "-DSKYRIMBRIDGE_NATIVE_REPLACEMENTS=OFF for a distributable build. "
            "See CREDITS.md."
        )
        if strict:
            raise AssertionError(message)
        print(f"NOTE: private build, not distributable. {message}")


if __name__ == "__main__":
    raise SystemExit(main())
