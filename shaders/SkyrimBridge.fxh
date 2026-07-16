#ifndef SKYRIMBRIDGE_FXH
#define SKYRIMBRIDGE_FXH
//=============================================================================
//  SkyrimBridge.fxh v2.0 — HLSL API for SkyrimBridge External Parameters
//
//  Include this file in any ENB shader (.fx) that needs SkyrimBridge data.
//  Place the #include AFTER the ENB built-in parameter block (Timer, ScreenSize,
//  etc.) and BEFORE your shader code.
//
//  ╔═══════════════════════════════════════════════════════════════════════╗
//  ║  CRITICAL: PARAMETER RETENTION                                      ║
//  ║                                                                     ║
//  ║  ENB compiles each .fx file as a D3D Effect. The HLSL compiler     ║
//  ║  dead-strips any global variable not referenced by a pixel shader   ║
//  ║  entry point. Dead-stripped variables vanish from the compiled      ║
//  ║  constant buffer, causing ENBSetParameter() to silently fail.      ║
//  ║                                                                     ║
//  ║  This header uses a KeepAlive sink that touches every parameter    ║
//  ║  through a runtime-dependent path the compiler cannot eliminate.    ║
//  ║  Every shader MUST call SB_Retain() in at least one pixel shader:  ║
//  ║                                                                     ║
//  ║    color.rgb += SB_Retain(uv);  // zero-cost, adds 0.0            ║
//  ║                                                                     ║
//  ║  Without this call, SkyrimBridge data WILL NOT reach the shader.   ║
//  ╚═══════════════════════════════════════════════════════════════════════╝
//
//  Author: Zain Dana Harper
//  Version: 2.0.0
//  Techniques: ReforgedUI (TheSandvichMaker), kingeric1992, Adyss, TreyM,
//              l00ping — improved and unified
//=============================================================================


//─────────────────────────────────────────────────────────────────────────────
//  CONFIGURATION
//─────────────────────────────────────────────────────────────────────────────

// Set to 1 to enable SkyrimBridge debug readout in the ENB GUI.
// When enabled, key parameters are exposed as read-only spinners
// under a "SkyrimBridge Monitor" section in the ENB shader editor.
#ifndef SB_ENABLE_MONITOR
#define SB_ENABLE_MONITOR  0
#endif

// Set to 1 to include the full helper function library.
// Disable if you only need raw parameter access (saves ~2KB compiled).
#ifndef SB_ENABLE_HELPERS
#define SB_ENABLE_HELPERS  1
#endif


//═════════════════════════════════════════════════════════════════════════════
//
//  SECTION 1: EXTERNAL PARAMETER DECLARATIONS
//
//  These float4 variables are populated every frame by the SkyrimBridge
//  SKSE plugin via ENBSetParameter(). Without the DLL installed, all
//  values default to 0 — shaders must handle graceful fallback.
//
//  Naming convention:   SB_{Domain}_{Specific}
//  Packing convention:  .xyz = spatial/color data, .w = scalar/flag
//
//═════════════════════════════════════════════════════════════════════════════


//─────────────────────────────────────────────────────────────────────────────
//  1. CELESTIAL — Sun, moons, time of day
//─────────────────────────────────────────────────────────────────────────────

float4 SB_Sun_NDC;             // .xy = NDC [-1,1], .z = onScreen (0/1), .w = elevation (rad)
float4 SB_Sun_Direction;       // .xyz = normalized world dir, .w = elevation (rad)
float4 SB_Sun_Color;           // .rgb = weather sunlight color, .a = sun glare factor

float4 SB_Masser_NDC;          // .xy = NDC pos, .z = onScreen (0/1), .w = phase brightness [0,1]
float4 SB_Masser_Direction;    // .xyz = normalized world dir, .w = elevation (rad)

float4 SB_Secunda_NDC;         // .xy = NDC pos, .z = onScreen (0/1), .w = phase brightness [0,1]
float4 SB_Secunda_Direction;   // .xyz = normalized world dir, .w = elevation (rad)

float4 SB_Time;                // .x = gameHour [0,24), .y = sunriseHour, .z = sunsetHour,
                                // .w = dayProgress [0,1]


//─────────────────────────────────────────────────────────────────────────────
//  2. ATMOSPHERE — Sky gradient, ambient, directional lighting
//─────────────────────────────────────────────────────────────────────────────

float4 SB_Atmos_SkyUpper;      // .rgb = upper sky gradient
float4 SB_Atmos_SkyLower;      // .rgb = lower sky gradient
float4 SB_Atmos_Horizon;       // .rgb = horizon band color
float4 SB_Atmos_Ambient;       // .rgb = ambient light color, .a = intensity
float4 SB_Atmos_Sunlight;      // .rgb = directional sunlight, .a = sunlight scale
float4 SB_Atmos_CloudDiffuse;  // .rgb = cloud LOD diffuse tint
float4 SB_Atmos_CloudAmbient;  // .rgb = cloud LOD ambient tint
float4 SB_Atmos_EffectLight;   // .rgb = magic/effect lighting color


