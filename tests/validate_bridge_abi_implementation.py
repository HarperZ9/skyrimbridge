#!/usr/bin/env python3
"""Source and PE checks for the SkyrimBridge state ABI producer.

The runtime export is resolved with GetProcAddress, and the plugin itself is
awkward to load outside SKSE. These checks therefore validate the producer's
source-level synchronization contract and, when a DLL path is supplied, parse
the PE export directory directly with the standard library.
"""

from __future__ import annotations

import argparse
import pathlib
import re
import struct
import sys


ROOT = pathlib.Path(__file__).resolve().parents[1]
BRIDGE_API = ROOT / "src" / "core" / "BridgeApi.cpp"
HEADER = ROOT / "include" / "SkyrimBridgeAPI.h"
MAIN = ROOT / "src" / "core" / "main.cpp"
CMAKE = ROOT / "CMakeLists.txt"

REQUIRED_EXPORTS = [b"SB_GetBridgeInterface"]


def strip_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"//.*", "", text)


def normalize(text: str) -> str:
    return re.sub(r"\s+", " ", strip_comments(text)).strip()


def function_body(code: str, signature_re: str) -> str | None:
    match = re.search(signature_re, code)
    if not match:
        return None
    brace = code.find("{", match.end())
    if brace < 0:
        return None
    depth = 0
    for index in range(brace, len(code)):
        if code[index] == "{":
            depth += 1
        elif code[index] == "}":
            depth -= 1
            if depth == 0:
                return code[brace + 1:index]
    return None


def initializer_items(code: str) -> list[str] | None:
    match = re.search(r"BridgeInterface\s+g_interface\s*\{", code)
    if not match:
        return None
    start = code.find("{", match.end() - 1)
    depth = 0
    for index in range(start, len(code)):
        if code[index] == "{":
            depth += 1
        elif code[index] == "}":
            depth -= 1
            if depth == 0:
                body = code[start + 1:index]
                return [item.strip() for item in body.split(",") if item.strip()]
    return None


