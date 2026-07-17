#!/usr/bin/env python3
"""Assemble the SkyrimBridge release archive.

Builds a mod-manager-installable zip that mirrors the Skyrim Data layout,
plus reference docs, the shader headers, and the creator tools. Run after a
Release build:

    cmake --build build --config Release --target SkyrimBridge d3d11_proxy
    python scripts/package.py
"""
import os
import shutil
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
VERSION = "3.0.0"
DIST = os.path.join(ROOT, "dist")
STAGE = os.path.join(DIST, f"SkyrimBridge-{VERSION}")

CONFIG = ["SkyrimBridge.ini", "Sky.ini", "WeatherParams.ini",
          "WriteBackConfig.ini", "GPU.ini", "WeatherRouting.example.ini"]


def copy(src, dst):
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    shutil.copy2(src, dst)


def main():
    dll = os.path.join(ROOT, "build", "Release", "SkyrimBridge.dll")
    if not os.path.exists(dll):
        raise SystemExit("build/Release/SkyrimBridge.dll not found; build Release first")

    if os.path.exists(STAGE):
        shutil.rmtree(STAGE)
    os.makedirs(STAGE)

    # installable Data layout
    plug = os.path.join(STAGE, "SKSE", "Plugins")
    copy(dll, os.path.join(plug, "SkyrimBridge.dll"))
    for c in CONFIG:
        copy(os.path.join(ROOT, "config", c),
             os.path.join(plug, "SkyrimBridge", c))

    # reference docs
    for d in ("README.md", "CHANGELOG.md", "LICENSE"):
        copy(os.path.join(ROOT, d), os.path.join(STAGE, "Docs", d))
    for d in ("USER-GUIDE.md", "GPU.md", "parameters.md", "SPEC-ENGINE-EXPOSURE.md",
              "VALIDATION-PROTOCOL.md"):
        copy(os.path.join(ROOT, "docs", d), os.path.join(STAGE, "Docs", d))

    # shader headers (for ENB preset authors) and creator tools
    for f in os.listdir(os.path.join(ROOT, "shaders")):
        copy(os.path.join(ROOT, "shaders", f), os.path.join(STAGE, "Shaders", f))
    for f in ("sb_command_client.py", "sb_smoke_tour.py", "SkyrimBridgeClient.h"):
        copy(os.path.join(ROOT, "tools", f), os.path.join(STAGE, "Tools", f))
    copy(os.path.join(ROOT, "tools", "blender", "skyrimbridge_push.py"),
         os.path.join(STAGE, "Tools", "blender", "skyrimbridge_push.py"))

    # optional D3D11 proxy (goes next to SkyrimSE.exe, not in Data)
    proxy = os.path.join(ROOT, "build", "Release", "d3d11.dll")
    if os.path.exists(proxy):
        copy(proxy, os.path.join(STAGE, "Optional-GPU-Proxy", "d3d11.dll"))
        with open(os.path.join(STAGE, "Optional-GPU-Proxy", "READ-ME.txt"), "w",
                  newline="\r\n") as f:
            f.write("The GPU tier is optional.\r\n\r\n"
                    "Place d3d11.dll next to SkyrimSE.exe, OR chain it through ENB\r\n"
                    "by setting ProxyLibrary in the [PROXY] section of enblocal.ini.\r\n"
                    "Do not replace ENB's own d3d11.dll; chain instead.\r\n"
                    "Without the proxy the rest of the plugin works normally.\r\n")

    # zip it
    archive = os.path.join(DIST, f"SkyrimBridge-{VERSION}.zip")
    if os.path.exists(archive):
        os.remove(archive)
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as z:
        for base, _, files in os.walk(STAGE):
            for fn in files:
                full = os.path.join(base, fn)
                z.write(full, os.path.relpath(full, STAGE))

    size = os.path.getsize(archive)
    print(f"packaged {archive} ({size/1024:.0f} KiB)")
    for base, _, files in os.walk(STAGE):
        for fn in sorted(files):
            print("  " + os.path.relpath(os.path.join(base, fn), STAGE).replace("\\", "/"))


if __name__ == "__main__":
    main()