//─────────────────────────────────────────────────────────────────────────────
//  3. FOG — Near/far fog, height fog, density curves
//─────────────────────────────────────────────────────────────────────────────

float4 SB_Fog_NearColor;       // .rgb = near fog color, .a = near distance
float4 SB_Fog_FarColor;        // .rgb = far fog color,  .a = far distance
float4 SB_Fog_Density;         // .x = power curve, .y = maxOpacity,
                                // .z = isInterior (0/1)
float4 SB_Fog_Height;          // .x = waterSurfaceZ, .y = playerAltitude,
                                // .z = seaLevelDensity, .w = falloffRate


//─────────────────────────────────────────────────────────────────────────────
//  4. WEATHER — Wind, precipitation, lightning, weather classification
//─────────────────────────────────────────────────────────────────────────────

float4 SB_Wind;                // .x = speed [0,1], .y = direction (rad)
float4 SB_Precipitation;       // .x = type (0=none,1=rain,2=snow),
                                // .y = intensity [0,1]
float4 SB_Lightning;           // .x = frequency, .y = isFlashing (0/1),
                                // .z = flashIntensity, .w = timeSinceFlash
float4 SB_Weather_Flags;       // .x = isPleasant, .y = isCloudy,
                                // .z = isRainy, .w = isSnowy
float4 SB_Weather_Transition;  // .x = transition% [0,1], .y = outgoingID,
                                // .z = currentID
float4 SB_Precip_Surface;      // .x = surface wetness [0,1], .y = puddle depth,
                                // .z = snow accumulation


//─────────────────────────────────────────────────────────────────────────────
//  5. PLAYER — Position, vitals, movement, combat, water state
//─────────────────────────────────────────────────────────────────────────────

float4 SB_Player_Position;     // .xyz = world pos, .w = altitude above water
float4 SB_Player_Vitals;       // .x = health%, .y = stamina%, .z = magicka%, .w = level
float4 SB_Player_Movement;     // .x = speed (units/s), .y = sprinting,
                                // .z = swimming, .w = mounted
float4 SB_Player_Combat;       // .x = inCombat, .y = bleedout,
                                // .z = killcam, .w = weaponDrawn
float4 SB_Player_Water;        // .x = underwater, .y = waterSurfaceZ,
                                // .z = submersionDepth, .w = wading


//─────────────────────────────────────────────────────────────────────────────
//  6. CAMERA — FOV, matrices, view state
//─────────────────────────────────────────────────────────────────────────────

float4 SB_Camera_Info;         // .x = FOV (deg), .y = nearClip, .z = farClip,
                                // .w = aspectRatio
float4 SB_Camera_Angles;       // .x = pitch (rad), .y = yaw (rad),
                                // .z = cameraStateEnum
float4 SB_Camera_WorldPos;     // .xyz = camera world position

// View matrix (world → camera), row-major
float4 SB_View_Row0;
float4 SB_View_Row1;
float4 SB_View_Row2;
float4 SB_View_Row3;

// Projection matrix
float4 SB_Proj_Row0;
float4 SB_Proj_Row1;
float4 SB_Proj_Row2;
float4 SB_Proj_Row3;

// Combined View*Projection
float4 SB_ViewProj_Row0;
float4 SB_ViewProj_Row1;
float4 SB_ViewProj_Row2;
float4 SB_ViewProj_Row3;

// Previous frame View*Projection (temporal reprojection / motion vectors)
float4 SB_PrevVP_Row0;
float4 SB_PrevVP_Row1;
float4 SB_PrevVP_Row2;
float4 SB_PrevVP_Row3;

// Inverse View*Projection (world position reconstruction from depth)
float4 SB_InvVP_Row0;
float4 SB_InvVP_Row1;
float4 SB_InvVP_Row2;
float4 SB_InvVP_Row3;


//─────────────────────────────────────────────────────────────────────────────
//  7. INTERIOR LIGHTING — Cell lighting template data
//─────────────────────────────────────────────────────────────────────────────

float4 SB_Interior_Flags;      // .x = isInterior (0/1), .y = hasLightingTemplate
float4 SB_Interior_Ambient;    // .rgb = ambient, .a = intensity
float4 SB_Interior_DirColor;   // .rgb = directional light, .a = fade
float4 SB_Interior_DirDir;     // .xyz = light direction
float4 SB_Interior_FogColor;   // .rgb = interior fog color
float4 SB_Interior_FogDist;    // .x = near, .y = far, .z = power, .w = clipDist


//─────────────────────────────────────────────────────────────────────────────
//  8. SHADOW / DIRECTIONAL LIGHT — Scene graph shadow caster
//─────────────────────────────────────────────────────────────────────────────

