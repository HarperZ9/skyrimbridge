#!/usr/bin/env python3
"""Contract checks for the public SkyrimBridge ABI header.

The header is consumed by out-of-tree plugins that resolve
SB_GetBridgeInterface with GetProcAddress. Field order is the ABI, so this
asserts order and signatures, not just presence. Standard library only.
"""

from __future__ import annotations

import pathlib
import re
import sys


HEADER = pathlib.Path(__file__).resolve().parents[1] / "include" / "SkyrimBridgeAPI.h"

EXPECTED_FIELDS = [
    "version",
    "allDataSize",
    "GetFrameIndex",
    "GetFrameData",
    "IsFrameValid",
    "CopyFrameData",
]

EXPECTED_SIGNATURES = {
    "version": r"uint32_t\s+version\s*;",
    "allDataSize": r"uint32_t\s+allDataSize\s*;",
    "GetFrameIndex": r"uint64_t\s*\(\s*\*\s*GetFrameIndex\s*\)\s*\(\s*\)\s*;",
    "GetFrameData": (
        r"const\s+SB::AllData\s*\*\s*\(\s*\*\s*GetFrameData\s*\)\s*\(\s*\)\s*;"
    ),
    "IsFrameValid": r"bool\s*\(\s*\*\s*IsFrameValid\s*\)\s*\(\s*\)\s*;",
    "CopyFrameData": (
        r"bool\s*\(\s*\*\s*CopyFrameData\s*\)\s*\(\s*void\s*\*\s*destination\s*,\s*"
        r"uint32_t\s+destinationSize\s*,\s*uint64_t\s*\*\s*frameIndex\s*\)\s*;"
    ),
}

FORBIDDEN_CODE_TOKENS = [
    "std::",
    "virtual ",
    "throw",
    "try",
    "catch",
    "template",
    "class ",
    "operator ",
    "new ",
    "delete ",
]

FORBIDDEN_INCLUDES = [
    "<array>",
    "<functional>",
    "<memory>",
    "<optional>",
    "<span>",
    "<string>",
    "<string_view>",
    "<type_traits>",
    "<variant>",
    "<vector>",
]

REQUIRED_STATIC_ASSERTS = [
    (
        "SB::Float4 standard-layout",
        r"static_assert\s*\(\s*__is_standard_layout\s*\(\s*SB::Float4\s*\)",
    ),
    (
        "SB::Float4 trivially copyable",
        r"static_assert\s*\(\s*__is_trivially_copyable\s*\(\s*SB::Float4\s*\)",
    ),
    (
        "SB::AllData standard-layout",
        r"static_assert\s*\(\s*__is_standard_layout\s*\(\s*SB::AllData\s*\)",
    ),
    (
        "SB::AllData trivially copyable",
        r"static_assert\s*\(\s*__is_trivially_copyable\s*\(\s*SB::AllData\s*\)",
    ),
    (
        "BridgeInterface standard-layout",
        r"static_assert\s*\(\s*__is_standard_layout\s*\(\s*BridgeInterface\s*\)",
    ),
    (
        "BridgeInterface trivially copyable",
        r"static_assert\s*\(\s*__is_trivially_copyable\s*\(\s*BridgeInterface\s*\)",
    ),
]

EXPORT_DECLARATION_RE = (
    r'extern\s+"C"\s+SB_BRIDGE_API\s+SB::Api::BridgeInterface\s*\*\s*'
    r"SB_GetBridgeInterface\s*\(\s*\)\s*;"
)

EXPORT_MACRO_RE = (
    r"#\s*if\s+defined\s*\(\s*SKYRIMBRIDGE_BUILDING_DLL\s*\)\s*"
    r"#\s*define\s+SB_BRIDGE_API\s+__declspec\s*\(\s*dllexport\s*\)\s*"
    r"#\s*else\s*"
    r"#\s*define\s+SB_BRIDGE_API\s*(?:\r?\n)\s*"
    r"#\s*endif"
)


def strip_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"//.*", "", text)