def check_source() -> list[str]:
    failures: list[str] = []

    if not BRIDGE_API.is_file():
        failures.append(f"BridgeApi.cpp is absent: {BRIDGE_API}")
        bridge_text = ""
        bridge_code = ""
    else:
        bridge_text = BRIDGE_API.read_text(encoding="utf-8")
        bridge_code = strip_comments(bridge_text)
        bridge_one_line = normalize(bridge_text)

        for include in [
            '#include "SkyrimBridgeAPI.h"',
            "#include <atomic>",
            "#include <cstring>",
            "#include <mutex>",
            '#include "core/BridgeData.h"',
        ]:
            if include not in bridge_code:
                failures.append(f"BridgeApi.cpp missing required include: {include}")

        if "namespace SB::Api" not in bridge_code:
            failures.append("BridgeApi.cpp must implement inside namespace SB::Api")

        if "SB::GetMutableData" in bridge_code or "GetMutableData()" in bridge_code:
            failures.append("BridgeApi.cpp must not publish or return the mutating live block")

        patterns = {
            "atomic frame counter starts at zero": (
                r"std::atomic\s*<\s*(?:std::)?uint64_t\s*>\s+g_frameIndex\s*\{\s*0\s*\}"
            ),
            "frame validity starts false": (
                r"std::atomic\s*<\s*bool\s*>\s+g_frameValid\s*\{\s*false\s*\}"
            ),
            "snapshot mutex exists": r"std::mutex\s+g_snapshotMutex\s*;",
            "two published snapshots exist": r"SB::AllData\s+g_snapshots\s*\[\s*2\s*\]\s*\{\s*\}",
            "published slot starts at zero": (
                r"std::atomic\s*<\s*(?:std::)?uint32_t\s*>\s+g_publishedSlot\s*\{\s*0\s*\}"
            ),
            "coherent frame index storage exists": (
                r"(?:std::)?uint64_t\s+g_publishedFrameIndex\s*=\s*0\s*;"
            ),
        }
        for label, pattern in patterns.items():
            if not re.search(pattern, bridge_code):
                failures.append(f"BridgeApi.cpp missing {label}")

        get_index = function_body(
            bridge_code,
            r"(?:std::)?uint64_t\s+GetFrameIndexImpl\s*\(\s*\)",
        )
        if get_index is None:
            failures.append("GetFrameIndexImpl is missing")
        elif not re.search(
            r"g_frameIndex\.load\s*\(\s*std::memory_order_acquire\s*\)",
            get_index,
        ):
            failures.append("GetFrameIndexImpl must acquire-load g_frameIndex")

        get_data = function_body(
            bridge_code,
            r"const\s+SB::AllData\s*\*\s*GetFrameDataImpl\s*\(\s*\)",
        )
        if get_data is None:
            failures.append("GetFrameDataImpl is missing")
        else:
            if "nullptr" not in get_data or not re.search(
                r"g_frameValid\.load\s*\(\s*std::memory_order_acquire\s*\)",
                get_data,
            ):
                failures.append("GetFrameDataImpl must return nullptr when no frame is valid")
            if not re.search(r"g_publishedSlot\.load\s*\(\s*std::memory_order_acquire\s*\)", get_data):
                failures.append("GetFrameDataImpl must acquire-load the published slot")
            if not re.search(r"return\s+&\s*g_snapshots\s*\[\s*slot\s*\]\s*;", get_data):
                failures.append("GetFrameDataImpl must return a published snapshot buffer")

        copy_data = function_body(
            bridge_code,
            r"bool\s+CopyFrameDataImpl\s*\(\s*void\s*\*\s*destination\s*,\s*"
            r"(?:std::)?uint32_t\s+destinationSize\s*,\s*(?:std::)?uint64_t\s*\*\s*frameIndex\s*\)",
        )
        if copy_data is None:
            failures.append("CopyFrameDataImpl is missing")
        else:
            compact_copy = normalize(copy_data)
            if "!destination" not in compact_copy:
                failures.append("CopyFrameDataImpl must reject null destinations")
            if "destinationSize < sizeof(SB::AllData)" not in compact_copy:
                failures.append("CopyFrameDataImpl must reject undersized destinations")
            if not re.search(r"std::lock_guard(?:\s*<[^>]+>)?\s+\w+\s*\(\s*g_snapshotMutex\s*\)", copy_data):
                failures.append("CopyFrameDataImpl must lock g_snapshotMutex")
            if not re.search(
                r"g_frameValid\.load\s*\(\s*std::memory_order_acquire\s*\)",
                copy_data,
            ):
                failures.append("CopyFrameDataImpl must re-check frame validity under synchronization")
            if not re.search(
                r"std::memcpy\s*\(\s*destination\s*,\s*&\s*g_snapshots\s*\[\s*slot\s*\]\s*,\s*"
                r"sizeof\s*\(\s*SB::AllData\s*\)\s*\)",
                copy_data,
            ):
                failures.append("CopyFrameDataImpl must copy exactly sizeof(SB::AllData) from the published snapshot")
            if not re.search(r"if\s*\(\s*frameIndex\s*\)\s*\{\s*\*\s*frameIndex\s*=\s*g_publishedFrameIndex\s*;", copy_data, re.S):
                failures.append("CopyFrameDataImpl must return the frame index coherent with the copied snapshot")

        items = initializer_items(bridge_code)
        expected_initializer = [
            "kBridgeInterfaceVersion",
            "static_cast<uint32_t>(sizeof(SB::AllData))",
            "&GetFrameIndexImpl",
            "&GetFrameDataImpl",
            "&IsFrameValidImpl",
            "&CopyFrameDataImpl",
        ]
        if items != expected_initializer:
            failures.append(
                "BridgeInterface g_interface must initialize all six v1 fields in ABI order; "
                f"expected {expected_initializer} got {items}"
            )

        publish = function_body(
            bridge_code,
            r"void\s+MarkFramePublished\s*\(\s*const\s+SB::AllData\s*&\s*publishedData\s*\)",
        )
        if publish is None:
            failures.append("MarkFramePublished(const SB::AllData& publishedData) is missing")
        else:
            publish_one_line = normalize(publish)
            if not re.search(r"std::lock_guard(?:\s*<[^>]+>)?\s+\w+\s*\(\s*g_snapshotMutex\s*\)", publish):
                failures.append("MarkFramePublished must lock g_snapshotMutex")
            required_order = [
                "g_snapshots[nextSlot] = publishedData;",
                "g_publishedSlot.store(nextSlot, std::memory_order_release);",
                "++g_publishedFrameIndex;",
                "g_frameIndex.store(g_publishedFrameIndex, std::memory_order_release);",
                "g_frameValid.store(true, std::memory_order_release);",
            ]
            positions = [publish_one_line.find(item) for item in required_order]
            if any(position < 0 for position in positions) or positions != sorted(positions):
                failures.append(
                    "MarkFramePublished must copy the sanitized frame into the next snapshot "
                    "before advancing the public frame counter and setting valid true"
                )
            if not re.search(r"1U\s*-\s*currentSlot", publish):
                failures.append("MarkFramePublished must use the alternate double-buffer slot")

        teardown = function_body(bridge_code, r"void\s+MarkTeardown\s*\(\s*\)")
        if teardown is None:
            failures.append("MarkTeardown() is missing")
        elif not re.search(r"g_frameValid\.store\s*\(\s*false\s*,\s*std::memory_order_release\s*\)", teardown):
            failures.append("MarkTeardown must release-store frame validity false")

        exported = function_body(
            bridge_code,
            r'extern\s+"C"\s+SB_BRIDGE_API\s+SB::Api::BridgeInterface\s*\*\s*'
            r"SB_GetBridgeInterface\s*\(\s*\)",
        )
        if exported is None:
            failures.append("SB_GetBridgeInterface export definition must use extern C plus SB_BRIDGE_API")
        elif "return &SB::Api::g_interface;" not in normalize(exported):
            failures.append("SB_GetBridgeInterface must return the static BridgeInterface")

    if not HEADER.is_file():
        failures.append(f"public ABI header is absent: {HEADER}")
    else:
        header_code = strip_comments(HEADER.read_text(encoding="utf-8"))
        if "Internal. Called by the publish path, not by consumers." not in HEADER.read_text(encoding="utf-8"):
            failures.append("internal lifecycle declarations must be marked as producer-only")
        if not re.search(
            r"void\s+MarkFramePublished\s*\(\s*const\s+SB::AllData\s*&\s*publishedData\s*\)\s*;",
            header_code,
        ):
            failures.append("header must declare internal MarkFramePublished helper")
        if not re.search(r"void\s+MarkTeardown\s*\(\s*\)\s*;", header_code):
            failures.append("header must declare internal MarkTeardown helper")

    if not MAIN.is_file():
        failures.append(f"main.cpp is absent: {MAIN}")
    else:
        main_text = MAIN.read_text(encoding="utf-8")
        main_code = strip_comments(main_text)
        main_one_line = normalize(main_text)
        if '#include "SkyrimBridgeAPI.h"' not in main_code:
            failures.append("main.cpp must include SkyrimBridgeAPI.h")
        if "SanitizeAllData(data);" not in main_one_line:
            failures.append("main.cpp sanitization call was not found")
        publish_sequence = [
            "SanitizeAllData(data);",
            "ENBInterface::PushAllData(data);",
            "SB::Api::MarkFramePublished(data);",
        ]
        positions = [main_one_line.find(item) for item in publish_sequence]
        if any(position < 0 for position in positions) or positions != sorted(positions):
            failures.append(
                "main.cpp must publish the ABI snapshot only after sanitization and existing ENBInterface::PushAllData(data)"
            )

        on_exit = function_body(
            main_code,
            r"static\s+void\s+__stdcall\s+OnENBCallback\s*\(\s*int\s+a_callbackType\s*\)",
        )
        if on_exit is None:
            failures.append("OnENBCallback was not found")
        else:
            exit_match = re.search(
                r"case\s+ENBInterface::CallbackType::OnExit\s*:(?P<body>.*?)break\s*;",
                on_exit,
                re.S,
            )
            if not exit_match:
                failures.append("ENB OnExit shutdown path was not found")
            else:
                exit_one_line = normalize(exit_match.group("body"))
                teardown_pos = exit_one_line.find("SB::Api::MarkTeardown();")
                release_calls = [
                    "SB::ParmLinkCompat::Get().Shutdown();",
                    "SB::SharedMemoryBridge::Get().Shutdown();",
                    "SB::BridgeCommand::Get().Shutdown();",
                    "SB::WeatherParameterComputer::Get().Shutdown();",
                ]
                release_positions = [
                    exit_one_line.find(call) for call in release_calls if exit_one_line.find(call) >= 0
                ]
                if teardown_pos < 0:
                    failures.append("ENB OnExit must call SB::Api::MarkTeardown()")
                elif release_positions and teardown_pos > min(release_positions):
                    failures.append("MarkTeardown must run before existing shutdown calls release producer state")

    if not CMAKE.is_file():
        failures.append(f"CMakeLists.txt is absent: {CMAKE}")
    else:
        cmake_code = CMAKE.read_text(encoding="utf-8")
        if "src/core/BridgeApi.cpp" not in cmake_code:
            failures.append("CMakeLists.txt must add src/core/BridgeApi.cpp to the plugin target sources")
        if "${CMAKE_CURRENT_SOURCE_DIR}/include" not in cmake_code:
            failures.append("CMakeLists.txt must add repository include/ to the plugin target include paths")
        if "SKYRIMBRIDGE_BUILDING_DLL=1" not in cmake_code:
            failures.append("CMakeLists.txt must define SKYRIMBRIDGE_BUILDING_DLL=1 for the producer target")

    return failures


