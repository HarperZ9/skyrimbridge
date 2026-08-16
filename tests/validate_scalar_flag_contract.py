#!/usr/bin/env python3
"""Focused regression checks for public scalar flag fields.

BridgeData.h documents these public flag groups as ordinary 0.0/1.0 float4
components. This test guards against silently reintroducing bit-packed .x
publishers or shader helpers for those fields.
"""

from __future__ import annotations

import pathlib
import re


ROOT = pathlib.Path(__file__).resolve().parents[1]


SCALAR_PUBLIC_COMMENTS = {
    "src/core/BridgeData.h": [
        r"Float4\s+Flags;\s*//\s*\.x\s*=\s*isPleasant,\s*\.y\s*=\s*isCloudy,\s*\.z\s*=\s*isRainy,\s*\.w\s*=\s*isSnowy",
        r"Float4\s+IsInterior;\s*//\s*\.x\s*=\s*isInterior\(0/1\),\s*\.y\s*=\s*hasLightingTemplate,\s*\.z\s*=\s*0,\s*\.w\s*=\s*0",
        r"Float4\s+VisionEffects;\s*//\s*\.x\s*=\s*nightEye\(0/1\),\s*\.y\s*=\s*detectLife\(0/1\),\s*\.z\s*=\s*detectDead\(0/1\),\s*\.w\s*=\s*etherealForm\(0/1\)",
        r"Float4\s+DamageEffects;\s*//\s*\.x\s*=\s*isTakingFireDmg,\s*\.y\s*=\s*isTakingFrostDmg,\s*\.z\s*=\s*isTakingShockDmg,\s*\.w\s*=\s*isTakingPoisonDmg",
        r"Float4\s+MiscEffects;\s*//\s*\.x\s*=\s*isInvisible,\s*\.y\s*=\s*isParalyzed,\s*\.z\s*=\s*isDrunk \(skooma/ale\),\s*\.w\s*=\s*0",
        r"Float4\s+Flags;\s*//\s*\.x\s*=\s*weaponDrawn\(0/1\),\s*\.y\s*=\s*hasBow\(0/1\),\s*\.z\s*=\s*hasTorch\(0/1\),\s*\.w\s*=\s*isTwoHanding\(0/1\)",
        r"Float4\s+Menus;\s*//\s*\.x\s*=\s*isInMenu\(0/1\),\s*\.y\s*=\s*isInDialogue\(0/1\),\s*\.z\s*=\s*isInInventory\(0/1\),\s*\.w\s*=\s*isInMap\(0/1\)",
        r"Float4\s+HUD;\s*//\s*\.x\s*=\s*isHUDVisible\(0/1\),\s*\.y\s*=\s*isCrosshairVisible\(0/1\),\s*\.z\s*=\s*isInCinematicMode\(0/1\),\s*\.w\s*=\s*isLoading\(0/1\)",
        r"Float4\s+Detail;\s*//\s*\.x\s*=\s*isInCrafting\(0/1\),\s*\.y\s*=\s*isInBook\(0/1\),\s*\.z\s*=\s*isInLockpick\(0/1\),\s*\.w\s*=\s*isInConsole\(0/1\)",
    ],
}