float4 SB_Shadow_Direction;    // .xyz = shadow caster dir (world), .w = shadow intensity
float4 SB_Shadow_Diffuse;      // .rgb = shadow caster diffuse color
float4 SB_Shadow_Ambient;      // .rgb = shadow caster ambient color


//─────────────────────────────────────────────────────────────────────────────
//  9. ACTIVE MAGIC EFFECTS — Vision, time, damage, misc
//─────────────────────────────────────────────────────────────────────────────

float4 SB_FX_Vision;           // .x = nightEye, .y = detectLife,
                                // .z = detectDead, .w = ethereal
float4 SB_FX_Time;             // .x = slowTimeFactor, .y = timeStopped
float4 SB_FX_Damage;           // .x = fire, .y = frost, .z = shock, .w = poison
float4 SB_FX_Misc;             // .x = invisible, .y = paralyzed, .z = drunk


//─────────────────────────────────────────────────────────────────────────────
//  10. RENDER STATE — Frame counter, timing, TAA jitter
//─────────────────────────────────────────────────────────────────────────────

float4 SB_Render_Frame;        // .x = frameCount (wraps at 2^20),
                                // .y = deltaTime (sec), .z = screenW, .w = screenH
float4 SB_Render_Jitter;       // .xy = TAA jitter (NDC), .z = frameIndex%16


//─────────────────────────────────────────────────────────────────────────────
//  11. IMAGE SPACE — Game's own post-processing state (IMODs)
//─────────────────────────────────────────────────────────────────────────────

float4 SB_IS_HDR;              // .x = eyeAdaptSpeed, .y = bloomScale,
                                // .z = bloomThreshold, .w = sunlightScale
float4 SB_IS_Cinematic;        // .x = saturation, .y = brightness,
                                // .z = contrast, .w = tintAlpha
float4 SB_IS_CineTint;         // .rgb = cinematic tint color
float4 SB_IS_DOF;              // .x = strength, .y = distance,
                                // .z = range, .w = vignetteRadius
float4 SB_IS_IMOD;             // .x = hasActiveIMOD(0/1), .y = imodStrength,
                                // .z = imodFadeIn, .w = imodElapsed
float4 SB_IS_IMODTint;         // .rgb = IMOD tint color, .a = blur amount


//─────────────────────────────────────────────────────────────────────────────
//  12. NEARBY LIGHTS — 3 nearest point/spot lights
//─────────────────────────────────────────────────────────────────────────────

float4 SB_Light0_PosRad;       // .xyz = world position, .w = radius
float4 SB_Light0_Color;        // .rgb = color, .a = intensity
float4 SB_Light1_PosRad;
float4 SB_Light1_Color;
float4 SB_Light2_PosRad;
float4 SB_Light2_Color;
float4 SB_Light_Summary;       // .x = total nearby count, .y = nearest distance,
                                // .z = total luminous flux, .w = dominant hue [0,1]


//─────────────────────────────────────────────────────────────────────────────
//  13. EXTENDED ACTOR VALUES — Resistances, combat stats, skills
//─────────────────────────────────────────────────────────────────────────────

float4 SB_AV_Resist;           // .x = fireResist%, .y = frostResist%,
                                // .z = shockResist%, .w = magicResist%
float4 SB_AV_Resist2;          // .x = poisonResist%, .y = diseaseResist%,
                                // .z = damageResist (armor)
float4 SB_AV_Combat;           // .x = attackDamageMult, .y = weaponSpeedMult,
                                // .z = critChance, .w = unarmedDmg
float4 SB_AV_Movement;         // .x = speedMult, .y = carryWeight,
                                // .z = inventoryWeight, .w = encumbranceRatio
float4 SB_AV_SkillCombat;      // .x = oneHanded, .y = twoHanded,
                                // .z = archery, .w = block
float4 SB_AV_SkillMagic;       // .x = alteration, .y = conjuration,
                                // .z = destruction, .w = illusion
float4 SB_AV_SkillMagic2;      // .x = restoration, .y = enchanting, .z = alchemy
float4 SB_AV_SkillStealth;     // .x = lightArmor, .y = sneak,
                                // .z = lockpicking, .w = pickpocket


//─────────────────────────────────────────────────────────────────────────────
//  14. CROSSHAIR / LOOK-AT TARGET
//─────────────────────────────────────────────────────────────────────────────

float4 SB_XHair_Info;          // .x = hasTarget(0/1), .y = distance,
                                // .z = formType (enum), .w = isActor(0/1)
float4 SB_XHair_Pos;           // .xyz = target world position, .w = boundingRadius
float4 SB_XHair_Actor;         // .x = healthPct, .y = level,
                                // .z = isHostile(0/1), .w = isEssential(0/1)


//─────────────────────────────────────────────────────────────────────────────
//  15. EQUIPMENT — Weapons, armor, torch state
//─────────────────────────────────────────────────────────────────────────────

