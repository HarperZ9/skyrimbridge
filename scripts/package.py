#!/usr/bin/env python3
"""Build the deterministic public SkyrimBridge release archive.

Run after both Release targets have been built:

    cmake --build build --config Release --target SkyrimBridge d3d11_proxy
    python scripts/package.py

The release ZIP always contains the SKSE plugin and the optional GPU proxy.
It also contains a content-hash manifest and is accompanied by a SHA-256
sidecar. Identical inputs produce byte-for-byte identical archives.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import struct
import tempfile
import zipfile


ROOT = Path(__file__).resolve().parents[1]
PACKAGE = "SkyrimBridge"
FIXED_ZIP_TIMESTAMP = (2000, 1, 1, 0, 0, 0)

CONFIG_FILES = (
    "SkyrimBridge.ini",
    "Sky.ini",
    "WeatherParams.ini",
    "WriteBackConfig.ini",
    "GPU.ini",
    "WeatherRouting.example.ini",
)

# Configs owned by the Kitsuune-derived native replacement suite. A build made
# with SKYRIMBRIDGE_NATIVE_REPLACEMENTS=OFF has no code that reads these, so
# shipping them would hand the user settings for features the binary does not
# have. See CREDITS.md.
NATIVE_SUITE_CONFIGS = frozenset({"Sky.ini", "WeatherRouting.example.ini"})

# The marker used to decide which kind of build is being packaged. Read from the
# compiled bytes rather than from a CMake variable, so the archive contents can
# never disagree with the binary they ship beside.
NATIVE_SUITE_MARKER = b"SkyLighting"

PUBLIC_FILES = (
    ("README.md", "Docs/README.md"),
    ("CHANGELOG.md", "Docs/CHANGELOG.md"),
    ("LICENSE", "Docs/LICENSE"),
    ("THIRD_PARTY_NOTICES.md", "Docs/THIRD_PARTY_NOTICES.md"),
    ("docs/USER-GUIDE.md", "Docs/USER-GUIDE.md"),
    ("docs/GPU.md", "Docs/GPU.md"),
    ("docs/parameters.md", "Docs/parameters.md"),
    ("docs/SPEC-ENGINE-EXPOSURE.md", "Docs/SPEC-ENGINE-EXPOSURE.md"),
    ("docs/VALIDATION-PROTOCOL.md", "Docs/VALIDATION-PROTOCOL.md"),
    ("shaders/enbUI_SkyrimBridge.fxh", "Shaders/enbUI_SkyrimBridge.fxh"),
    ("shaders/SkyrimBridge.fxh", "Shaders/SkyrimBridge.fxh"),
    ("shaders/SkyrimBridge_CB.fxh", "Shaders/SkyrimBridge_CB.fxh"),
    ("tools/sb_command_client.py", "Tools/sb_command_client.py"),
    ("tools/sb_smoke_tour.py", "Tools/sb_smoke_tour.py"),
    ("tools/SkyrimBridgeClient.h", "Tools/SkyrimBridgeClient.h"),
    (
        "tools/blender/skyrimbridge_push.py",
        "Tools/blender/skyrimbridge_push.py",
    ),
)

GPU_PROXY_README = (
    "The GPU tier is optional.\r\n"
    "\r\n"
    "Place d3d11.dll next to SkyrimSE.exe, OR chain it through ENB by setting\r\n"
    "ProxyLibrary in the [PROXY] section of enblocal.ini.\r\n"
    "Do not replace ENB's own d3d11.dll; chain instead.\r\n"
    "Without the proxy the rest of the plugin works normally.\r\n"
).encode("ascii")


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def read_project_version(root: Path) -> str:
    """Read and cross-check the canonical version in CMake and vcpkg."""
    cmake = (root / "CMakeLists.txt").read_text(encoding="utf-8")
    match = re.search(
        r"project\s*\(\s*SkyrimBridge\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)",
        cmake,
        flags=re.IGNORECASE | re.MULTILINE,
    )
    if not match:
        raise ValueError("cannot read SkyrimBridge version from CMakeLists.txt")
    cmake_version = match.group(1)

    vcpkg = json.loads((root / "vcpkg.json").read_text(encoding="utf-8"))
    vcpkg_version = vcpkg.get("version-semver")
    if cmake_version != vcpkg_version:
        raise ValueError(
            "version mismatch: "
            f"CMakeLists.txt={cmake_version!r}, vcpkg.json={vcpkg_version!r}"
        )
    return cmake_version


def validate_x64_pe(path: Path, label: str) -> None:
    """Reject missing, malformed, or non-x64 release binaries."""
    if not path.is_file():
        raise FileNotFoundError(f"{label} not found: {path}")
    data = path.read_bytes()
    if len(data) < 0x40 or data[:2] != b"MZ":
        raise ValueError(f"{label} is not a PE executable: {path}")
    pe_offset = struct.unpack_from("<I", data, 0x3C)[0]
    if pe_offset + 6 > len(data) or data[pe_offset : pe_offset + 4] != b"PE\0\0":
        raise ValueError(f"{label} has an invalid PE header: {path}")
    machine = struct.unpack_from("<H", data, pe_offset + 4)[0]
    if machine != 0x8664:
        raise ValueError(
            f"{label} must be x64 (PE machine 0x8664), got 0x{machine:04x}"
        )


def checked_destination(stage: Path, relative: str) -> Path:
    posix_path = PurePosixPath(relative)
    if posix_path.is_absolute() or ".." in posix_path.parts:
        raise ValueError(f"unsafe package path: {relative!r}")
    destination = stage.joinpath(*posix_path.parts)
    if stage.resolve() not in destination.resolve().parents:
        raise ValueError(f"package path escapes staging root: {relative!r}")
    return destination


def copy_payload(source: Path, stage: Path, relative: str) -> None:
    if not source.is_file():
        raise FileNotFoundError(f"required release input not found: {source}")
    destination = checked_destination(stage, relative)
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, destination)


def assemble_stage(root: Path, build_dir: Path, stage: Path, version: str) -> list[str]:
    plugin = build_dir / "Release" / "SkyrimBridge.dll"
    proxy = build_dir / "Release" / "d3d11.dll"
    validate_x64_pe(plugin, "SKSE plugin")
    validate_x64_pe(proxy, "GPU proxy")

    copy_payload(plugin, stage, "SKSE/Plugins/SkyrimBridge.dll")

    has_native_suite = NATIVE_SUITE_MARKER in plugin.read_bytes()
    configs = [
        name for name in CONFIG_FILES
        if has_native_suite or name not in NATIVE_SUITE_CONFIGS
    ]
    for name in configs:
        copy_payload(
            root / "config" / name,
            stage,
            f"SKSE/Plugins/SkyrimBridge/{name}",
        )
    for source, destination in PUBLIC_FILES:
        copy_payload(root / Path(source), stage, destination)

    # THIRD_PARTY_NOTICES.md points at CREDITS.md for the Kitsuune attribution,
    # so the two ship together or the notices reference a file that is not there.
    copy_payload(root / "CREDITS.md", stage, "Docs/CREDITS.md")
    copy_payload(proxy, stage, "Optional-GPU-Proxy/d3d11.dll")

    proxy_readme = checked_destination(stage, "Optional-GPU-Proxy/READ-ME.txt")
    proxy_readme.parent.mkdir(parents=True, exist_ok=True)
    proxy_readme.write_bytes(GPU_PROXY_README)

    payload_paths = sorted(
        path.relative_to(stage).as_posix()
        for path in stage.rglob("*")
        if path.is_file()
    )
    manifest = {
        "schema_version": 1,
        "package": PACKAGE,
        "version": version,
        "files": [
            {
                "path": relative,
                "size": (stage / Path(relative)).stat().st_size,
                "sha256": sha256_file(stage / Path(relative)),
            }
            for relative in payload_paths
        ],
    }
    manifest_bytes = (
        json.dumps(manifest, indent=2, sort_keys=True, ensure_ascii=True) + "\n"
    ).encode("utf-8")
    checked_destination(stage, "MANIFEST.json").write_bytes(manifest_bytes)
    return payload_paths


def write_deterministic_zip(stage: Path, archive: Path) -> None:
    paths = sorted(
        path.relative_to(stage).as_posix()
        for path in stage.rglob("*")
        if path.is_file()
    )
    with zipfile.ZipFile(
        archive,
        mode="w",
        compression=zipfile.ZIP_DEFLATED,
        compresslevel=9,
        strict_timestamps=True,
    ) as release_zip:
        for relative in paths:
            data = (stage / Path(relative)).read_bytes()
            info = zipfile.ZipInfo(relative, date_time=FIXED_ZIP_TIMESTAMP)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.create_system = 3
            info.external_attr = 0o100644 << 16
            release_zip.writestr(info, data, compress_type=zipfile.ZIP_DEFLATED, compresslevel=9)


def package_release(
    root: Path = ROOT,
    build_dir: Path | None = None,
    dist_dir: Path | None = None,
) -> tuple[Path, list[str]]:
    root = root.resolve()
    build_dir = (build_dir or root / "build").resolve()
    dist_dir = (dist_dir or root / "dist").resolve()
    version = read_project_version(root)
    archive_name = f"{PACKAGE}-{version}.zip"

    dist_dir.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".skyrimbridge-package-", dir=dist_dir) as temp:
        temp_root = Path(temp)
        stage = temp_root / "stage"
        stage.mkdir()
        payload_paths = assemble_stage(root, build_dir, stage, version)

        temporary_archive = temp_root / archive_name
        write_deterministic_zip(stage, temporary_archive)
        digest = sha256_file(temporary_archive)
        temporary_sidecar = temp_root / f"{archive_name}.sha256"
        temporary_sidecar.write_text(
            f"{digest}  {archive_name}\n",
            encoding="ascii",
            newline="\n",
        )

        archive = dist_dir / archive_name
        sidecar = dist_dir / f"{archive_name}.sha256"
        os.replace(temporary_archive, archive)
        os.replace(temporary_sidecar, sidecar)

    return archive, payload_paths


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--build-dir",
        type=Path,
        default=ROOT / "build",
        help="CMake build directory (default: %(default)s)",
    )
    parser.add_argument(
        "--dist-dir",
        type=Path,
        default=ROOT / "dist",
        help="release output directory (default: %(default)s)",
    )
    parser.add_argument(
        "--quiet",
        action="store_true",
        help="print only the final archive path and digest",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        archive, payload_paths = package_release(
            build_dir=args.build_dir,
            dist_dir=args.dist_dir,
        )
    except (FileNotFoundError, ValueError, OSError, json.JSONDecodeError) as error:
        raise SystemExit(f"error: {error}") from error

    digest = sha256_file(archive)
    print(f"packaged {archive} ({archive.stat().st_size / 1024:.0f} KiB)")
    print(f"sha256  {digest}")
    if not args.quiet:
        for relative in payload_paths:
            print(f"  {relative}")
        print("  MANIFEST.json")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