PUBLISHER_ASSIGNMENTS = {
    "src/core/WeatherTracker.cpp": [
        r"data\.Flags\.x\s*=\s*isPleasant\s*\?\s*1\.0f\s*:\s*0\.0f\s*;",
        r"data\.Flags\.y\s*=\s*isCloudy\s*\?\s*1\.0f\s*:\s*0\.0f\s*;",
        r"data\.Flags\.z\s*=\s*isRain\s*\?\s*1\.0f\s*:\s*0\.0f\s*;",
        r"data\.Flags\.w\s*=\s*isSnow\s*\?\s*1\.0f\s*:\s*0\.0f\s*;",
    ],
    "src/core/InteriorTracker.cpp": [
        r"data\.IsInterior\.x\s*=\s*isInterior\s*\?\s*1\.0f\s*:\s*0\.0f\s*;",
        r"data\.IsInterior\.y\s*=\s*hasLightingTemplate\s*\?\s*1\.0f\s*:\s*0\.0f\s*;",
        r"data\.IsInterior\.z\s*=\s*0\.0f\s*;",
        r"data\.IsInterior\.w\s*=\s*0\.0f\s*;",
    ],
    "src/core/EffectsTracker.cpp": [
        r"data\.VisionEffects\.x\s*=\s*hasNightEye\s*\?\s*1\.0f\s*:\s*0\.0f\s*;",
        r"data\.VisionEffects\.y\s*=\s*hasDetectLife\s*\?\s*1\.0f\s*:\s*0\.0f\s*;",
        r"data\.VisionEffects\.z\s*=\s*hasDetectDead\s*\?\s*1\.0f\s*:\s*0\.0f\s*;",
        r"data\.VisionEffects\.w\s*=\s*hasEthereal\s*\?\s*1\.0f\s*:\s*0\.0f\s*;",
        r"data\.DamageEffects\.x\s*=\s*hasFireDamage\s*\?\s*1\.0f\s*:\s*0\.0f\s*;",
        r"data\.DamageEffects\.y\s*=\s*hasFrostDamage\s*\?\s*1\.0f\s*:\s*0\.0f\s*;",
        r"data\.DamageEffects\.z\s*=\s*hasShockDamage\s*\?\s*1\.0f\s*:\s*0\.0f\s*;",
        r"data\.DamageEffects\.w\s*=\s*hasPoisonDamage\s*\?\s*1\.0f\s*:\s*0\.0f\s*;",
        r"data\.MiscEffects\.x\s*=\s*hasInvisibility\s*\?\s*1\.0f\s*:\s*0\.0f\s*;",
        r"data\.MiscEffects\.y\s*=\s*hasParalysis\s*\?\s*1\.0f\s*:\s*0\.0f\s*;",
        r"data\.MiscEffects\.z\s*=\s*hasDrunk\s*\?\s*1\.0f\s*:\s*0\.0f\s*;",
        r"data\.MiscEffects\.w\s*=\s*0\.0f\s*;",
    ],
    "src/core/EquipmentTracker.cpp": [
        r"data\.Flags\.x\s*=\s*hasWeaponDrawn\s*\?\s*1\.0f\s*:\s*0\.0f\s*;",
        r"data\.Flags\.y\s*=\s*hasBow\s*\?\s*1\.0f\s*:\s*0\.0f\s*;",
        r"data\.Flags\.z\s*=\s*hasTorch\s*\?\s*1\.0f\s*:\s*0\.0f\s*;",
        r"data\.Flags\.w\s*=\s*isTwoHanding\s*\?\s*1\.0f\s*:\s*0\.0f\s*;",
    ],
    "src/core/UIStateTracker.cpp": [
        r"data\.Menus\.x\s*=\s*isInMenu\s*\?\s*1\.0f\s*:\s*0\.0f\s*;",
        r"data\.Menus\.y\s*=\s*isInDialogue\s*\?\s*1\.0f\s*:\s*0\.0f\s*;",
        r"data\.Menus\.z\s*=\s*isInInventory\s*\?\s*1\.0f\s*:\s*0\.0f\s*;",
        r"data\.Menus\.w\s*=\s*isInMap\s*\?\s*1\.0f\s*:\s*0\.0f\s*;",
        r"data\.HUD\.x\s*=\s*isHUDVisible\s*\?\s*1\.0f\s*:\s*0\.0f\s*;",
        r"data\.HUD\.y\s*=\s*isCrosshairVisible\s*\?\s*1\.0f\s*:\s*0\.0f\s*;",
        r"data\.HUD\.z\s*=\s*isInCinematicMode\s*\?\s*1\.0f\s*:\s*0\.0f\s*;",
        r"data\.HUD\.w\s*=\s*isLoading\s*\?\s*1\.0f\s*:\s*0\.0f\s*;",
        r"data\.Detail\.x\s*=\s*isInCrafting\s*\?\s*1\.0f\s*:\s*0\.0f\s*;",
        r"data\.Detail\.y\s*=\s*isInBook\s*\?\s*1\.0f\s*:\s*0\.0f\s*;",
        r"data\.Detail\.z\s*=\s*isInLockpick\s*\?\s*1\.0f\s*:\s*0\.0f\s*;",
        r"data\.Detail\.w\s*=\s*isInConsole\s*\?\s*1\.0f\s*:\s*0\.0f\s*;",
    ],
}

CB_SCALAR_COMMENTS = {
    "SB_Weather_Flags": r"\.x = isPleasant\(0/1\), \.y = isCloudy\(0/1\), \.z = isRainy\(0/1\), \.w = isSnowy\(0/1\)",
    "SB_Interior_Flags": r"\.x = isInterior\(0/1\), \.y = hasLightingTemplate\(0/1\), \.z = 0, \.w = 0",
    "SB_FX_Vision": r"\.x = nightEye\(0/1\), \.y = detectLife\(0/1\), \.z = detectDead\(0/1\), \.w = etherealForm\(0/1\)",
    "SB_FX_Damage": r"\.x = fire\(0/1\), \.y = frost\(0/1\), \.z = shock\(0/1\), \.w = poison\(0/1\)",
    "SB_FX_Misc": r"\.x = invisible\(0/1\), \.y = paralyzed\(0/1\), \.z = drunk\(0/1\), \.w = 0",
    "SB_Equip_Flags": r"\.x = weaponDrawn\(0/1\), \.y = hasBow\(0/1\), \.z = hasTorch\(0/1\), \.w = isTwoHanding\(0/1\)",
    "SB_UI_Menus": r"\.x = isInMenu\(0/1\), \.y = isInDialogue\(0/1\), \.z = isInInventory\(0/1\), \.w = isInMap\(0/1\)",
    "SB_UI_HUD": r"\.x = isHUDVisible\(0/1\), \.y = isCrosshairVisible\(0/1\), \.z = isInCinematicMode\(0/1\), \.w = isLoading\(0/1\)",
    "SB_UI_Detail": r"\.x = isInCrafting\(0/1\), \.y = isInBook\(0/1\), \.z = isInLockpick\(0/1\), \.w = isInConsole\(0/1\)",
}