float4 SB_Equip_Right;         // .x = weaponType, .y = baseDamage,
                                // .z = isEnchanted(0/1), .w = enchantCharge [0,1]
float4 SB_Equip_Left;          // .x = itemType, .y = damage/armorRating,
                                // .z = isEnchanted(0/1), .w = isSpell(0/1)
float4 SB_Equip_Armor;         // .x = totalArmorRating, .y = isWearingHeavy(0/1),
                                // .z = isWearingLight(0/1), .w = isWearingRobes(0/1)
float4 SB_Equip_Flags;         // .x = weaponDrawn(0/1), .y = hasBow(0/1),
                                // .z = hasTorch(0/1), .w = isTwoHanding(0/1)


//─────────────────────────────────────────────────────────────────────────────
//  16. QUEST STATE
//─────────────────────────────────────────────────────────────────────────────

float4 SB_Quest_Progress;      // .x = mainQuestStage, .y = totalQuestsCompleted,
                                // .z = activeQuestCount, .w = activeObjectiveCount
float4 SB_Quest_Tracked;       // .x = trackedQuestStage, .y = questType,
                                // .z = questFormID (low 16 bits),
                                // .w = hasObjectiveMarker(0/1)


//─────────────────────────────────────────────────────────────────────────────
//  17. UI / MENU STATE
//─────────────────────────────────────────────────────────────────────────────

float4 SB_UI_Menus;            // .x = isInMenu(0/1), .y = isInDialogue(0/1),
                                // .z = isInInventory(0/1), .w = isInMap(0/1)
float4 SB_UI_HUD;              // .x = isHUDVisible(0/1), .y = isCrosshairVisible(0/1),
                                // .z = isInCinematicMode(0/1), .w = isLoading(0/1)
float4 SB_UI_Detail;           // .x = isInCrafting(0/1), .y = isInBook(0/1),
                                // .z = isInLockpick(0/1), .w = isInConsole(0/1)


//═════════════════════════════════════════════════════════════════════════════
//
//  SECTION 2: PARAMETER RETENTION — THE KEEPALIVE SYSTEM
//
//  The HLSL compiler aggressively dead-strips global variables that are
//  not referenced by any compiled shader function. Since a typical shader
//  only uses ~5-10 of the 103 SB_ parameters, the other ~95 get removed
//  from the constant buffer. ENBSetParameter then can't find them.
//
//  The KeepAlive system creates a dependency chain that references every
//  single parameter through a path the compiler cannot prove is dead.
//  The trick: Timer.x is a runtime value (ENB's frame timer), so the
//  compiler cannot constant-fold the branch away.
//
//  Cost: ZERO at runtime. The branch is never taken (Timer.x is always
//  positive), so the GPU never executes the parameter reads. But the
//  compiler must keep the variables in the constant buffer because it
//  cannot prove the branch is unreachable.
//
//═════════════════════════════════════════════════════════════════════════════

