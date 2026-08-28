#pragma once
//=============================================================================
//  BridgeData.h — The data contract between SkyrimBridge and ENB shaders
//
//  Every parameter pushed to ENB is defined here as a named float4.
//  The HLSL mirror is in shaders/SkyrimBridge.fxh.
//
//  NAMING: SB_ prefix avoids collision with ENB/game parameters.
//  PACKING: One float4 per semantic group.
//  The authoritative param count is kParamCount (BridgeData.cpp); the
//  first-push log line reports it live. 136 float4s across 25 domains
//  as of the DerivedData addition.
//=============================================================================

#include <cstdint>
#include <cstddef>

namespace SB
{
    // ── Utility ─────────────────────────────────────────────────────────
    struct Float4 {
        float x = 0.f, y = 0.f, z = 0.f, w = 0.f;
    };

    struct Float4x4 {
        Float4 row[4];
    };

    // ── 1. CELESTIAL ────────────────────────────────────────────────────
    // NDC positions removed — derivable in shader from direction + VP matrix.
    // Phase brightness packed into direction .w.

    struct CelestialData
    {
        Float4 SunDirection;    // .xyz = normalized world dir, .w = elevation angle (rad)
        Float4 SunColor;        // .rgb = weather sunlight color, .a = sun glare factor

        Float4 MasserDirection; // .xyz = normalized world dir, .w = phase brightness [0,1]
        Float4 SecundaDirection;// .xyz = normalized world dir, .w = phase brightness [0,1]

        Float4 TimeData;        // .x = gameHour [0,24), .y = sunriseHour, .z = sunsetHour, .w = dayProgress [0,1]

        Float4 TimeSegments1;   // .x = dawn [0,1], .y = sunrise [0,1], .z = day [0,1], .w = sunset [0,1]
        Float4 TimeSegments2;   // .x = dusk [0,1], .y = night [0,1], .z = goldenHour [0,1], .w = blueHour [0,1]
    };

    // ── 2. ATMOSPHERE & SKY COLORS ──────────────────────────────────────

    struct AtmosphereData
    {
        Float4 SkyUpper;        // .rgb = upper sky gradient, .a = 0
        Float4 SkyLower;        // .rgb = lower sky gradient, .a = 0
        Float4 Horizon;         // .rgb = horizon band color, .a = 0
        Float4 Ambient;         // .rgb = ambient light color, .a = ambient intensity (normalized)
        Float4 SunlightColor;   // .rgb = directional sunlight color, .a = sunlight scale
        Float4 CloudLODDiffuse; // .rgb = cloud LOD diffuse tint, .a = 0
        Float4 CloudLODAmbient; // .rgb = cloud LOD ambient tint, .a = 0
        Float4 EffectLighting;  // .rgb = magic/effect lighting color, .a = 0
    };

    // ── 3. FOG ──────────────────────────────────────────────────────────

    struct FogData
    {
        Float4 NearColor;       // .rgb = near fog color, .a = near distance
        Float4 FarColor;        // .rgb = far fog color,  .a = far distance
        Float4 Density;         // .x = power curve, .y = maxOpacity [0,1], .z = isInteriorFog(0/1), .w = 0
        Float4 HeightFog;       // .x = waterSurfaceZ, .y = playerAltitude, .z = seaLevelDensity, .w = falloffRate
    };

    // ── 4. WEATHER ──────────────────────────────────────────────────────

    struct WeatherData
    {
        Float4 Wind;            // .x = speed [0,1], .y = direction (radians), .z = 0, .w = 0
        Float4 Precipitation;   // .x = type (0=none,1=rain,2=snow), .y = intensity [0,1], .z = 0, .w = 0
        Float4 Lightning;       // .x = frequency, .y = isFlashing(0/1), .z = flashIntensity, .w = timeSinceFlash(sec)
        Float4 Flags;           // .x = isPleasant, .y = isCloudy, .z = isRainy, .w = isSnowy
        Float4 Transition;      // .x = transition% [0,1], .y = outgoingWeatherID, .z = currentWeatherID, .w = 0
        Float4 PrecipSurface;   // .x = surface wetness [0,1], .y = puddle depth, .z = snow accumulation, .w = 0