def exported_names(path: pathlib.Path) -> list[bytes]:
    data = path.read_bytes()
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe:pe + 4] != b"PE\0\0":
        raise ValueError("not a PE image")

    optional_magic = struct.unpack_from("<H", data, pe + 24)[0]
    dir_offset = pe + 24 + (112 if optional_magic == 0x20B else 96)
    export_rva, _ = struct.unpack_from("<II", data, dir_offset)
    if export_rva == 0:
        return []

    section_count = struct.unpack_from("<H", data, pe + 6)[0]
    opt_size = struct.unpack_from("<H", data, pe + 20)[0]
    sections = []
    base = pe + 24 + opt_size
    for i in range(section_count):
        off = base + i * 40
        virt_size, virt_addr, raw_size, raw_ptr = struct.unpack_from("<IIII", data, off + 8)
        sections.append((virt_addr, max(virt_size, raw_size), raw_ptr))

    def to_file_offset(rva: int) -> int:
        for virt_addr, raw_size, raw_ptr in sections:
            if virt_addr <= rva < virt_addr + raw_size:
                return raw_ptr + (rva - virt_addr)
        raise ValueError(f"unmapped RVA {rva:#x}")

    table = to_file_offset(export_rva)
    name_count = struct.unpack_from("<I", data, table + 24)[0]
    names_rva = struct.unpack_from("<I", data, table + 32)[0]
    names_off = to_file_offset(names_rva)

    out: list[bytes] = []
    for i in range(name_count):
        rva = struct.unpack_from("<I", data, names_off + i * 4)[0]
        start = to_file_offset(rva)
        end = data.index(b"\0", start)
        out.append(data[start:end])
    return out


def check_exports(dll: pathlib.Path | None) -> list[str]:
    if dll is None:
        return []
    if not dll.is_file():
        return [f"DLL is absent: {dll}"]

    try:
        names = exported_names(dll)
    except (OSError, struct.error, ValueError) as exc:
        return [f"could not parse PE exports from {dll}: {exc}"]

    missing = [name.decode("ascii") for name in REQUIRED_EXPORTS if name not in names]
    return [f"{name} is not exported from {dll}" for name in missing]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--dll",
        type=pathlib.Path,
        help="optional built SkyrimBridge.dll path for PE export validation",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    failures = check_source()
    failures.extend(check_exports(args.dll))

    for failure in failures:
        print(f"FAIL: {failure}")
    if not failures:
        if args.dll is None:
            print("validate_bridge_abi_implementation: source contract passed")
        else:
            print("validate_bridge_abi_implementation: source contract and PE export passed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