// Internal: accumulate ALL parameters into a single float4 sink.
// This function exists solely to create references that prevent dead-stripping.
// It is NEVER executed at runtime (guarded by impossible branch in SB_Retain).
float4 _SB_KeepAlive_Sink()
{
    float4 s = 0;

    // 1. Celestial (8 params)
    s += SB_Sun_NDC;
    s += SB_Sun_Direction;
    s += SB_Sun_Color;
    s += SB_Masser_NDC;
    s += SB_Masser_Direction;
    s += SB_Secunda_NDC;
    s += SB_Secunda_Direction;
    s += SB_Time;

    // 2. Atmosphere (8 params)
    s += SB_Atmos_SkyUpper;
    s += SB_Atmos_SkyLower;
    s += SB_Atmos_Horizon;
    s += SB_Atmos_Ambient;
    s += SB_Atmos_Sunlight;
    s += SB_Atmos_CloudDiffuse;
    s += SB_Atmos_CloudAmbient;
    s += SB_Atmos_EffectLight;

    // 3. Fog (4 params)
    s += SB_Fog_NearColor;
    s += SB_Fog_FarColor;
    s += SB_Fog_Density;
    s += SB_Fog_Height;

    // 4. Weather (6 params)
    s += SB_Wind;
    s += SB_Precipitation;
    s += SB_Lightning;
    s += SB_Weather_Flags;
    s += SB_Weather_Transition;
    s += SB_Precip_Surface;

    // 5. Player (5 params)
    s += SB_Player_Position;
    s += SB_Player_Vitals;
    s += SB_Player_Movement;
    s += SB_Player_Combat;
    s += SB_Player_Water;

    // 6. Camera (23 params: 3 info + 20 matrix rows)
    s += SB_Camera_Info;
    s += SB_Camera_Angles;
    s += SB_Camera_WorldPos;
    s += SB_View_Row0;
    s += SB_View_Row1;
    s += SB_View_Row2;
    s += SB_View_Row3;
    s += SB_Proj_Row0;
    s += SB_Proj_Row1;
    s += SB_Proj_Row2;
    s += SB_Proj_Row3;
    s += SB_ViewProj_Row0;
    s += SB_ViewProj_Row1;
    s += SB_ViewProj_Row2;
    s += SB_ViewProj_Row3;
    s += SB_PrevVP_Row0;
    s += SB_PrevVP_Row1;
    s += SB_PrevVP_Row2;
    s += SB_PrevVP_Row3;
    s += SB_InvVP_Row0;
    s += SB_InvVP_Row1;
    s += SB_InvVP_Row2;
    s += SB_InvVP_Row3;

    // 7. Interior (6 params)
    s += SB_Interior_Flags;
    s += SB_Interior_Ambient;
    s += SB_Interior_DirColor;
    s += SB_Interior_DirDir;
    s += SB_Interior_FogColor;
    s += SB_Interior_FogDist;

    // 8. Shadow (3 params)
    s += SB_Shadow_Direction;
    s += SB_Shadow_Diffuse;
    s += SB_Shadow_Ambient;

    // 9. Magic FX (4 params)
    s += SB_FX_Vision;
    s += SB_FX_Time;
    s += SB_FX_Damage;
    s += SB_FX_Misc;

    // 10. Render (2 params)
    s += SB_Render_Frame;
    s += SB_Render_Jitter;

    // 11. ImageSpace (6 params)
    s += SB_IS_HDR;
    s += SB_IS_Cinematic;
    s += SB_IS_CineTint;
    s += SB_IS_DOF;
    s += SB_IS_IMOD;
    s += SB_IS_IMODTint;

    // 12. Nearby Lights (7 params)
    s += SB_Light0_PosRad;
    s += SB_Light0_Color;
    s += SB_Light1_PosRad;
    s += SB_Light1_Color;
    s += SB_Light2_PosRad;
    s += SB_Light2_Color;
    s += SB_Light_Summary;

    // 13. Actor Values (8 params)
    s += SB_AV_Resist;
    s += SB_AV_Resist2;
    s += SB_AV_Combat;
    s += SB_AV_Movement;
    s += SB_AV_SkillCombat;
    s += SB_AV_SkillMagic;
    s += SB_AV_SkillMagic2;
    s += SB_AV_SkillStealth;

    // 14. Crosshair (3 params)
    s += SB_XHair_Info;
    s += SB_XHair_Pos;
    s += SB_XHair_Actor;

    // 15. Equipment (4 params)
    s += SB_Equip_Right;
    s += SB_Equip_Left;
    s += SB_Equip_Armor;
    s += SB_Equip_Flags;

    // 16. Quest (2 params)
    s += SB_Quest_Progress;
    s += SB_Quest_Tracked;

    // 17. UI (3 params)
    s += SB_UI_Menus;
    s += SB_UI_HUD;
    s += SB_UI_Detail;

    return s;
    // Total: 102 float4 parameters retained
}


//─────────────────────────────────────────────────────────────────────────────
//  SB_Retain() — Call this in every pixel shader that needs SkyrimBridge data.
//
//  Returns float3(0,0,0) at runtime (zero visual impact).
//  At compile time, creates the dependency chain that prevents dead-stripping.
//
//  Usage:
//    float4 PS_MyShader(VS_OUTPUT i) : SV_Target
//    {
//        float3 color = ...;
//        color += SB_Retain(i.uv);  // Must be BEFORE return
//        return float4(color, 1);
//    }
//
//  The UV parameter creates a data-dependent path through the KeepAlive
//  sink that the compiler provably cannot eliminate, while the Timer guard
//  ensures the GPU never actually executes the parameter reads.
//─────────────────────────────────────────────────────────────────────────────

float3 SB_Retain(float2 uv)
{
    // Timer.x is ENB's generic timer — always positive at runtime.
    // The compiler sees a runtime-dependent branch and must keep both paths.
    // The GPU predicts/takes the else branch (return 0) every frame.
    //
    // The uv multiply prevents the compiler from hoisting the sink
    // out of the branch (makes the result depend on per-pixel input).
    [branch] if (Timer.x < -1.0e15)
    {
        float4 sink = _SB_KeepAlive_Sink();
        return sink.rgb * uv.x * 0.0001;
    }
    return 0;
}


//═════════════════════════════════════════════════════════════════════════════
//
//  SECTION 3: MATRIX RECONSTRUCTION HELPERS
//
//═════════════════════════════════════════════════════════════════════════════

#if SB_ENABLE_HELPERS

float4x4 SB_GetViewMatrix()
{
    return float4x4(SB_View_Row0, SB_View_Row1, SB_View_Row2, SB_View_Row3);
}

float4x4 SB_GetProjMatrix()
{
    return float4x4(SB_Proj_Row0, SB_Proj_Row1, SB_Proj_Row2, SB_Proj_Row3);
}