        // Tier A: Live Sky singleton data (replaces TESWeather approximations)
        Float4 WindLive;        // .x = Sky::windSpeed, .y = Sky::windAngle (rad), .z = windX (cos), .w = windZ (sin)
        Float4 PrecipLive;      // .x = Precipitation::currentParticleDensity, .y = lastParticleDensity, .z = Sky::flash, .w = Sky::currentGameHour
        Float4 CloudCover;      // .x = avgCloudAlpha, .y = numActiveLayers, .z = maxLayerAlpha, .w = Sky::currentWeatherPct
        Float4 AuroraFade;      // .x = auroraIn, .y = auroraOut, .z = auroraInStart, .w = auroraOutStart
    };

    // ── 5. PLAYER ───────────────────────────────────────────────────────

    struct PlayerData
    {
        Float4 Position;        // .xyz = world position, .w = altitude above sea level
        Float4 Vitals;          // .x = health%, .y = stamina%, .z = magicka%, .w = level (raw)
        Float4 Movement;        // .x = speed (units/s), .y = isSprinting, .z = isSwimming, .w = isRiding
        Float4 Combat;          // .x = packed bitfield (b0=combat,b1=bleedout,b2=killcam,b3=weaponDrawn), .y = beastForm(0/1/2), .z = timeScale, .w = combatTargetCount
        Float4 Water;           // .x = isUnderwater(0/1), .y = waterSurfaceZ, .z = submersionDepth, .w = isWading(0/1)
    };

    // ── 6. CAMERA ───────────────────────────────────────────────────────
    // Optimized per Marty McFly's feedback: pass minimal data, derive the rest.
    // Proj, ViewProj, InvViewProj, PrevViewProj → all derivable in shader.
    // View 4th row derivable from worldPos + rotation. Angles derivable from View.
    // FOV stored in RADIANS (not degrees) per shader convention.

    struct CameraData
    {
        Float4 Params;          // .x = FOV (radians!), .y = nearClip, .z = farClip, .w = aspectRatio
        Float4 WorldPos;        // .xyz = camera world position, .w = cameraStateEnum

        Float4 ViewRow0;        // .xyz = view rotation row 0 (right), .w = 0
        Float4 ViewRow1;        // .xyz = view rotation row 1 (up), .w = 0
        Float4 ViewRow2;        // .xyz = view rotation row 2 (forward), .w = 0

        Float4 PrevWorldPos;    // .xyz = previous frame camera position, .w = prev FOV (rad)
        Float4 PrevViewRow0;    // .xyz = previous view row 0, .w = 0
        Float4 PrevViewRow1;    // .xyz = previous view row 1, .w = 0
    };

    // ── 7. INTERIOR LIGHTING ────────────────────────────────────────────

    struct InteriorData
    {
        Float4 IsInterior;      // .x = isInterior(0/1), .y = hasLightingTemplate, .z = 0, .w = 0
        Float4 AmbientColor;    // .rgb = interior ambient, .a = intensity
        Float4 DirectionalColor;// .rgb = interior directional light, .a = fade
        Float4 DirectionalDir;  // .xyz = interior light direction, .w = 0
        Float4 InteriorFogColor;// .rgb = interior fog color, .a = 0
        Float4 InteriorFogDist; // .x = near, .y = far, .z = power, .w = clipDist

        Float4 LightingTemplate;// .x = templateFormID (float-cast), .y = inheritFlags, .z = 0, .w = 0
    };

    // ── 8. DIRECTIONAL LIGHT & SHADOWS ──────────────────────────────────

    struct ShadowData
    {
        Float4 LightDirection;  // .xyz = shadow caster direction (world), .w = shadow intensity
        Float4 LightDiffuse;    // .rgb = shadow caster diffuse color, .a = 0
        Float4 LightAmbient;    // .rgb = shadow caster ambient color, .a = 0
    };

    // ── 9. ACTIVE MAGIC EFFECTS ─────────────────────────────────────────
    // VisionEffects.z and MiscEffects.z are reserved: the vendored
    // CommonLibSSE-NG archetype/actor-value/keyword surface has no signal
    // for detect-dead or drunk/intoxication, so both publish 0.0 always.