def bridge_interface_body(text: str) -> str | None:
    match = re.search(r"struct\s+BridgeInterface\s*\{(?P<body>.*?)^\s*\};", text, re.S | re.M)
    if not match:
        return None
    return match.group("body")


def member_statements(body: str) -> list[str]:
    code = strip_comments(body)
    return [statement.strip() + ";" for statement in code.split(";") if statement.strip()]


def field_name(statement: str) -> str | None:
    data_member = re.fullmatch(r"uint32_t\s+(\w+)\s*;", statement)
    if data_member:
        return data_member.group(1)

    function_pointer = re.search(r"\(\s*\*\s*(\w+)\s*\)\s*\(", statement)
    if function_pointer:
        return function_pointer.group(1)

    return None


def main() -> int:
    failures = []

    if not HEADER.is_file():
        print(f"FAIL: header is absent: {HEADER}")
        return 1

    text = HEADER.read_text(encoding="utf-8")
    code = strip_comments(text)

    body = bridge_interface_body(text)
    if body is None:
        failures.append("struct BridgeInterface was not found")
    else:
        statements = member_statements(body)
        ordered = [name for statement in statements if (name := field_name(statement)) is not None]
        if ordered != EXPECTED_FIELDS:
            failures.append(
                f"field order is the ABI; expected {EXPECTED_FIELDS} got {ordered}"
            )
        if len(statements) != len(EXPECTED_FIELDS):
            failures.append(
                f"BridgeInterface must contain exactly {len(EXPECTED_FIELDS)} ABI fields; "
                f"found {len(statements)} declarations"
            )

        for statement in statements:
            name = field_name(statement)
            if name not in EXPECTED_SIGNATURES:
                failures.append(f"unexpected BridgeInterface member declaration: {statement}")
                continue
            if not re.fullmatch(EXPECTED_SIGNATURES[name], statement):
                failures.append(f"wrong signature for BridgeInterface::{name}: {statement}")

        for expected in EXPECTED_FIELDS:
            if not any(field_name(statement) == expected for statement in statements):
                failures.append(f"missing BridgeInterface::{expected}")

    if not re.search(r"\binline\s+constexpr\s+uint32_t\s+kBridgeInterfaceVersion\s*=\s*1U\s*;", code):
        failures.append("kBridgeInterfaceVersion must be inline constexpr uint32_t version 1U")

    if not re.search(EXPORT_MACRO_RE, code):
        failures.append(
            "SB_BRIDGE_API must dllexport only when SKYRIMBRIDGE_BUILDING_DLL is defined "
            "and expand empty for GetProcAddress consumers"
        )

    if "__declspec(dllimport)" in code:
        failures.append("consumer inclusion must not force dllimport; consumers use GetProcAddress")

    dllexport_uses = re.findall(r"__declspec\s*\(\s*dllexport\s*\)", code)
    if len(dllexport_uses) != 1:
        failures.append(
            "dllexport must appear exactly once, inside the SB_BRIDGE_API producer macro"
        )

    if not re.search(EXPORT_DECLARATION_RE, code):
        failures.append("the exported entry point must use extern C plus SB_BRIDGE_API")

    if re.search(r'extern\s+"C"\s+__declspec\s*\(\s*dllexport\s*\)', code):
        failures.append("the consumer-visible declaration must not hard-code dllexport")

    for phrase in ["C++ header", "C-linkage symbol", "not a pure-C header"]:
        if phrase not in text:
            failures.append(f"missing public header documentation phrase: {phrase!r}")

    for label, pattern in REQUIRED_STATIC_ASSERTS:
        if not re.search(pattern, code):
            failures.append(f"missing static assertion: {label}")

    for include in FORBIDDEN_INCLUDES:
        if include in code:
            failures.append(f"STL include in a public ABI header: {include!r}")

    for token in FORBIDDEN_CODE_TOKENS:
        if token in code:
            failures.append(f"non-ABI-safe token in a public header: {token!r}")

    for line in failures:
        print(f"FAIL: {line}")
    if not failures:
        print("validate_bridge_abi_header: all cases passed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