float4x4 SB_GetViewProjMatrix()
{
    return float4x4(SB_ViewProj_Row0, SB_ViewProj_Row1,
                    SB_ViewProj_Row2, SB_ViewProj_Row3);
}

float4x4 SB_GetPrevViewProjMatrix()
{
    return float4x4(SB_PrevVP_Row0, SB_PrevVP_Row1,
                    SB_PrevVP_Row2, SB_PrevVP_Row3);
}

float4x4 SB_GetInvViewProjMatrix()
{
    return float4x4(SB_InvVP_Row0, SB_InvVP_Row1,
                    SB_InvVP_Row2, SB_InvVP_Row3);
}


//═════════════════════════════════════════════════════════════════════════════
//
//  SECTION 4: SPATIAL RECONSTRUCTION
//
//═════════════════════════════════════════════════════════════════════════════

//  Reconstruct world position from UV + hardware depth
float3 SB_WorldPosFromDepth(float2 uv, float depth)
{
    float4 ndc = float4(uv * 2.0 - 1.0, depth, 1.0);
    ndc.y = -ndc.y;  // D3D UV convention → NDC
    float4 world = mul(ndc, SB_GetInvViewProjMatrix());
    return world.xyz / world.w;
}

//  Reconstruct view-space direction from screen UV
float3 SB_ViewDirFromUV(float2 uv)
{
    float tanHalfFov = tan(radians(SB_Camera_Info.x) * 0.5);
    float aspect = SB_Camera_Info.w;
    float2 ndc = uv * 2.0 - 1.0;
    return normalize(float3(ndc.x * tanHalfFov * aspect,
                           -ndc.y * tanHalfFov,
                           -1.0));
}

//  Linearize depth using camera near/far from SkyrimBridge
//  (More accurate than hardcoded far plane)
float SB_LinearizeDepth(float rawDepth)
{
    float n = SB_Camera_Info.y;
    float f = SB_Camera_Info.z;
    return n * f / (f - rawDepth * (f - n));
}

//  Compute per-pixel motion vector from world position
float2 SB_MotionVector(float3 worldPos, float2 currentUV)
{
    float4 prevClip = mul(float4(worldPos, 1.0), SB_GetPrevViewProjMatrix());
    float2 prevUV = prevClip.xy / prevClip.w * float2(0.5, -0.5) + 0.5;
    return currentUV - prevUV;
}


//═════════════════════════════════════════════════════════════════════════════
//
//  SECTION 5: CELESTIAL HELPERS
//
//═════════════════════════════════════════════════════════════════════════════

//  Sun screen-space UV (compatible with ENB's LightParameters pattern)
float2 SB_SunScreenUV()
{
    return SB_Sun_NDC.xy * float2(0.5, -0.5) + 0.5;
}

bool SB_IsSunOnScreen()
{
    return SB_Sun_NDC.z > 0.5;
}

bool SB_IsSunAboveHorizon()
{
    return SB_Sun_NDC.w > 0.0;
}

//  Is it nighttime? (sun below horizon)
bool SB_IsNight()
{
    return SB_Time.x < SB_Time.y || SB_Time.x > SB_Time.z;
}

//  Moon visibility helpers (for lens flares, volumetric moonlight)
bool SB_IsMasserOnScreen()   { return SB_Masser_NDC.z > 0.5; }
bool SB_IsSecundaOnScreen()  { return SB_Secunda_NDC.z > 0.5; }
float SB_MasserBrightness()  { return SB_Masser_NDC.w; }
float SB_SecundaBrightness() { return SB_Secunda_NDC.w; }

float2 SB_MasserScreenUV()
{
    return SB_Masser_NDC.xy * float2(0.5, -0.5) + 0.5;
}

float2 SB_SecundaScreenUV()
{
    return SB_Secunda_NDC.xy * float2(0.5, -0.5) + 0.5;
}


//═════════════════════════════════════════════════════════════════════════════
//
//  SECTION 6: WEATHER & ENVIRONMENT HELPERS
//
//═════════════════════════════════════════════════════════════════════════════

bool SB_IsRaining()    { return SB_Weather_Flags.z > 0.5; }
bool SB_IsSnowing()    { return SB_Weather_Flags.w > 0.5; }
bool SB_IsStorming()   { return SB_Lightning.x > 0.1; }
bool SB_IsFlashing()   { return SB_Lightning.y > 0.5; }

//  Wind vector in XZ plane (for fog scrolling, particle direction)
float2 SB_GetWindVector()
{
    float spd = SB_Wind.x;
    float dir = SB_Wind.y;
    return float2(cos(dir), sin(dir)) * spd;
}

//  Fog color interpolated by distance (matches game's linear blend)
float3 SB_GetFogColor(float distance)
{
    float nearDist = SB_Fog_NearColor.a;
    float farDist  = SB_Fog_FarColor.a;
    float t = saturate((distance - nearDist) / max(farDist - nearDist, 0.001));
    return lerp(SB_Fog_NearColor.rgb, SB_Fog_FarColor.rgb, t);
}