    struct EffectsData
    {
        Float4 VisionEffects;   // .x = nightEye(0/1), .y = detectLife(0/1), .z = reserved (detectDead, no engine signal, always 0.0), .w = etherealForm(0/1)
        Float4 TimeEffects;     // .x = slowTimeFactor, .y = isTimeStopped, .z = 0, .w = 0
        Float4 DamageEffects;   // .x = isTakingFireDmg, .y = isTakingFrostDmg, .z = isTakingShockDmg, .w = isTakingPoisonDmg
        Float4 MiscEffects;     // .x = isInvisible, .y = isParalyzed, .z = reserved (isDrunk, no engine signal, always 0.0), .w = 0
    };

    // ── 10. RENDER STATE ────────────────────────────────────────────────

    struct RenderData
    {
        Float4 FrameInfo;       // .x = frameCount, .y = deltaTime (sec), .z = screenWidth, .w = screenHeight
        Float4 Jitter;          // .xy = TAA jitter offset (NDC), .z = frameIndex%16 (for blue noise), .w = timeDilation (game/real delta)
        Float4 StencilInfo;     // .x = stencilAvailable(0/1), .y = srvSlot (t16), .z = stencilBits, .w = gamePaused(0/1)
        // DepthParams removed — derivable from Camera.Params (near/far)
    };

    // ── 11. IMAGE SPACE — Game's post-processing state (IMODs) ────────

    struct ImageSpaceData
    {
        Float4 HDR;             // .x = eyeAdaptSpeed, .y = bloomScale, .z = bloomThreshold, .w = sunlightScale
        Float4 Cinematic;       // .x = saturation, .y = brightness, .z = contrast, .w = tintAlpha
        Float4 CineTint;        // .rgb = cinematic tint color, .a = 0
        Float4 DOF;             // .x = strength, .y = distance, .z = range, .w = vignetteRadius
        Float4 IMOD;            // .x = hasActiveIMOD(0/1), .y = imodStrength, .z = imodFadeIn, .w = imodElapsed
        Float4 IMODTint;        // .rgb = IMOD tint color, .a = blur amount
    };

    // ── 12. NEARBY LIGHTS — 3 nearest point/spot lights ───────────────

    struct LightData
    {
        Float4 Light0PosRad;    // .xyz = world position, .w = radius
        Float4 Light0Color;     // .rgb = color, .a = intensity
        Float4 Light1PosRad;
        Float4 Light1Color;
        Float4 Light2PosRad;
        Float4 Light2Color;
        Float4 Summary;         // .x = total nearby count, .y = nearest distance, .z = total luminous flux, .w = dominant hue [0,1]
    };

    // ── 13. ACTOR VALUES — Resistances, combat stats, skills ──────────

    struct ActorValueData
    {
        Float4 Resist;          // .x = fireResist%, .y = frostResist%, .z = shockResist%, .w = magicResist%
        Float4 Resist2;         // .x = poisonResist%, .y = diseaseResist%, .z = damageResist (armor), .w = 0
        Float4 Combat;          // .x = attackDamageMult, .y = weaponSpeedMult, .z = critChance, .w = unarmedDmg
        Float4 Movement;        // .x = speedMult, .y = carryWeight, .z = inventoryWeight, .w = encumbranceRatio
        Float4 SkillCombat;     // .x = oneHanded, .y = twoHanded, .z = archery, .w = block
        Float4 SkillMagic;      // .x = alteration, .y = conjuration, .z = destruction, .w = illusion
        Float4 SkillMagic2;     // .x = restoration, .y = enchanting, .z = alchemy, .w = 0
        Float4 SkillStealth;    // .x = lightArmor, .y = sneak, .z = lockpicking, .w = pickpocket
    };

    // ── 14. CROSSHAIR / LOOK-AT TARGET ────────────────────────────────

    struct CrosshairData
    {
        Float4 Info;            // .x = hasTarget(0/1), .y = distance, .z = formType (enum), .w = isActor(0/1)
        Float4 Pos;             // .xyz = target world position, .w = boundingRadius
        Float4 Actor;           // .x = healthPct, .y = level, .z = isHostile(0/1), .w = isEssential(0/1)
    };

