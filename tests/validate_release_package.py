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
import argparse
import configparser
from pathlib import Path
import subprocess
import struct
import sys
import tempfile
import zipfile


ROOT = Path(__file__).resolve().parents[1]
VERSION = "3.0.0"
ARCHIVE_NAME = f"SkyrimBridge-{VERSION}.zip"

EXPECTED_DOCS = {
    "BRIDGE-ABI.md",
    "CHANGELOG.md",
    "CREDITS.md",
    "GPU.md",
    "LICENSE",
    "README.md",
    "SPEC-ENGINE-EXPOSURE.md",
    "THIRD_PARTY_NOTICES.md",
    "USER-GUIDE.md",
    "VALIDATION-PROTOCOL.md",
    "parameters.md",
}

BASE_PAYLOAD = {f"Docs/{name}" for name in EXPECTED_DOCS} | {
    "Optional-GPU-Proxy/READ-ME.txt",
    "Optional-GPU-Proxy/d3d11.dll",
    "SDK/SkyrimBridgeAPI.h",
    "SDK/core/BridgeData.h",
    "SKSE/Plugins/SkyrimBridge.dll",
    "SKSE/Plugins/SkyrimBridge/GPU.ini",
    "SKSE/Plugins/SkyrimBridge/SkyrimBridge.ini",
    "SKSE/Plugins/SkyrimBridge/WeatherParams.ini",
    "SKSE/Plugins/SkyrimBridge/WriteBackConfig.ini",
    "Shaders/SkyrimBridge.fxh",
    "Shaders/SkyrimBridge_CB.fxh",
    "Shaders/enbUI_SkyrimBridge.fxh",
    "Tools/SkyrimBridgeClient.h",
    "Tools/blender/skyrimbridge_push.py",
    "Tools/sb_command_client.py",
    "Tools/sb_smoke_tour.py",
}

PROVENANCE_MARKER_DOCS = {
    "Docs/BRIDGE-ABI.md",
    "Docs/CREDITS.md",
    "Docs/README.md",
    "Docs/THIRD_PARTY_NOTICES.md",
    "Docs/USER-GUIDE.md",
    "Docs/VALIDATION-PROTOCOL.md",
}


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def make_minimal_x64_pe(*markers: bytes) -> bytes:
    """Return enough of a PE image for the public packager's x64 gate."""
    data = bytearray(512)
    data[0:2] = b"MZ"
    pe_offset = 0x80
    struct.pack_into("<I", data, 0x3C, pe_offset)
    data[pe_offset : pe_offset + 4] = b"PE\0\0"
    struct.pack_into("<H", data, pe_offset + 4, 0x8664)
    for marker in markers:
        data.extend(marker)
    return bytes(data)