//  Height fog density at a world-space Y position
float SB_HeightFogDensity(float worldY)
{
    float seaLevel = SB_Fog_Height.x;
    float density  = SB_Fog_Height.z;
    float falloff  = SB_Fog_Height.w;
    return density * exp(-max(worldY - seaLevel, 0.0) * falloff);
}


//═════════════════════════════════════════════════════════════════════════════
//
//  SECTION 7: PLAYER STATE HELPERS
//
//═════════════════════════════════════════════════════════════════════════════

float SB_GetHealth()         { return SB_Player_Vitals.x; }
float SB_GetStamina()        { return SB_Player_Vitals.y; }
float SB_GetMagicka()        { return SB_Player_Vitals.z; }
bool  SB_IsInCombat()        { return SB_Player_Combat.x > 0.5; }
bool  SB_IsInBleedout()      { return SB_Player_Combat.y > 0.5; }
bool  SB_IsInKillcam()       { return SB_Player_Combat.z > 0.5; }
bool  SB_IsUnderwater()      { return SB_Player_Water.x > 0.5; }
bool  SB_IsSprinting()       { return SB_Player_Movement.y > 0.5; }
bool  SB_IsSwimming()        { return SB_Player_Movement.z > 0.5; }
bool  SB_IsMounted()         { return SB_Player_Movement.w > 0.5; }
float SB_GetPlayerSpeed()    { return SB_Player_Movement.x; }
float SB_GetSubmersionDepth(){ return SB_Player_Water.z; }


//═════════════════════════════════════════════════════════════════════════════
//
//  SECTION 8: IMAGESPACE & MAGIC EFFECT HELPERS
//
//═════════════════════════════════════════════════════════════════════════════

bool SB_HasActiveIMOD()      { return SB_IS_IMOD.x > 0.5; }
float SB_GetIMODStrength()   { return SB_IS_IMOD.y; }

float SB_GetGameSaturation() { return SB_IS_Cinematic.x; }
float SB_GetGameBrightness() { return SB_IS_Cinematic.y; }
float SB_GetGameContrast()   { return SB_IS_Cinematic.z; }

bool SB_HasNightEye()        { return SB_FX_Vision.x > 0.5; }
bool SB_HasDetectLife()      { return SB_FX_Vision.y > 0.5; }
bool SB_IsEthereal()         { return SB_FX_Vision.w > 0.5; }
bool SB_IsInvisible()        { return SB_FX_Misc.x > 0.5; }
float SB_GetSlowTimeFactor() { return SB_FX_Time.x; }


//═════════════════════════════════════════════════════════════════════════════
//
//  SECTION 8b: CONVENIENCE WRAPPERS — used by Helper/ effect modules
//
//═════════════════════════════════════════════════════════════════════════════

// Scene wetness from precipitation surface tracking
float SB_SceneWetness()       { return SB_Precip_Surface.x; }

// Combat intensity — returns 0.0 (peaceful) to 1.0 (full combat)
float SB_CombatIntensity()    { return SB_Player_Combat.x; }

// Weather transition progress — 0.0 = fully outgoing, 1.0 = fully incoming
float SB_SmoothWeatherTransition() { return SB_Weather_Transition.x; }

// Weather Parameter stub — Phase 2 (WeatherParameterComputer) not yet active
// Returns the default value for all parameters until Phase 2 is wired in.
static const float SB_WP_BloomInt   = 0.0;
static const float SB_WP_BloomRad   = 0.0;
static const float SB_WP_Saturation = 0.0;
static const float SB_WP_Contrast   = 0.0;
static const float SB_WP_ColorTemp  = 0.0;
static const float SB_WP_FrostLens  = 0.0;
float SB_GetWP(float param, float defaultVal) { return defaultVal; }


//═════════════════════════════════════════════════════════════════════════════
//
//  SECTION 9: NEARBY LIGHTS — Multi-light evaluation
//
//═════════════════════════════════════════════════════════════════════════════

//  Inverse-square-ish attenuation from a light to a world-space point
float SB_LightAttenuation(float3 worldPos, float4 lightPosRad, float4 lightColor)
{
    float3 toLight = lightPosRad.xyz - worldPos;
    float dist = length(toLight);
    float radius = lightPosRad.w;
    if (radius < 1.0 || lightColor.a < 0.001) return 0.0;
    float atten = saturate(1.0 - dist / radius);
    return atten * atten * lightColor.a;
}