    // ── 15. EQUIPMENT — Weapons, armor, torch state ───────────────────

    struct EquipmentData
    {
        Float4 Right;           // .x = weaponType, .y = baseDamage, .z = isEnchanted(0/1), .w = enchantCharge [0,1]
        Float4 Left;            // .x = itemType, .y = damage/armorRating, .z = isEnchanted(0/1), .w = isSpell(0/1)
        Float4 Armor;           // .x = totalArmorRating, .y = isWearingHeavy(0/1), .z = isWearingLight(0/1), .w = isWearingRobes(0/1)
        Float4 Flags;           // .x = weaponDrawn(0/1), .y = hasBow(0/1), .z = hasTorch(0/1), .w = isTwoHanding(0/1)
    };

    // ── 16. QUEST STATE ───────────────────────────────────────────────

    struct QuestData
    {
        Float4 Progress;        // .x = mainQuestStage, .y = totalQuestsCompleted, .z = activeQuestCount, .w = activeObjectiveCount
        Float4 Tracked;         // .x = trackedQuestStage, .y = questType, .z = questFormID (low 16 bits), .w = hasObjectiveMarker(0/1)
    };

    // ── 17. UI / MENU STATE ───────────────────────────────────────────

    struct UIStateData
    {
        Float4 Menus;           // .x = isInMenu(0/1), .y = isInDialogue(0/1), .z = isInInventory(0/1), .w = isInMap(0/1)
        Float4 HUD;             // .x = isHUDVisible(0/1), .y = isCrosshairVisible(0/1), .z = isInCinematicMode(0/1), .w = isLoading(0/1)
        Float4 Detail;          // .x = isInCrafting(0/1), .y = isInBook(0/1), .z = isInLockpick(0/1), .w = isInConsole(0/1)
    };

    // ── 18. COMPUTED FEEDBACK ─────────────────────────────────────────
    // Values read back from the GPU after ENB rendering (1-frame delay).
    // Populated by FeedbackProcessor in HookedPresent, distributed to
    // shaders on the next frame via the constant buffer.

    struct FeedbackData
    {
        Float4 Luminance;       // .x = smoothed center lum, .y = instant center lum, .z = center R, .w = center G
        Float4 Scene;           // .x = center B, .y = sceneAvgLum(smoothed), .z = lumRange(max-min), .w = feedbackValid(0/1)
        Float4 SceneStats;      // .x = keyValue(log-avg), .y = contrastRatio, .z = peripheryAvgLum, .w = center/periphery ratio
        Float4 SceneColor;      // .x = avgR, .y = avgG, .z = avgB, .w = colorTemp(K)
        Float4 Histogram;       // .x = shadows(<0.05), .y = darks(<0.18), .z = mids(<0.50), .w = brights(>=0.50)
        Float4 Temporal;        // .x = sceneCut(0/1), .y = lumVelocity, .z = colorShift, .w = stabilityScore

        // Tier C: ENBGetParameter readback — cross-shader data sharing
        // Slot0-3 single floats packed into one vector, Slot4+ available as float4
        Float4 ENBReadback;     // .x = slot0 value, .y = slot1, .z = slot2, .w = slot3 (single-float readback)
        Float4 ENBReadback4;    // = slot4 data (float4 readback, or slot4-7 floats)
    };

    // ── 19. REGION / LOCATION ─────────────────────────────────────────

    struct RegionData
    {
        Float4 Location;        // .x = locationFormID (float), .y = parentLocFormID, .z = worldspaceFormID, .w = cellFormID
        Float4 Region;          // .x = primaryRegionFormID, .y = hasWeatherOverride(0/1), .z = landMapWeight, .w = regionTypeFlags
        Float4 Worldspace;      // .x = hasLODWater(0/1), .y = defaultWaterLevel, .z = mapCenterX, .w = mapCenterY
    };

    // ── 20. AUDIO / MUSIC STATE ───────────────────────────────────────

    struct AudioData
    {
        Float4 Music;           // .x = musicTypeFormID (float), .y = musicPriority, .z = isCombatMusic(0/1), .w = isDungeonMusic(0/1)
        Float4 Ambient;         // .x = isExteriorAmbient(0/1), .y = reverbLevel, .z = weatherSoundActive(0/1), .w = 0
    };