def run_packager(output_dir: Path, build_dir: Path) -> Path:
    result = subprocess.run(
        [
            sys.executable,
            str(ROOT / "scripts" / "package.py"),
            "--build-dir",
            str(build_dir),
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


def check_native_suite_build_is_rejected() -> None:
    """A public package must fail hard on a permission-gated native marker."""
    with tempfile.TemporaryDirectory(prefix="skyrimbridge-native-marker-test-") as temp:
        temp_root = Path(temp)
        build_dir = temp_root / "build"
        release_dir = build_dir / "Release"
        dist_dir = temp_root / "dist"
        release_dir.mkdir(parents=True)
        (release_dir / "SkyrimBridge.dll").write_bytes(
            make_minimal_x64_pe(NATIVE_SUITE_MARKERS[0])
        )
        (release_dir / "d3d11.dll").write_bytes(make_minimal_x64_pe())

        result = subprocess.run(
            [
                sys.executable,
                str(ROOT / "scripts" / "package.py"),
                "--build-dir",
                str(build_dir),
                "--dist-dir",
                str(dist_dir),
                "--quiet",
            ],
            cwd=ROOT,
            text=True,
            capture_output=True,
        )
        output = result.stdout + result.stderr
        assert result.returncode != 0, (
            "packager accepted a binary carrying a native-suite marker:\n"
            f"{output}"
        )
        assert "Kitsuune" in output or "native replacement" in output, output
        assert not (dist_dir / ARCHIVE_NAME).exists(), (
            "packager created a public archive after detecting native-suite content"
        )
        assert not (dist_dir / f"{ARCHIVE_NAME}.sha256").exists(), (
            "packager created a checksum for a rejected native-suite archive"
        )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--build-dir",
        type=Path,
        default=ROOT / "build",
        help="CMake build directory to package and validate (default: %(default)s)",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    build_dir = args.build_dir.resolve()
    check_native_suite_build_is_rejected()

    required_binaries = (
        build_dir / "Release" / "SkyrimBridge.dll",
        build_dir / "Release" / "d3d11.dll",
    )
    missing = [str(path) for path in required_binaries if not path.is_file()]
    assert not missing, "build Release targets first; missing: " + ", ".join(missing)

    with tempfile.TemporaryDirectory(prefix="skyrimbridge-package-test-") as temp:
        temp_root = Path(temp)
        archive_a = run_packager(temp_root / "a", build_dir)
        archive_b = run_packager(temp_root / "b", build_dir)
        bytes_a = archive_a.read_bytes()
        bytes_b = archive_b.read_bytes()

        assert bytes_a == bytes_b, "identical inputs produced different ZIP bytes"

        digest = sha256(bytes_a)
        for archive in (archive_a, archive_b):
            sidecar = archive.with_suffix(archive.suffix + ".sha256")
            expected_sidecar = f"{digest}  {archive.name}\n"
            assert sidecar.read_text(encoding="ascii") == expected_sidecar

        with zipfile.ZipFile(archive_a) as release_zip:
            expected_files = BASE_PAYLOAD

            names = release_zip.namelist()
            assert names == sorted(expected_files | {"MANIFEST.json"}), (
                "archive manifest mismatch:\n"
                f"expected={sorted(expected_files | {'MANIFEST.json'})!r}\n"
                f"actual={names!r}"
            )
            assert len(names) == len(set(names)), "archive contains duplicate paths"
            assert all(not name.startswith("/") for name in names)
            assert all("\\" not in name and ".." not in Path(name).parts for name in names)

            manifest = json.loads(release_zip.read("MANIFEST.json"))
            assert manifest["schema_version"] == 1
            assert manifest["package"] == "SkyrimBridge"
            assert manifest["version"] == VERSION
            entries = manifest["files"]
            assert [entry["path"] for entry in entries] == sorted(expected_files)

            for entry in entries:
                payload = release_zip.read(entry["path"])
                assert entry["size"] == len(payload), entry["path"]
                assert entry["sha256"] == sha256(payload), entry["path"]

            for info in release_zip.infolist():
                assert info.date_time == (2000, 1, 1, 0, 0, 0), info.filename

            check_no_native_replacements(release_zip)
            check_no_private_markers_outside_provenance(release_zip)
            check_public_runtime_defaults(release_zip)

    print(
        "PASS: deterministic public release package "
        f"({len(expected_files)} payload files + manifest, distributable build)"
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
    b"Sky.ini",
    b"WeatherRouting.ini",
)


def check_no_native_replacements(release_zip: zipfile.ZipFile) -> None:
    """Fail if a packaged binary carries the permission-gated suite.

    Distributing it needs Kitsuune's permission, which has not been granted;
    see CREDITS.md. A runtime toggle is not sufficient, because an inert copy
    in the binary is still a copy, so this inspects the shipped bytes rather
    than trusting the build configuration.
    """
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
        raise AssertionError(message)


def check_no_private_markers_outside_provenance(
    release_zip: zipfile.ZipFile,
) -> None:
    """Runtime payloads and non-provenance docs must not carry suite markers."""
    failures = []
    for info in release_zip.infolist():
        if info.filename == "MANIFEST.json" or info.filename in PROVENANCE_MARKER_DOCS:
            continue
        blob = release_zip.read(info.filename)
        found = [m.decode("ascii") for m in NATIVE_SUITE_MARKERS if m in blob]
        if found:
            failures.append(f"{info.filename}: {', '.join(found)}")
    assert not failures, (
        "private native-suite markers outside provenance docs:\n"
        + "\n".join(failures)
    )


def check_public_runtime_defaults(release_zip: zipfile.ZipFile) -> None:
    """Engine-writing public defaults must stay disabled in the package."""
    text = release_zip.read("SKSE/Plugins/SkyrimBridge/SkyrimBridge.ini").decode(
        "ascii"
    )
    parser = configparser.ConfigParser(inline_comment_prefixes=(";",))
    parser.optionxform = str
    parser.read_string(text)

    assert parser.has_section("Native"), "public SkyrimBridge.ini lacks [Native]"
    value = parser.get("Native", "EngineFixes", fallback=None)
    assert value == "false", (
        "public SkyrimBridge.ini must ship engine-writing EngineFixes disabled; "
        f"got {value!r}"
    )


if __name__ == "__main__":
    raise SystemExit(main())