//  Sum light color contribution from all 3 nearest lights
float3 SB_EvaluateNearbyLights(float3 worldPos)
{
    float3 result = 0.0;
    result += SB_Light0_Color.rgb * SB_LightAttenuation(worldPos, SB_Light0_PosRad, SB_Light0_Color);
    result += SB_Light1_Color.rgb * SB_LightAttenuation(worldPos, SB_Light1_PosRad, SB_Light1_Color);
    result += SB_Light2_Color.rgb * SB_LightAttenuation(worldPos, SB_Light2_PosRad, SB_Light2_Color);
    return result;
}

//  Project a light source to screen UV (for lens flare placement)
float4 SB_LightToScreen(float4 lightPosRad)
{
    float4x4 vp = SB_GetViewProjMatrix();
    float4 clip = mul(float4(lightPosRad.xyz, 1.0), vp);
    float3 ndc = clip.xyz / clip.w;
    float2 uv = ndc.xy * 0.5 + 0.5;
    uv.y = 1.0 - uv.y;
    return float4(uv, clip.w > 0.0 ? 1.0 : 0.0, clip.w);
}


//═════════════════════════════════════════════════════════════════════════════
//
//  SECTION 10: EQUIPMENT & CROSSHAIR HELPERS
//
//═════════════════════════════════════════════════════════════════════════════

bool  SB_HasWeaponDrawn()    { return SB_Equip_Flags.x > 0.5; }
bool  SB_HasBowEquipped()    { return SB_Equip_Flags.y > 0.5; }
bool  SB_HasTorchEquipped()  { return SB_Equip_Flags.z > 0.5; }
bool  SB_IsTwoHanding()      { return SB_Equip_Flags.w > 0.5; }
bool  SB_IsRightEnchanted()  { return SB_Equip_Right.z > 0.5; }
float SB_GetEnchantCharge()  { return SB_Equip_Right.w; }

bool SB_HasCrosshairTarget() { return SB_XHair_Info.x > 0.5; }
float SB_GetTargetDistance()  { return SB_XHair_Info.y; }
bool SB_IsTargetActor()      { return SB_XHair_Info.w > 0.5; }
bool SB_IsTargetHostile()    { return SB_XHair_Actor.z > 0.5; }
float SB_GetTargetHealthPct() { return SB_XHair_Actor.x; }

//  Auto-focus DOF: use crosshair target distance or fallback
float SB_GetAutoFocusDistance(float defaultDist)
{
    return SB_HasCrosshairTarget() ? SB_GetTargetDistance() : defaultDist;
}

//  Armor class: 0=unarmored, 1=heavy, 2=light, 3=robes
float SB_GetArmorClass()
{
    if (SB_Equip_Armor.y > 0.5) return 1.0;
    if (SB_Equip_Armor.z > 0.5) return 2.0;
    if (SB_Equip_Armor.w > 0.5) return 3.0;
    return 0.0;
}

bool SB_IsMeleeWeapon()
{
    float t = SB_Equip_Right.x;
    return (t >= 1.0 && t <= 7.0);
}

bool SB_IsRangedWeapon()
{
    float t = SB_Equip_Right.x;
    return (t == 8.0 || t == 10.0);
}

bool SB_IsCasting()
{
    return (SB_Equip_Right.x == 11.0 || SB_Equip_Left.w > 0.5);
}


//═════════════════════════════════════════════════════════════════════════════
//
//  SECTION 11: UI STATE HELPERS
//
//═════════════════════════════════════════════════════════════════════════════

bool SB_IsInMenu()           { return SB_UI_Menus.x > 0.5; }
bool SB_IsInDialogue()       { return SB_UI_Menus.y > 0.5; }
bool SB_IsLoading()          { return SB_UI_HUD.w > 0.5; }
bool SB_IsCinematicMode()    { return SB_UI_HUD.z > 0.5; }

//  Should post-processing intensity be reduced?
bool SB_ShouldReducePostFX()
{
    return SB_IsInMenu() || SB_IsLoading() || SB_UI_Detail.w > 0.5;
}

//  Progressive menu blur: 0 = no blur, 1 = full blur
float SB_GetMenuBlurAmount()
{
    if (SB_IsLoading()) return 1.0;
    if (SB_IsInMenu()) return 0.7;
    if (SB_IsInDialogue()) return 0.3;
    return 0.0;
}

float SB_GetEncumbrance()    { return SB_AV_Movement.w; }
bool SB_IsOverEncumbered()   { return SB_AV_Movement.w > 1.0; }


#endif // SB_ENABLE_HELPERS


//═════════════════════════════════════════════════════════════════════════════
//
//  SECTION 12: BRIDGE STATUS
//
//═════════════════════════════════════════════════════════════════════════════

//  Check if SkyrimBridge DLL is active (non-zero frame count)
bool SB_IsActive()
{
    return SB_Render_Frame.x > 0.0;
}

//  Version check — the DLL can set a version identifier in the frame data
//  This is always available regardless of SB_ENABLE_HELPERS
float SB_GetVersion()
{
    return SB_Render_Jitter.z;
}


#endif // SKYRIMBRIDGE_FXH