    // ── 21. NPC DETECTION ─────────────────────────────────────────────

    struct NPCDetectData
    {
        Float4 Nearest;         // .x = distance (units), .y = isHostile(0/1), .z = healthPct, .w = level
        Float4 NearestPos;      // .xyz = world position, .w = isAlerted(0/1)
        Float4 Summary;         // .x = hostileCount(30m), .y = friendlyCount(30m), .z = nearestHostileDist, .w = nearestFriendlyDist
        Float4 Threat;          // .x = threatRating [0,1], .y = stealthMeter [0,100], .z = highActorCount, .w = maxDetectionLevel
    };

    // ── 22. PERFORMANCE & GPU TIMING ──────────────────────────────────

    struct PerfData
    {
        Float4 Timing;          // .x = gpuFrameMs, .y = cpuFrameMs, .z = presentLatencyMs, .w = targetFps
        Float4 Budget;          // .x = gpuBudgetPct [0,1], .y = qualityScale [0,1], .z = thermalState, .w = frameDropCount
    };

    // ── 23. SCENE COMPOSITION ──────────────────────────────────────────
    // Material counts from BSShader::BeginTechnique hook (1-frame delay).
    // Render engine state from BSShaderManager::State singleton.

    struct SceneData
    {
        Float4 MaterialCounts1; // .x = general%, .y = skin%, .z = terrain%, .w = vegetation% (fraction of lighting draws)
        Float4 MaterialCounts2; // .x = hair%, .y = eye%, .z = snow%, .w = emissive%
        Float4 DrawStats;       // .x = totalDrawCalls, .y = lightingDrawCalls, .z = metalGlossy%, .w = reserved
        Float4 CharLight;       // .x = charLightEnabled(0/1), .y = primary, .z = secondary, .w = luminance
        Float4 AmbientSpec;     // .rgb = ambient specular color, .a = ambientSpecEnabled(0/1)

        // Tier 3a: Per-frame material property aggregates (from SetupMaterial vtable hook)
        Float4 MaterialProps1;  // .x = avgSpecPower, .y = avgSpecScale, .z = avgRoughness(1/specPower), .w = avgSubSurfaceRolloff
        Float4 MaterialProps2;  // .x = avgRimLightPower, .y = avgEnvMapScale, .z = avgMaterialAlpha, .w = skinSpecPower
        Float4 ShaderFlags;     // .x = envMapFraction, .y = glowMapFraction, .z = backLitFraction, .w = softLitFraction

        // Tier A: BSShaderManager::State expanded reads
        Float4 EngineState;     // .x = interior(0/1), .y = cameraInWaterState, .z = waterIntersect, .w = currentShaderTechnique
        Float4 EngineTimers;    // .x = timerDefault, .y = timerDelta, .z = timerSystem, .w = timerRealDelta
        Float4 DirAmbient1;     // Sky::directionalAmbientColors — X+ (rgb) and .w = X- luminance
        Float4 DirAmbient2;     // Y+ (rgb) and .w = Y- luminance
        Float4 DirAmbient3;     // Z+ (rgb) and .w = Z- luminance
        Float4 SunGlare;        // .x = Sun::glareScale, .y = sunOcclusionTest, .z = activeLightCount, .w = shadowCasterCount

        // Tier B: Per-draw geometry info (SetupGeometry vtable hook on BSLightingShader)
        Float4 GeometryInfo;    // .x = avgLightsPerDraw, .y = maxLightsPerDraw, .z = avgPassEnum, .w = LODModeAvg
        // Tier B: Water shader observation (SetupMaterial vtable hook on BSWaterShader)
        Float4 WaterPlane;      // .xyz = water plane normal, .w = plane distance
        Float4 WaterColor;      // .rgb = shallow water color, .a = alpha
        Float4 WaterParams;     // .x = sunSpecularPower, .y = reflectionAmount, .z = refractionMagnitude, .w = fresnelAmount
        Float4 WaterWave;       // .x = displacementDampener, .y = flowmapScale, .z = aboveWaterFogDistFar, .w = underwaterFogDistFar
        // Tier B: Effect shader observation (SetupMaterial vtable hook on BSEffectShader)
        Float4 EffectShader;    // .x = effectDrawCount, .y = avgBaseColorScale, .z = avgSoftFalloffDepth, .w = avgFalloffOpacity
        Float4 EffectColor;     // .rgb = avgBaseColor, .a = avgAlpha
    };

