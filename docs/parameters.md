# SkyrimBridge parameter contract

Every value is published as a hidden ENB `float4` parameter each frame and read
through `shaders/SkyrimBridge.fxh`. Names and packing are stable within a major
version, so presets can depend on them. Without the plugin installed, every
value defaults to zero and shaders should handle that gracefully.

Include the header after ENB's built-in parameter block, and call `SB_Retain(uv)`
in at least one pixel shader so the compiler cannot dead-strip the parameters.

## Domains

| Domain | Examples | Notes |
|---|---|---|
| Atmosphere | `SB_Atmos_SkyUpper`, `SB_Atmos_SkyLower`, `SB_Atmos_Horizon`, `SB_Atmos_Ambient`, `SB_Atmos_Sunlight`, `SB_Atmos_CloudDiffuse`, `SB_Atmos_CloudAmbient`, `SB_Atmos_EffectLight` | Weather colors, interpolated across the day and blended across weather transitions, exactly as the engine does. |
| Camera | `SB_Camera_WorldPos`, `SB_Camera_Angles`, `SB_Camera_Info` | World position, view basis, and projection params; shaders derive View/Proj/ViewProj. |
| Fog | `SB_Fog_NearColor`, `SB_Fog_FarColor`, `SB_Fog_Height`, `SB_Fog_Density` | Distance and height fog color and shape. |
| Sun and moon | celestial position and phase | Drives directional and night lighting. |
| Interior | interior flag and lighting context | Indoor state without depth heuristics. |
| Actor values | `SB_AV_Combat`, `SB_AV_Movement`, `SB_AV_Resist`, `SB_AV_SkillCombat`, `SB_AV_SkillMagic`, `SB_AV_SkillStealth` | The gameplay-reactive surface: drive effects from what the player is doing. |
| Combat | `SB_CombatIntensity` | Escalation signal for reactive post. |
| Equipment | `SB_Equip_Right`, `SB_Equip_Left`, `SB_Equip_Armor`, `SB_Equip_Flags` | Worn and wielded state. |
| Effects | `SB_FX_Damage`, `SB_FX_Vision`, `SB_FX_Time`, `SB_FX_Misc` | Damage, vision, and time-based feedback. |

Helper functions in the header (for example `SB_GetFogColor`, `SB_GetGameBrightness`,
`SB_GetGameContrast`, `SB_EvaluateNearbyLights`) wrap the raw parameters into
ready-to-use values.

## Versioning

The `SB_*` names and their packing form the public contract. Additive changes
bump the minor version; any rename or repack bumps the major version, so a
preset built against a major version keeps working across minor updates.

## The measurement-oracle role

Because SkyrimBridge measures engine state directly through SKSE, its values are
a live ground truth. A second tool that reads the same state a different way (for
example a native Address Library bridge) can be cross-checked against
SkyrimBridge frame by frame, turning "the offset is probably right" into a
witnessed comparison.