CB_HELPERS = [
    r"bool\s+SB_IsInMenu\(\)\s*\{\s*return\s+SB_UI_Menus\.x\s*>\s*0\.5\s*;\s*\}",
    r"bool\s+SB_IsInDialogue\(\)\s*\{\s*return\s+SB_UI_Menus\.y\s*>\s*0\.5\s*;\s*\}",
    r"bool\s+SB_IsLoading\(\)\s*\{\s*return\s+SB_UI_HUD\.w\s*>\s*0\.5\s*;\s*\}",
    r"bool\s+SB_HasTorchEquipped\(\)\s*\{\s*return\s+SB_Equip_Flags\.z\s*>\s*0\.5\s*;\s*\}",
    r"bool\s+SB_IsRaining\(\)\s*\{\s*return\s+SB_Weather_Flags\.z\s*>\s*0\.5\s*;\s*\}",
    r"bool\s+SB_IsSnowing\(\)\s*\{\s*return\s+SB_Weather_Flags\.w\s*>\s*0\.5\s*;\s*\}",
    r"float\s+SB_RainFlag\(\)\s*\{\s*return\s+SB_Weather_Flags\.z\s*;\s*\}",
    r"float\s+SB_SnowFlag\(\)\s*\{\s*return\s+SB_Weather_Flags\.w\s*;\s*\}",
    r"float\s+SB_InteriorFlag\(\)\s*\{\s*return\s+SB_Interior_Flags\.x\s*;\s*\}",
    r"bool\s+SB_IsInterior\(\)\s*\{\s*return\s+SB_Interior_Flags\.x\s*>\s*0\.5\s*;\s*\}",
]

FORBIDDEN_SCALAR_PACKING_TOKENS = [
    "SB_HAS_FLAG",
    "SB_FLAG_TO_FLOAT",
    "SB_WFLAG_",
    "SB_IFLAG_",
    "SB_VFLAG_",
    "SB_DFLAG_",
    "SB_MFLAG_",
    "SB_EFLAG_",
    "SB_UFLAG_",
    "asuint(SB_Weather_Flags.x",
    "asuint(SB_Interior_Flags.x",
    "asuint(SB_FX_",
    "asuint(SB_Equip_Flags.x",
    "asuint(SB_UI_",
]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


def assert_patterns(failures: list[str], relative: str, patterns: list[str]) -> None:
    text = read(relative)
    for pattern in patterns:
        if not re.search(pattern, text):
            failures.append(f"{relative} missing scalar contract pattern: {pattern}")


def main() -> int:
    failures: list[str] = []

    for relative, patterns in SCALAR_PUBLIC_COMMENTS.items():
        assert_patterns(failures, relative, patterns)

    for relative, patterns in PUBLISHER_ASSIGNMENTS.items():
        text = read(relative)
        if "std::bit_cast<float>" in text:
            failures.append(f"{relative} must not bit-pack public scalar flag fields")
        if "packed uint bitfield" in text:
            failures.append(f"{relative} still describes public scalar flag fields as packed")
        assert_patterns(failures, relative, patterns)

    cb_text = read("shaders/SkyrimBridge_CB.fxh")
    for param_name, comment_pattern in CB_SCALAR_COMMENTS.items():
        declaration_pattern = (
            rf"float4\s+{param_name}\b[^\n]*//\s*{comment_pattern}"
        )
        if not re.search(declaration_pattern, cb_text):
            failures.append(f"SkyrimBridge_CB.fxh has stale comment for {param_name}")

    for token in FORBIDDEN_SCALAR_PACKING_TOKENS:
        if token in cb_text:
            failures.append(f"SkyrimBridge_CB.fxh still exposes packed scalar-flag token: {token}")

    for pattern in CB_HELPERS:
        if not re.search(pattern, cb_text):
            failures.append(f"SkyrimBridge_CB.fxh missing scalar helper: {pattern}")

    for line in failures:
        print(f"FAIL: {line}")
    if not failures:
        print("validate_scalar_flag_contract: all cases passed")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