    // ── 24. THEME ──────────────────────────────────────────────────────
    // C++ reads the theme index from enbeffect.fx ENB panel and broadcasts
    // to all 9 shaders via SB_Theme_Config, keeping them in sync.

    struct ThemeData
    {
        Float4 Config;          // .x = theme index (0-7), .yzw = reserved
    };

    // ── 25. DERIVED — CPU-computed screen-space projections ────────────
    // Restores the SB_*_NDC contract that shaders/SkyrimBridge.fxh has
    // documented since v2: single-pass ENB stages (sun sprite, lens) have
    // no place to share a per-frame VP projection, so deriving NDC per
    // pixel is wasteful there. Computed once per frame from CameraData by
    // ComputeDerivedData(), before sanitize. Sits at the end of AllData so
    // every pre-existing field keeps its shared-memory offset.

    struct DerivedData
    {
        Float4 SunNDC;          // .xy = NDC [-1,1], .z = onScreen (0/1), .w = elevation (rad)
        Float4 MasserNDC;       // .xy = NDC [-1,1], .z = onScreen (0/1), .w = phase brightness [0,1]
        Float4 SecundaNDC;      // .xy = NDC [-1,1], .z = onScreen (0/1), .w = phase brightness [0,1]
    };

    // ── Aggregate ───────────────────────────────────────────────────────

    struct AllData
    {
        CelestialData   celestial;
        AtmosphereData  atmosphere;
        FogData         fog;
        WeatherData     weather;
        PlayerData      player;
        CameraData      camera;
        InteriorData    interior;
        ShadowData      shadow;
        EffectsData     effects;
        RenderData      render;
        ImageSpaceData  imageSpace;
        LightData       lights;
        ActorValueData  actorValues;
        CrosshairData   crosshair;
        EquipmentData   equipment;
        QuestData       quest;
        UIStateData     uiState;
        FeedbackData    feedback;
        RegionData      region;
        AudioData       audio;
        NPCDetectData   npcDetect;
        PerfData        perf;
        SceneData       scene;
        ThemeData       theme;
        DerivedData     derived;
    };

    // Projects the celestial directions through the camera basis into the
    // SB_*_NDC parameters, mirroring SB_GetViewMatrix/SB_GetProjMatrix in
    // shaders/SkyrimBridge_CB.fxh (row-vector view rows, FOV in radians).
    // Call once per frame after the trackers fill CameraData/CelestialData
    // and before SanitizeAllData().
    void ComputeDerivedData(AllData& a_data);

    // ── Parameter name table ────────────────────────────────────────────
    // Maps each Float4 field to the ENB parameter name string.
    // Used by the push routine to iterate all parameters generically.

    struct ParamEntry {
        const char* name;       // ENB parameter name (e.g., "SB_Sun_NDC")
        std::size_t offset;     // Byte offset into AllData
    };

    // Declared in BridgeData.cpp
    extern const ParamEntry kParamTable[];
    extern const std::size_t kParamCount;

    // 16-byte alignment ensures each Float4 is naturally aligned for memcmp dirty tracking
    static_assert(sizeof(AllData) % 16 == 0,
        "AllData must be 16-byte aligned (Float4 natural alignment)");

    // Target shader files — every .fx that includes SkyrimBridge_CB.fxh
    // UPPERCASE required — ENB's internal lookup is case-sensitive.
    // Confirmed by doodlum/enb-api: uses "ENBEFFECT.FX" not "enbeffect.fx".
    inline constexpr const char* kTargetShaders[] = {
        "ENBSUNSPRITE.FX",
        "ENBEFFECTPREPASS.FX",
        "ENBEFFECT.FX",
        "ENBEFFECTPOSTPASS.FX",
        "ENBLENS.FX",
        "ENBUNDERWATER.FX",
        "ENBDEPTHOFFIELD.FX",
        "ENBBLOOM.FX",
        "ENBADAPTATION.FX",
    };
}
