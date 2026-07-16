//=============================================================================
//  SkyrimBridge_CB.fxh — Parameter declarations for SkyrimBridge data
//
//  SkyrimBridge v3 pushes ALL game state data via ENBSetParameter.
//  Each float4 is declared as an extern with UIName annotation,
//  which ENB's SDK matches by name. UIHidden=1 keeps them out of the
//  ENB editor GUI while remaining accessible via the SDK.
//
//  Usage: #include "Helper/SkyrimBridge_CB.fxh"
//  Then use SB_* variables directly (e.g., SB_Render_Frame.x for frameCount).
//
//  Author: Zain Dana Harper
//  License: MIT
//=============================================================================

#ifndef SKYRIMBRIDGE_CB_FXH
#define SKYRIMBRIDGE_CB_FXH

// Guard against the old extern-based header
#define SKYRIMBRIDGE_FXH 1

//-----------------------------------------------------------------------------
//  Parameter declarations — 134 float4s delivered via ENBSetParameter
//
//  Each variable has a UIName annotation matching the C++ kParamTable name.
//  ENB resolves SetParameter(nullptr, "ENBBLOOM.FX", "SB_Sun_Direction", &param)
//  to the variable with UIName="SB_Sun_Direction" in that shader.
//-----------------------------------------------------------------------------

// ---- 1. Celestial (7 float4s) ----
// NDC positions removed — derive from direction + SB_GetViewProjMatrix() in shader
float4 SB_Sun_Direction       < string UIName = "SB_Sun_Direction";       string UIWidget = "Color"; int UIHidden = 1; >; // .xyz = normalized world dir, .w = elevation angle (rad)
float4 SB_Sun_Color           < string UIName = "SB_Sun_Color";           string UIWidget = "Color"; int UIHidden = 1; >; // .rgb = weather sunlight color, .a = sun glare factor
float4 SB_Masser_Direction    < string UIName = "SB_Masser_Direction";    string UIWidget = "Color"; int UIHidden = 1; >; // .xyz = normalized world dir, .w = phase brightness [0,1]
float4 SB_Secunda_Direction   < string UIName = "SB_Secunda_Direction";   string UIWidget = "Color"; int UIHidden = 1; >; // .xyz = normalized world dir, .w = phase brightness [0,1]
float4 SB_Time                < string UIName = "SB_Time";                string UIWidget = "Color"; int UIHidden = 1; >; // .x = gameHour [0,24), .y = sunriseHour, .z = sunsetHour, .w = dayProgress [0,1]
float4 SB_Time_Segments1      < string UIName = "SB_Time_Segments1";      string UIWidget = "Color"; int UIHidden = 1; >; // .x = dawn [0,1], .y = sunrise [0,1], .z = day [0,1], .w = sunset [0,1]
float4 SB_Time_Segments2      < string UIName = "SB_Time_Segments2";      string UIWidget = "Color"; int UIHidden = 1; >; // .x = dusk [0,1], .y = night [0,1], .z = goldenHour [0,1], .w = blueHour [0,1]

// ---- 2. Atmosphere (8 float4s) ----
float4 SB_Atmos_SkyUpper      < string UIName = "SB_Atmos_SkyUpper";      string UIWidget = "Color"; int UIHidden = 1; >; // .rgb = upper sky gradient
float4 SB_Atmos_SkyLower      < string UIName = "SB_Atmos_SkyLower";      string UIWidget = "Color"; int UIHidden = 1; >; // .rgb = lower sky gradient
float4 SB_Atmos_Horizon       < string UIName = "SB_Atmos_Horizon";       string UIWidget = "Color"; int UIHidden = 1; >; // .rgb = horizon band color
float4 SB_Atmos_Ambient       < string UIName = "SB_Atmos_Ambient";       string UIWidget = "Color"; int UIHidden = 1; >; // .rgb = ambient light color, .a = ambient intensity
float4 SB_Atmos_Sunlight      < string UIName = "SB_Atmos_Sunlight";      string UIWidget = "Color"; int UIHidden = 1; >; // .rgb = directional sunlight color, .a = sunlight scale
float4 SB_Atmos_CloudDiffuse  < string UIName = "SB_Atmos_CloudDiffuse";  string UIWidget = "Color"; int UIHidden = 1; >; // .rgb = cloud LOD diffuse tint
float4 SB_Atmos_CloudAmbient  < string UIName = "SB_Atmos_CloudAmbient";  string UIWidget = "Color"; int UIHidden = 1; >; // .rgb = cloud LOD ambient tint
float4 SB_Atmos_EffectLight   < string UIName = "SB_Atmos_EffectLight";   string UIWidget = "Color"; int UIHidden = 1; >; // .rgb = magic/effect lighting color

// ---- 3. Fog (4 float4s) ----
float4 SB_Fog_NearColor       < string UIName = "SB_Fog_NearColor";       string UIWidget = "Color"; int UIHidden = 1; >; // .rgb = near fog color, .a = near distance
float4 SB_Fog_FarColor        < string UIName = "SB_Fog_FarColor";        string UIWidget = "Color"; int UIHidden = 1; >; // .rgb = far fog color,  .a = far distance
float4 SB_Fog_Density         < string UIName = "SB_Fog_Density";         string UIWidget = "Color"; int UIHidden = 1; >; // .x = power curve, .y = maxOpacity [0,1], .z = isInteriorFog(0/1)
float4 SB_Fog_Height          < string UIName = "SB_Fog_Height";          string UIWidget = "Color"; int UIHidden = 1; >; // .x = waterSurfaceZ, .y = playerAltitude, .z = seaLevelDensity, .w = falloffRate

// ---- 4. Weather (10 float4s) ----
float4 SB_Wind                < string UIName = "SB_Wind";                string UIWidget = "Color"; int UIHidden = 1; >; // .x = speed [0,1], .y = direction (radians)
float4 SB_Precipitation       < string UIName = "SB_Precipitation";       string UIWidget = "Color"; int UIHidden = 1; >; // .x = type (0=none,1=rain,2=snow), .y = intensity [0,1]
float4 SB_Lightning           < string UIName = "SB_Lightning";           string UIWidget = "Color"; int UIHidden = 1; >; // .x = frequency, .y = isFlashing(0/1), .z = flashIntensity, .w = timeSinceFlash(sec)
float4 SB_Weather_Flags       < string UIName = "SB_Weather_Flags";       string UIWidget = "Color"; int UIHidden = 1; >; // .x = packed uint bitfield (bit0=pleasant,1=cloudy,2=rainy,3=snowy), .yzw = reserved
float4 SB_Weather_Transition  < string UIName = "SB_Weather_Transition";  string UIWidget = "Color"; int UIHidden = 1; >; // .x = transition% [0,1], .y = outgoingWeatherID, .z = currentWeatherID
float4 SB_Precip_Surface      < string UIName = "SB_Precip_Surface";      string UIWidget = "Color"; int UIHidden = 1; >; // .x = surface wetness [0,1], .y = puddle depth, .z = snow accumulation
float4 SB_Wind_Live           < string UIName = "SB_Wind_Live";           string UIWidget = "Color"; int UIHidden = 1; >; // .x = Sky::windSpeed, .y = windAngle (rad), .z = windDirX (cos), .w = windDirZ (sin)
float4 SB_Precip_Live         < string UIName = "SB_Precip_Live";         string UIWidget = "Color"; int UIHidden = 1; >; // .x = currentParticleDensity, .y = lastParticleDensity, .z = Sky::flash, .w = gameHour
float4 SB_Cloud_Cover         < string UIName = "SB_Cloud_Cover";         string UIWidget = "Color"; int UIHidden = 1; >; // .x = avgCloudAlpha, .y = activeLayerCount, .z = maxLayerAlpha, .w = weatherTransition%
float4 SB_Aurora_Fade         < string UIName = "SB_Aurora_Fade";         string UIWidget = "Color"; int UIHidden = 1; >; // .x = auroraIn, .y = auroraOut, .z = auroraInStart, .w = auroraOutStart

// ---- 5. Player (5 float4s) ----
float4 SB_Player_Position     < string UIName = "SB_Player_Position";     string UIWidget = "Color"; int UIHidden = 1; >; // .xyz = world position, .w = altitude above sea level
float4 SB_Player_Vitals       < string UIName = "SB_Player_Vitals";       string UIWidget = "Color"; int UIHidden = 1; >; // .x = health%, .y = stamina%, .z = magicka%, .w = level
float4 SB_Player_Movement     < string UIName = "SB_Player_Movement";     string UIWidget = "Color"; int UIHidden = 1; >; // .x = speed (units/s), .y = isSprinting, .z = isSwimming, .w = isRiding
float4 SB_Player_Combat       < string UIName = "SB_Player_Combat";       string UIWidget = "Color"; int UIHidden = 1; >; // .x = packed uint bitfield (bit0=inCombat,1=bleedout,2=killMove,3=weaponDrawn), .y = beastForm(0=none,1=werewolf,2=vampireLord), .z = timeScale, .w = combatTargetCount
float4 SB_Player_Water        < string UIName = "SB_Player_Water";        string UIWidget = "Color"; int UIHidden = 1; >; // .x = isUnderwater(0/1), .y = waterSurfaceZ, .z = submersionDepth, .w = isWading(0/1)

// ---- 6. Camera (8 float4s — optimized: derive Proj/VP/InvVP in shader) ----
float4 SB_Camera_Params       < string UIName = "SB_Camera_Params";       string UIWidget = "Color"; int UIHidden = 1; >; // .x = FOV (RADIANS!), .y = nearClip, .z = farClip, .w = aspectRatio
float4 SB_Camera_WorldPos     < string UIName = "SB_Camera_WorldPos";     string UIWidget = "Color"; int UIHidden = 1; >; // .xyz = camera world position, .w = cameraStateEnum

// View rotation 3x3 (world -> camera) — 3 row vectors (Row3 derivable from WorldPos)
float4 SB_View_Row0           < string UIName = "SB_View_Row0";           string UIWidget = "Color"; int UIHidden = 1; >; // .xyz = right vector
float4 SB_View_Row1           < string UIName = "SB_View_Row1";           string UIWidget = "Color"; int UIHidden = 1; >; // .xyz = up vector
float4 SB_View_Row2           < string UIName = "SB_View_Row2";           string UIWidget = "Color"; int UIHidden = 1; >; // .xyz = forward vector

// Previous frame camera (for motion vectors / temporal effects)
float4 SB_PrevCamera_Pos      < string UIName = "SB_PrevCamera_Pos";      string UIWidget = "Color"; int UIHidden = 1; >; // .xyz = prev world pos, .w = prev FOV (rad)
float4 SB_PrevView_Row0       < string UIName = "SB_PrevView_Row0";       string UIWidget = "Color"; int UIHidden = 1; >; // .xyz = prev right vector
float4 SB_PrevView_Row1       < string UIName = "SB_PrevView_Row1";       string UIWidget = "Color"; int UIHidden = 1; >; // .xyz = prev up vector (derive Row2 via cross)

// ---- 7. Interior (7 float4s) ----
float4 SB_Interior_Flags      < string UIName = "SB_Interior_Flags";      string UIWidget = "Color"; int UIHidden = 1; >; // .x = packed uint bitfield (bit0=isInterior,1=hasLightingTemplate), .yzw = reserved
float4 SB_Interior_Ambient    < string UIName = "SB_Interior_Ambient";    string UIWidget = "Color"; int UIHidden = 1; >; // .rgb = interior ambient, .a = intensity
float4 SB_Interior_DirColor   < string UIName = "SB_Interior_DirColor";   string UIWidget = "Color"; int UIHidden = 1; >; // .rgb = interior directional light, .a = fade
float4 SB_Interior_DirDir     < string UIName = "SB_Interior_DirDir";     string UIWidget = "Color"; int UIHidden = 1; >; // .xyz = interior light direction
float4 SB_Interior_FogColor   < string UIName = "SB_Interior_FogColor";   string UIWidget = "Color"; int UIHidden = 1; >; // .rgb = interior fog color
float4 SB_Interior_FogDist    < string UIName = "SB_Interior_FogDist";    string UIWidget = "Color"; int UIHidden = 1; >; // .x = near, .y = far, .z = power, .w = clipDist
float4 SB_Interior_Template   < string UIName = "SB_Interior_Template";   string UIWidget = "Color"; int UIHidden = 1; >; // .x = lightingTemplateFormID, .y = inheritFlags

// ---- 8. Shadow (3 float4s) ----
float4 SB_Shadow_Direction    < string UIName = "SB_Shadow_Direction";    string UIWidget = "Color"; int UIHidden = 1; >; // .xyz = shadow caster direction (world), .w = shadow intensity
float4 SB_Shadow_Diffuse      < string UIName = "SB_Shadow_Diffuse";      string UIWidget = "Color"; int UIHidden = 1; >; // .rgb = shadow caster diffuse color
float4 SB_Shadow_Ambient      < string UIName = "SB_Shadow_Ambient";      string UIWidget = "Color"; int UIHidden = 1; >; // .rgb = shadow caster ambient color

// ---- 9. Effects (4 float4s) ----
float4 SB_FX_Vision           < string UIName = "SB_FX_Vision";           string UIWidget = "Color"; int UIHidden = 1; >; // .x = packed uint bitfield (bit0=nightEye,1=detectLife,2=detectDead,3=ethereal), .yzw = reserved
float4 SB_FX_Time             < string UIName = "SB_FX_Time";             string UIWidget = "Color"; int UIHidden = 1; >; // .x = slowTimeFactor, .y = isTimeStopped
float4 SB_FX_Damage           < string UIName = "SB_FX_Damage";           string UIWidget = "Color"; int UIHidden = 1; >; // .x = packed uint bitfield (bit0=fire,1=frost,2=shock,3=poison), .yzw = reserved
float4 SB_FX_Misc             < string UIName = "SB_FX_Misc";             string UIWidget = "Color"; int UIHidden = 1; >; // .x = packed uint bitfield (bit0=invisible,1=paralyzed,2=drunk), .yzw = reserved

// ---- 10. Render State (3 float4s) ----
// DepthParams removed — use SB_LinearizeDepth() which derives from Camera_Params
float4 SB_Render_Frame        < string UIName = "SB_Render_Frame";        string UIWidget = "Color"; int UIHidden = 1; >; // .x = frameCount, .y = deltaTime (sec), .z = screenWidth, .w = screenHeight
float4 SB_Render_Jitter       < string UIName = "SB_Render_Jitter";       string UIWidget = "Color"; int UIHidden = 1; >; // .xy = TAA jitter offset (NDC), .z = frameIndex%16, .w = timeDilation (game/real delta, <1=slowmo)
float4 SB_Render_StencilInfo  < string UIName = "SB_Render_StencilInfo";  string UIWidget = "Color"; int UIHidden = 1; >; // .x = stencilAvailable(0/1), .y = srvSlot (t16), .z = stencilBits, .w = gamePaused(0/1)

// ---- 11. ImageSpace (6 float4s) ----
float4 SB_IS_HDR              < string UIName = "SB_IS_HDR";              string UIWidget = "Color"; int UIHidden = 1; >; // .x = eyeAdaptSpeed, .y = bloomScale, .z = bloomThreshold, .w = sunlightScale
float4 SB_IS_Cinematic        < string UIName = "SB_IS_Cinematic";        string UIWidget = "Color"; int UIHidden = 1; >; // .x = saturation, .y = brightness, .z = contrast, .w = tintAlpha
float4 SB_IS_CineTint         < string UIName = "SB_IS_CineTint";         string UIWidget = "Color"; int UIHidden = 1; >; // .rgb = cinematic tint color
float4 SB_IS_DOF              < string UIName = "SB_IS_DOF";              string UIWidget = "Color"; int UIHidden = 1; >; // .x = strength, .y = distance, .z = range, .w = vignetteRadius
float4 SB_IS_IMOD             < string UIName = "SB_IS_IMOD";             string UIWidget = "Color"; int UIHidden = 1; >; // .x = hasActiveIMOD(0/1), .y = imodStrength, .z = imodFadeIn, .w = imodElapsed
float4 SB_IS_IMODTint         < string UIName = "SB_IS_IMODTint";         string UIWidget = "Color"; int UIHidden = 1; >; // .rgb = IMOD tint color, .a = blur amount

// ---- 12. Nearby Lights (7 float4s) ----
float4 SB_Light0_PosRad       < string UIName = "SB_Light0_PosRad";       string UIWidget = "Color"; int UIHidden = 1; >; // .xyz = world position, .w = radius
float4 SB_Light0_Color        < string UIName = "SB_Light0_Color";        string UIWidget = "Color"; int UIHidden = 1; >; // .rgb = color, .a = intensity
float4 SB_Light1_PosRad       < string UIName = "SB_Light1_PosRad";       string UIWidget = "Color"; int UIHidden = 1; >;
float4 SB_Light1_Color        < string UIName = "SB_Light1_Color";        string UIWidget = "Color"; int UIHidden = 1; >;
float4 SB_Light2_PosRad       < string UIName = "SB_Light2_PosRad";       string UIWidget = "Color"; int UIHidden = 1; >;
float4 SB_Light2_Color        < string UIName = "SB_Light2_Color";        string UIWidget = "Color"; int UIHidden = 1; >;
float4 SB_Light_Summary       < string UIName = "SB_Light_Summary";       string UIWidget = "Color"; int UIHidden = 1; >; // .x = total nearby count, .y = nearest distance, .z = total luminous flux, .w = dominant hue [0,1]

// ---- 13. Actor Values (8 float4s) ----
float4 SB_AV_Resist           < string UIName = "SB_AV_Resist";           string UIWidget = "Color"; int UIHidden = 1; >; // .x = fireResist%, .y = frostResist%, .z = shockResist%, .w = magicResist%
float4 SB_AV_Resist2          < string UIName = "SB_AV_Resist2";          string UIWidget = "Color"; int UIHidden = 1; >; // .x = poisonResist%, .y = diseaseResist%, .z = damageResist (armor)
float4 SB_AV_Combat           < string UIName = "SB_AV_Combat";           string UIWidget = "Color"; int UIHidden = 1; >; // .x = attackDamageMult, .y = weaponSpeedMult, .z = critChance, .w = unarmedDmg
float4 SB_AV_Movement         < string UIName = "SB_AV_Movement";         string UIWidget = "Color"; int UIHidden = 1; >; // .x = speedMult, .y = carryWeight, .z = inventoryWeight, .w = encumbranceRatio
float4 SB_AV_SkillCombat      < string UIName = "SB_AV_SkillCombat";      string UIWidget = "Color"; int UIHidden = 1; >; // .x = oneHanded, .y = twoHanded, .z = archery, .w = block
float4 SB_AV_SkillMagic       < string UIName = "SB_AV_SkillMagic";       string UIWidget = "Color"; int UIHidden = 1; >; // .x = alteration, .y = conjuration, .z = destruction, .w = illusion
float4 SB_AV_SkillMagic2      < string UIName = "SB_AV_SkillMagic2";      string UIWidget = "Color"; int UIHidden = 1; >; // .x = restoration, .y = enchanting, .z = alchemy
float4 SB_AV_SkillStealth     < string UIName = "SB_AV_SkillStealth";     string UIWidget = "Color"; int UIHidden = 1; >; // .x = lightArmor, .y = sneak, .z = lockpicking, .w = pickpocket

// ---- 14. Crosshair (3 float4s) ----
float4 SB_XHair_Info          < string UIName = "SB_XHair_Info";          string UIWidget = "Color"; int UIHidden = 1; >; // .x = hasTarget(0/1), .y = distance, .z = formType (enum), .w = isActor(0/1)
float4 SB_XHair_Pos           < string UIName = "SB_XHair_Pos";           string UIWidget = "Color"; int UIHidden = 1; >; // .xyz = target world position, .w = boundingRadius
float4 SB_XHair_Actor         < string UIName = "SB_XHair_Actor";         string UIWidget = "Color"; int UIHidden = 1; >; // .x = healthPct, .y = level, .z = isHostile(0/1), .w = isEssential(0/1)

// ---- 15. Equipment (4 float4s) ----
float4 SB_Equip_Right         < string UIName = "SB_Equip_Right";         string UIWidget = "Color"; int UIHidden = 1; >; // .x = weaponType, .y = baseDamage, .z = isEnchanted(0/1), .w = enchantCharge [0,1]
float4 SB_Equip_Left          < string UIName = "SB_Equip_Left";          string UIWidget = "Color"; int UIHidden = 1; >; // .x = itemType, .y = damage/armorRating, .z = isEnchanted(0/1), .w = isSpell(0/1)
float4 SB_Equip_Armor         < string UIName = "SB_Equip_Armor";         string UIWidget = "Color"; int UIHidden = 1; >; // .x = totalArmorRating, .y = isWearingHeavy(0/1), .z = isWearingLight(0/1), .w = isWearingRobes(0/1)
float4 SB_Equip_Flags         < string UIName = "SB_Equip_Flags";         string UIWidget = "Color"; int UIHidden = 1; >; // .x = packed uint bitfield (bit0=weaponDrawn,1=hasBow,2=hasTorch,3=twoHanding), .yzw = reserved

// ---- 16. Quest (2 float4s) ----
float4 SB_Quest_Progress      < string UIName = "SB_Quest_Progress";      string UIWidget = "Color"; int UIHidden = 1; >; // .x = mainQuestStage, .y = totalQuestsCompleted, .z = activeQuestCount, .w = activeObjectiveCount
float4 SB_Quest_Tracked       < string UIName = "SB_Quest_Tracked";       string UIWidget = "Color"; int UIHidden = 1; >; // .x = trackedQuestStage, .y = questType, .z = questFormID (low 16 bits), .w = hasObjectiveMarker(0/1)

// ---- 17. UI State (3 float4s) ----
float4 SB_UI_Menus            < string UIName = "SB_UI_Menus";            string UIWidget = "Color"; int UIHidden = 1; >; // .x = packed uint bitfield (bit0=inMenu,1=inDialogue,2=inInventory,3=inMap), .yzw = reserved
float4 SB_UI_HUD              < string UIName = "SB_UI_HUD";              string UIWidget = "Color"; int UIHidden = 1; >; // .x = packed uint bitfield (bit0=hudVisible,1=crosshairVisible,2=cinematicMode,3=loading), .yzw = reserved
float4 SB_UI_Detail           < string UIName = "SB_UI_Detail";           string UIWidget = "Color"; int UIHidden = 1; >; // .x = packed uint bitfield (bit0=crafting,1=book,2=lockpick,3=console), .yzw = reserved

// ---- 18. Computed Feedback (6 float4s) ----
// GPU read-back from previous frame (1-frame delay)
float4 SB_Computed_Luminance  < string UIName = "SB_Computed_Luminance";  string UIWidget = "Color"; int UIHidden = 1; >; // .x = smoothed center lum, .y = instant center lum, .z = center R, .w = center G
float4 SB_Computed_Scene      < string UIName = "SB_Computed_Scene";      string UIWidget = "Color"; int UIHidden = 1; >; // .x = center B, .y = sceneAvgLum(smoothed), .z = lumRange(max-min), .w = feedbackValid(0/1)
float4 SB_Computed_SceneStats < string UIName = "SB_Computed_SceneStats"; string UIWidget = "Color"; int UIHidden = 1; >; // .x = keyValue(log-avg), .y = contrastRatio, .z = peripheryAvgLum, .w = center/periphery ratio
float4 SB_Computed_SceneColor < string UIName = "SB_Computed_SceneColor"; string UIWidget = "Color"; int UIHidden = 1; >; // .x = avgR, .y = avgG, .z = avgB, .w = colorTemp(K)
float4 SB_Computed_Histogram  < string UIName = "SB_Computed_Histogram";  string UIWidget = "Color"; int UIHidden = 1; >; // .x = shadows(<0.05), .y = darks(<0.18), .z = mids(<0.50), .w = brights(>=0.50)
float4 SB_Computed_Temporal   < string UIName = "SB_Computed_Temporal";   string UIWidget = "Color"; int UIHidden = 1; >; // .x = sceneCut(0/1), .y = lumVelocity, .z = colorShift, .w = stabilityScore
// Tier C: ENBGetParameter readback — cross-shader data sharing
float4 SB_ENB_Readback       < string UIName = "SB_ENB_Readback";       string UIWidget = "Color"; int UIHidden = 1; >; // .x = slot0, .y = slot1, .z = slot2, .w = slot3 (FeedbackConfig.ini)
float4 SB_ENB_Readback4      < string UIName = "SB_ENB_Readback4";      string UIWidget = "Color"; int UIHidden = 1; >; // slot4 as float4 or slots 4-7 as floats

// ---- 19. Region / Location (3 float4s) ----
float4 SB_Region_Location     < string UIName = "SB_Region_Location";     string UIWidget = "Color"; int UIHidden = 1; >; // .x = locationFormID, .y = parentLocFormID, .z = worldspaceFormID, .w = cellFormID
float4 SB_Region_Region       < string UIName = "SB_Region_Region";       string UIWidget = "Color"; int UIHidden = 1; >; // .x = primaryRegionFormID, .y = hasWeatherOverride(0/1), .z = landMapWeight, .w = regionTypeFlags
float4 SB_Region_Worldspace   < string UIName = "SB_Region_Worldspace";   string UIWidget = "Color"; int UIHidden = 1; >; // .x = hasLODWater(0/1), .y = defaultWaterLevel, .z = mapCenterX, .w = mapCenterY

// ---- 20. Audio / Music (2 float4s) ----
float4 SB_Audio_Music         < string UIName = "SB_Audio_Music";         string UIWidget = "Color"; int UIHidden = 1; >; // .x = musicTypeFormID, .y = musicPriority, .z = isCombatMusic(0/1), .w = isDungeonMusic(0/1)
float4 SB_Audio_Ambient       < string UIName = "SB_Audio_Ambient";       string UIWidget = "Color"; int UIHidden = 1; >; // .x = isExteriorAmbient(0/1), .y = reverbLevel, .z = weatherSoundActive(0/1)

// ---- 21. NPC Detection (4 float4s) ----
float4 SB_NPC_Nearest         < string UIName = "SB_NPC_Nearest";         string UIWidget = "Color"; int UIHidden = 1; >; // .x = distance (units), .y = isHostile(0/1), .z = healthPct, .w = level
float4 SB_NPC_NearestPos      < string UIName = "SB_NPC_NearestPos";      string UIWidget = "Color"; int UIHidden = 1; >; // .xyz = world position, .w = isAlerted(0/1)
float4 SB_NPC_Summary         < string UIName = "SB_NPC_Summary";         string UIWidget = "Color"; int UIHidden = 1; >; // .x = hostileCount(30m), .y = friendlyCount(30m), .z = nearestHostileDist, .w = nearestFriendlyDist
float4 SB_NPC_Threat          < string UIName = "SB_NPC_Threat";          string UIWidget = "Color"; int UIHidden = 1; >; // .x = threatRating [0,1], .y = stealthMeter, .z = highActorCount, .w = maxDetectionLevel

// ---- 22. Performance & GPU Timing (2 float4s) ----
float4 SB_Perf_Timing         < string UIName = "SB_Perf_Timing";         string UIWidget = "Color"; int UIHidden = 1; >; // .x = gpuFrameMs, .y = cpuFrameMs, .z = presentLatencyMs, .w = targetFps
float4 SB_Perf_Budget         < string UIName = "SB_Perf_Budget";         string UIWidget = "Color"; int UIHidden = 1; >; // .x = gpuBudgetPct [0,1], .y = qualityScale [0,1], .z = thermalState, .w = frameDropCount

// ---- 23. Scene Composition (5 float4s) ----
// Material counts from BSShader::BeginTechnique hook (1-frame delay, fractions of total lighting draws)
float4 SB_Scene_MatCount1     < string UIName = "SB_Scene_MatCount1";     string UIWidget = "Color"; int UIHidden = 1; >; // .x = general%, .y = skin%, .z = terrain%, .w = vegetation%
float4 SB_Scene_MatCount2     < string UIName = "SB_Scene_MatCount2";     string UIWidget = "Color"; int UIHidden = 1; >; // .x = hair%, .y = eye%, .z = snow%, .w = emissive%
float4 SB_Scene_DrawStats     < string UIName = "SB_Scene_DrawStats";     string UIWidget = "Color"; int UIHidden = 1; >; // .x = totalDrawCalls, .y = lightingDrawCalls, .z = metalGlossy%
float4 SB_Scene_CharLight     < string UIName = "SB_Scene_CharLight";     string UIWidget = "Color"; int UIHidden = 1; >; // .x = enabled, .y = primary, .z = secondary, .w = luminance
float4 SB_Scene_AmbientSpec   < string UIName = "SB_Scene_AmbientSpec";   string UIWidget = "Color"; int UIHidden = 1; >; // .rgb = ambient specular, .a = enabled

// Tier 3a: Material property aggregates (3 float4s)
float4 SB_Scene_MatProps1     < string UIName = "SB_Scene_MatProps1";     string UIWidget = "Color"; int UIHidden = 1; >; // .x = avgSpecPower, .y = avgSpecScale, .z = avgRoughness, .w = avgSubSurface
float4 SB_Scene_MatProps2     < string UIName = "SB_Scene_MatProps2";     string UIWidget = "Color"; int UIHidden = 1; >; // .x = avgRimLight, .y = avgEnvMapScale, .z = avgAlpha, .w = skinSpecPower
float4 SB_Scene_ShaderFlags   < string UIName = "SB_Scene_ShaderFlags";   string UIWidget = "Color"; int UIHidden = 1; >; // .x = envMapFrac, .y = glowMapFrac, .z = backLitFrac, .w = softLitFrac

// Tier A: Engine state expanded reads (6 float4s)
float4 SB_Engine_State        < string UIName = "SB_Engine_State";        string UIWidget = "Color"; int UIHidden = 1; >; // .x = interior(0/1), .y = cameraInWaterState, .z = waterIntersect, .w = currentShaderTechnique
float4 SB_Engine_Timers       < string UIName = "SB_Engine_Timers";       string UIWidget = "Color"; int UIHidden = 1; >; // .x = timerDefault, .y = timerDelta, .z = timerSystem, .w = timerRealDelta
float4 SB_DirAmbient_X        < string UIName = "SB_DirAmbient_X";        string UIWidget = "Color"; int UIHidden = 1; >; // .rgb = X+ directional ambient, .w = X- luminance
float4 SB_DirAmbient_Y        < string UIName = "SB_DirAmbient_Y";        string UIWidget = "Color"; int UIHidden = 1; >; // .rgb = Y+ directional ambient, .w = Y- luminance
float4 SB_DirAmbient_Z        < string UIName = "SB_DirAmbient_Z";        string UIWidget = "Color"; int UIHidden = 1; >; // .rgb = Z+ directional ambient, .w = Z- luminance
float4 SB_Sun_Glare           < string UIName = "SB_Sun_Glare";           string UIWidget = "Color"; int UIHidden = 1; >; // .x = Sun::glareScale, .y = sunOcclusionTest, .z = activeLightCount, .w = shadowCasterCount

// Tier B: Per-draw geometry + water/effect shader observation (7 float4s)
float4 SB_Scene_GeomInfo      < string UIName = "SB_Scene_GeomInfo";      string UIWidget = "Color"; int UIHidden = 1; >; // .x = avgLightsPerDraw, .y = maxLightsPerDraw, .z = avgPassEnum, .w = avgLODMode
float4 SB_Water_Plane         < string UIName = "SB_Water_Plane";         string UIWidget = "Color"; int UIHidden = 1; >; // .xyz = water plane normal, .w = plane constant (distance)
float4 SB_Water_Color         < string UIName = "SB_Water_Color";         string UIWidget = "Color"; int UIHidden = 1; >; // .rgb = shallow water color, .a = alpha
float4 SB_Water_Params        < string UIName = "SB_Water_Params";        string UIWidget = "Color"; int UIHidden = 1; >; // .x = sunSpecularPower, .y = reflectionAmount, .z = refractionMagnitude, .w = fresnelAmount
float4 SB_Water_Wave          < string UIName = "SB_Water_Wave";          string UIWidget = "Color"; int UIHidden = 1; >; // .x = displacementDampener, .y = flowmapScale, .z = aboveWaterFogDistFar, .w = underwaterFogDistFar
float4 SB_Effect_Shader       < string UIName = "SB_Effect_Shader";       string UIWidget = "Color"; int UIHidden = 1; >; // .x = effectDrawCount, .y = avgBaseColorScale, .z = avgSoftFalloffDepth, .w = avgFalloffOpacity
float4 SB_Effect_Color        < string UIName = "SB_Effect_Color";        string UIWidget = "Color"; int UIHidden = 1; >; // .rgb = avgBaseColor, .a = avgAlpha

// ---- 24. Theme (1 float4) ----
float4 SB_Theme_Config        < string UIName = "SB_Theme_Config";        string UIWidget = "Color"; int UIHidden = 1; >; // .x = theme index synced across all shaders

//-----------------------------------------------------------------------------
//  Bitfield extraction macros
//
//  Pure-flag float4s store all flags as bits in .x via asuint().
//  SB_HAS_FLAG returns bool, SB_FLAG_TO_FLOAT returns 0.0/1.0 for multipliers.
//-----------------------------------------------------------------------------

#define SB_HAS_FLAG(field, bit)       ((asuint(field) & (bit)) != 0)
#define SB_FLAG_TO_FLOAT(field, bit)  (SB_HAS_FLAG(field, bit) ? 1.0 : 0.0)

// -- Weather flags (SB_Weather_Flags.x) --
#define SB_WFLAG_PLEASANT   (1u << 0)
#define SB_WFLAG_CLOUDY     (1u << 1)
#define SB_WFLAG_RAINY      (1u << 2)
#define SB_WFLAG_SNOWY      (1u << 3)

// -- Player combat flags (SB_Player_Combat.x) --
#define SB_PFLAG_IN_COMBAT     (1u << 0)
#define SB_PFLAG_BLEEDOUT      (1u << 1)
#define SB_PFLAG_KILLMOVE      (1u << 2)
#define SB_PFLAG_WEAPON_DRAWN  (1u << 3)

// -- Interior flags (SB_Interior_Flags.x) --
#define SB_IFLAG_IS_INTERIOR       (1u << 0)
#define SB_IFLAG_HAS_LIGHTING_TPL  (1u << 1)

// -- Vision FX flags (SB_FX_Vision.x) --
#define SB_VFLAG_NIGHT_EYE    (1u << 0)
#define SB_VFLAG_DETECT_LIFE  (1u << 1)
#define SB_VFLAG_DETECT_DEAD  (1u << 2)
#define SB_VFLAG_ETHEREAL     (1u << 3)

// -- Damage FX flags (SB_FX_Damage.x) --
#define SB_DFLAG_FIRE    (1u << 0)
#define SB_DFLAG_FROST   (1u << 1)
#define SB_DFLAG_SHOCK   (1u << 2)
#define SB_DFLAG_POISON  (1u << 3)

// -- Misc FX flags (SB_FX_Misc.x) --
#define SB_MFLAG_INVISIBLE  (1u << 0)
#define SB_MFLAG_PARALYZED  (1u << 1)
#define SB_MFLAG_DRUNK      (1u << 2)

// -- Equipment flags (SB_Equip_Flags.x) --
#define SB_EFLAG_WEAPON_DRAWN  (1u << 0)
#define SB_EFLAG_HAS_BOW       (1u << 1)
#define SB_EFLAG_HAS_TORCH     (1u << 2)
#define SB_EFLAG_TWO_HANDING   (1u << 3)

// -- UI Menu flags (SB_UI_Menus.x) --
#define SB_UFLAG_IN_MENU       (1u << 0)
#define SB_UFLAG_IN_DIALOGUE   (1u << 1)
#define SB_UFLAG_IN_INVENTORY  (1u << 2)
#define SB_UFLAG_IN_MAP        (1u << 3)

// -- UI HUD flags (SB_UI_HUD.x) --
#define SB_UFLAG_HUD_VISIBLE        (1u << 0)
#define SB_UFLAG_CROSSHAIR_VISIBLE  (1u << 1)
#define SB_UFLAG_CINEMATIC_MODE     (1u << 2)
#define SB_UFLAG_LOADING            (1u << 3)

// -- UI Detail flags (SB_UI_Detail.x) --
#define SB_UFLAG_CRAFTING   (1u << 0)
#define SB_UFLAG_BOOK       (1u << 1)
#define SB_UFLAG_LOCKPICK   (1u << 2)
#define SB_UFLAG_CONSOLE    (1u << 3)


//-----------------------------------------------------------------------------
//  Helper functions
//-----------------------------------------------------------------------------

// Returns true when SkyrimBridge is actively pushing data
bool SB_IsActive() { return SB_Render_Frame.x > 0.0; }

// ---- Depth helpers ----

// Linearize a hardware depth buffer value to view-space distance
float SB_LinearizeDepth(float rawDepth)
{
    float n = SB_Camera_Params.y;  // nearClip
    float f = SB_Camera_Params.z;  // farClip
    return n * f / (f - rawDepth * (f - n));
}

// ---- Matrix reconstruction helpers ----
// Per Marty McFly's optimization: derive full matrices from minimal camera data.
// Call these once (in VS or at PS start), NOT per-pixel.
// Convention: row-vector multiply — use mul(float4(pos, 1), matrix)

// Reconstruct 4x4 view matrix from rotation rows + world position
float4x4 SB_GetViewMatrix()
{
    float3 r = SB_View_Row0.xyz;   // right
    float3 u = SB_View_Row1.xyz;   // up
    float3 f = SB_View_Row2.xyz;   // forward
    float3 p = SB_Camera_WorldPos.xyz;
    return float4x4(
        r.x, r.y, r.z, -dot(r, p),
        u.x, u.y, u.z, -dot(u, p),
        f.x, f.y, f.z, -dot(f, p),
          0,   0,   0,          1
    );
}

// Reconstruct perspective projection from FOV(rad)/near/far/aspect
float4x4 SB_GetProjMatrix()
{
    float fov = SB_Camera_Params.x;   // radians
    float n   = SB_Camera_Params.y;
    float f   = SB_Camera_Params.z;
    float a   = SB_Camera_Params.w;   // aspect ratio (w/h)
    float t   = tan(fov * 0.5);
    return float4x4(
        1.0/(a*t), 0,     0,            0,
        0,         1.0/t, 0,            0,
        0,         0,     f/(f-n),      1,
        0,         0,    -n*f/(f-n),    0
    );
}

// View * Projection — cache the result, don't call per-pixel
float4x4 SB_GetViewProjMatrix()
{
    return mul(SB_GetViewMatrix(), SB_GetProjMatrix());
}

// Inverse view matrix (transpose of rotation 3x3 + camera world position)
// View 3x3 is orthonormal so transpose == inverse — no matrix inversion needed
float4x4 SB_GetInvViewMatrix()
{
    float3 r = SB_View_Row0.xyz;
    float3 u = SB_View_Row1.xyz;
    float3 f = SB_View_Row2.xyz;
    float3 p = SB_Camera_WorldPos.xyz;
    return float4x4(
        r.x, u.x, f.x, p.x,
        r.y, u.y, f.y, p.y,
        r.z, u.z, f.z, p.z,
          0,   0,   0,   1
    );
}

// Inverse projection matrix (analytical inverse of perspective)
float4x4 SB_GetInvProjMatrix()
{
    float fov = SB_Camera_Params.x;
    float n   = SB_Camera_Params.y;
    float f   = SB_Camera_Params.z;
    float a   = SB_Camera_Params.w;
    float t   = tan(fov * 0.5);
    return float4x4(
        a*t, 0, 0,             0,
        0,   t, 0,             0,
        0,   0, 0, -(f-n)/(n*f),
        0,   0, 1,        1.0/n
    );
}

// Inverse ViewProjection — InvProj * InvView (row-vector convention)
float4x4 SB_GetInvViewProjMatrix()
{
    return mul(SB_GetInvProjMatrix(), SB_GetInvViewMatrix());
}

// Previous frame ViewProjection (for motion vectors / temporal effects)
// Uses prev camera data; derives Row2 from cross(Row0, Row1) and
// assumes near/far/aspect unchanged (only FOV can change between frames)
float4x4 SB_GetPrevViewProjMatrix()
{
    float3 r = SB_PrevView_Row0.xyz;
    float3 u = SB_PrevView_Row1.xyz;
    float3 f = cross(r, u);                // left-handed: forward = cross(right, up)
    float3 p = SB_PrevCamera_Pos.xyz;

    float4x4 prevView = float4x4(
        r.x, r.y, r.z, -dot(r, p),
        u.x, u.y, u.z, -dot(u, p),
        f.x, f.y, f.z, -dot(f, p),
          0,   0,   0,          1
    );

    float prevFov = SB_PrevCamera_Pos.w;   // previous FOV (rad)
    float n       = SB_Camera_Params.y;    // near/far assumed stable
    float f_clip  = SB_Camera_Params.z;
    float a       = SB_Camera_Params.w;
    float t       = tan(prevFov * 0.5);

    float4x4 prevProj = float4x4(
        1.0/(a*t), 0,     0,                    0,
        0,         1.0/t, 0,                    0,
        0,         0,     f_clip/(f_clip-n),     1,
        0,         0,    -n*f_clip/(f_clip-n),   0
    );

    return mul(prevView, prevProj);
}

// ---- World position reconstruction ----

// Reconstruct world position from screen UV [0,1] and raw depth buffer value
// Requires a precomputed InvViewProj matrix (call SB_GetInvViewProjMatrix() once)
float3 SB_WorldPosFromDepth(float2 uv, float rawDepth, float4x4 invVP)
{
    // UV → NDC (DX convention: Y flipped)
    float4 clipPos = float4(
        uv.x * 2.0 - 1.0,
        1.0 - uv.y * 2.0,
        rawDepth,
        1.0
    );
    float4 worldH = mul(clipPos, invVP);
    return worldH.xyz / worldH.w;
}

// ---- Motion vector helper ----

// Compute screen-space motion vector for a world position
// Returns (currentUV - prevUV) — positive = moved right/down
// Requires precomputed current VP and previous VP matrices
float2 SB_MotionVector(float3 worldPos, float4x4 currVP, float4x4 prevVP)
{
    float4 currClip = mul(float4(worldPos, 1.0), currVP);
    float2 currUV = currClip.xy / currClip.w * float2(0.5, -0.5) + 0.5;

    float4 prevClip = mul(float4(worldPos, 1.0), prevVP);
    float2 prevUV = prevClip.xy / prevClip.w * float2(0.5, -0.5) + 0.5;

    return currUV - prevUV;
}

// ---- Sun/Moon screen position helpers ----

// Project a world-space direction to [0,1] screen UV via a precomputed ViewProj
// For directional sources (sun/moon), projects from camera + direction * farClip
float2 SB_DirectionToScreenUV(float3 dir, float4x4 vp)
{
    float3 worldPt = SB_Camera_WorldPos.xyz + dir * SB_Camera_Params.z;
    float4 clipPos = mul(float4(worldPt, 1.0), vp);
    return clipPos.xy / clipPos.w * float2(0.5, -0.5) + 0.5;
}

// Sun screen UV — pass a precomputed VP, or call without args (computes VP internally)
float2 SB_SunScreenUV(float4x4 vp)    { return SB_DirectionToScreenUV(SB_Sun_Direction.xyz, vp); }
float2 SB_MasserScreenUV(float4x4 vp) { return SB_DirectionToScreenUV(SB_Masser_Direction.xyz, vp); }
float2 SB_SecundaScreenUV(float4x4 vp){ return SB_DirectionToScreenUV(SB_Secunda_Direction.xyz, vp); }

// ---- Time of day ----

// Returns true between sunset and sunrise
bool SB_IsNight() { return SB_Time.x < SB_Time.y || SB_Time.x > SB_Time.z; }

// Menu/UI state helpers (used by adaptation and DOF shaders)
bool SB_IsInMenu()         { return SB_HAS_FLAG(SB_UI_Menus.x, SB_UFLAG_IN_MENU); }
bool SB_IsInDialogue()     { return SB_HAS_FLAG(SB_UI_Menus.x, SB_UFLAG_IN_DIALOGUE); }
bool SB_IsLoading()        { return SB_HAS_FLAG(SB_UI_HUD.x, SB_UFLAG_LOADING); }
bool SB_HasTorchEquipped() { return SB_HAS_FLAG(SB_Equip_Flags.x, SB_EFLAG_HAS_TORCH); }

// Weather flag helpers (bool + float versions for multiplier use)
bool  SB_IsRaining()  { return SB_HAS_FLAG(SB_Weather_Flags.x, SB_WFLAG_RAINY); }
bool  SB_IsSnowing()  { return SB_HAS_FLAG(SB_Weather_Flags.x, SB_WFLAG_SNOWY); }
float SB_RainFlag()   { return SB_FLAG_TO_FLOAT(SB_Weather_Flags.x, SB_WFLAG_RAINY); }
float SB_SnowFlag()   { return SB_FLAG_TO_FLOAT(SB_Weather_Flags.x, SB_WFLAG_SNOWY); }
float SB_InteriorFlag() { return SB_FLAG_TO_FLOAT(SB_Interior_Flags.x, SB_IFLAG_IS_INTERIOR); }
bool  SB_IsInterior()   { return SB_HAS_FLAG(SB_Interior_Flags.x, SB_IFLAG_IS_INTERIOR); }

// Feedback helpers (1-frame delayed GPU read-back)
bool SB_HasFeedback()       { return SB_Computed_Scene.w > 0.5; }
float SB_CenterLuminance()  { return SB_Computed_Luminance.x; }
float3 SB_CenterColor()     { return float3(SB_Computed_Luminance.z, SB_Computed_Luminance.w, SB_Computed_Scene.x); }
float SB_SceneAvgLuminance(){ return SB_Computed_Scene.y; }
float SB_SceneKeyValue()    { return SB_Computed_SceneStats.x; }
float SB_ContrastRatio()    { return SB_Computed_SceneStats.y; }
float3 SB_SceneAvgColor()   { return float3(SB_Computed_SceneColor.xyz); }
float SB_ColorTemperature() { return SB_Computed_SceneColor.w; }

// Histogram + temporal helpers (enhanced scene analysis)
float4 SB_LuminanceHistogram() { return SB_Computed_Histogram; }
bool   SB_IsSceneCut()         { return SB_Computed_Temporal.x > 0.5; }
float  SB_LumVelocity()        { return SB_Computed_Temporal.y; }
float  SB_ColorShift()         { return SB_Computed_Temporal.z; }
float  SB_SceneStability()     { return SB_Computed_Temporal.w; }

// Time-of-day segment helpers (smoothstep interpolators [0,1])
float SB_Dawn()       { return SB_Time_Segments1.x; }
float SB_Sunrise()    { return SB_Time_Segments1.y; }
float SB_Day()        { return SB_Time_Segments1.z; }
float SB_Sunset()     { return SB_Time_Segments1.w; }
float SB_Dusk()       { return SB_Time_Segments2.x; }
float SB_Night()      { return SB_Time_Segments2.y; }
float SB_GoldenHour() { return SB_Time_Segments2.z; }
float SB_BlueHour()   { return SB_Time_Segments2.w; }

// NPC threat helpers
bool SB_HasNearbyHostile() { return SB_NPC_Summary.x > 0.5; }
bool SB_IsInDanger()       { return SB_NPC_Threat.x > 0.3; }

// Performance helper: quality scale for LOD/effect quality adjustment
float SB_QualityScale() { return SB_Perf_Budget.y; }

// Region helpers
bool SB_HasWeatherOverride() { return SB_Region_Region.y > 0.5; }
bool SB_HasCombatMusic()     { return SB_Audio_Music.z > 0.5; }
bool SB_IsDungeon()          { return SB_Audio_Music.w > 0.5; }

// Scene composition helpers (material fractions of total lighting draws)
float SB_SkinFraction()       { return SB_Scene_MatCount1.y; }
float SB_TerrainFraction()    { return SB_Scene_MatCount1.z; }
float SB_VegetationFraction() { return SB_Scene_MatCount1.w; }
bool  SB_HasCharLight()       { return SB_Scene_CharLight.x > 0.5; }
float3 SB_AmbientSpecular()   { return SB_Scene_AmbientSpec.xyz; }

// Tier 3a: Material property helpers
float SB_AvgSpecularPower()   { return SB_Scene_MatProps1.x; }
float SB_AvgSpecularScale()   { return SB_Scene_MatProps1.y; }
float SB_AvgRoughness()       { return SB_Scene_MatProps1.z; }
float SB_AvgSubSurface()      { return SB_Scene_MatProps1.w; }
float SB_AvgRimLight()        { return SB_Scene_MatProps2.x; }
float SB_AvgEnvMapScale()     { return SB_Scene_MatProps2.y; }
float SB_AvgMaterialAlpha()   { return SB_Scene_MatProps2.z; }
float SB_SkinSpecPower()      { return SB_Scene_MatProps2.w; }
float SB_EnvMapFraction()     { return SB_Scene_ShaderFlags.x; }
float SB_GlowMapFraction()    { return SB_Scene_ShaderFlags.y; }

// Tier A: Live weather helpers (real-time Sky singleton data)
float2 SB_LiveWindDir()       { return float2(SB_Wind_Live.z, SB_Wind_Live.w); }
float  SB_LiveWindSpeed()     { return SB_Wind_Live.x; }
float  SB_LiveWindAngle()     { return SB_Wind_Live.y; }
float  SB_ParticleDensity()   { return SB_Precip_Live.x; }
float  SB_FlashIntensity()    { return SB_Precip_Live.z; }
float  SB_CloudCoverage()     { return SB_Cloud_Cover.x; }
float  SB_CloudLayerCount()   { return SB_Cloud_Cover.y; }
float  SB_MaxCloudAlpha()     { return SB_Cloud_Cover.z; }
float  SB_AuroraFadeIn()      { return SB_Aurora_Fade.x; }
float  SB_AuroraFadeOut()     { return SB_Aurora_Fade.y; }
bool   SB_AuroraActive()      { return SB_Aurora_Fade.x > 0.001 || SB_Aurora_Fade.y > 0.001; }

// Tier A: Engine state helpers
bool   SB_EngineInterior()    { return SB_Engine_State.x > 0.5; }
float  SB_WaterIntersect()    { return SB_Engine_State.z; }
float  SB_TimerDefault()      { return SB_Engine_Timers.x; }
float  SB_TimerDelta()        { return SB_Engine_Timers.y; }
float  SB_TimerRealDelta()    { return SB_Engine_Timers.w; }

// Tier A: Directional ambient — 6-axis cube lighting
float3 SB_DirAmbientXPos()    { return SB_DirAmbient_X.rgb; }
float3 SB_DirAmbientYPos()    { return SB_DirAmbient_Y.rgb; }
float3 SB_DirAmbientZPos()    { return SB_DirAmbient_Z.rgb; }
// Evaluate directional ambient for a given world-space normal
float3 SB_EvalDirAmbient(float3 n)
{
    float3 xPos = SB_DirAmbient_X.rgb;
    float3 yPos = SB_DirAmbient_Y.rgb;
    float3 zPos = SB_DirAmbient_Z.rgb;
    float xNegScale = SB_DirAmbient_X.w / max(dot(xPos, float3(0.2126, 0.7152, 0.0722)), 0.001);
    float yNegScale = SB_DirAmbient_Y.w / max(dot(yPos, float3(0.2126, 0.7152, 0.0722)), 0.001);
    float zNegScale = SB_DirAmbient_Z.w / max(dot(zPos, float3(0.2126, 0.7152, 0.0722)), 0.001);
    float3 result = float3(0, 0, 0);
    result += saturate( n.x) * xPos + saturate(-n.x) * xPos * xNegScale;
    result += saturate( n.y) * yPos + saturate(-n.y) * yPos * yNegScale;
    result += saturate( n.z) * zPos + saturate(-n.z) * zPos * zNegScale;
    return result;
}

// Sun glare / shadow scene helpers
float  SB_SunGlareScale()     { return SB_Sun_Glare.x; }
float  SB_ActiveLightCount()  { return SB_Sun_Glare.z; }
float  SB_ShadowCasterCount() { return SB_Sun_Glare.w; }

// Tier B: Per-draw geometry helpers
float  SB_AvgLightsPerDraw()  { return SB_Scene_GeomInfo.x; }
float  SB_MaxLightsPerDraw()  { return SB_Scene_GeomInfo.y; }

// Tier B: Water shader helpers
float3 SB_WaterPlaneNormal()  { return SB_Water_Plane.xyz; }
float  SB_WaterPlaneD()       { return SB_Water_Plane.w; }
float3 SB_ShallowWaterColor() { return SB_Water_Color.rgb; }
float  SB_WaterAlpha()        { return SB_Water_Color.a; }
float  SB_WaterSunSpecPower() { return SB_Water_Params.x; }
float  SB_WaterReflection()   { return SB_Water_Params.y; }
float  SB_WaterRefraction()   { return SB_Water_Params.z; }
float  SB_WaterFresnel()      { return SB_Water_Params.w; }

// Tier B: Effect shader helpers
float  SB_EffectDrawCount()   { return SB_Effect_Shader.x; }
float3 SB_EffectAvgColor()    { return SB_Effect_Color.rgb; }
bool   SB_HasActiveEffects()  { return SB_Effect_Shader.x > 0.5; }

// Tier C: ENB readback helpers (FeedbackConfig.ini slot mapping)
float  SB_ENBAdaptedLum()     { return SB_ENB_Readback.x; }
float  SB_ENBBloomIntensity() { return SB_ENB_Readback.y; }
float  SB_ENBReadbackSlot(int i) { return (i < 4) ? SB_ENB_Readback[i] : SB_ENB_Readback4[i-4]; }

// Render state helpers
float  SB_TimeDilation()       { return SB_Render_Jitter.w; }
bool   SB_IsSlowMotion()       { return SB_Render_Jitter.w < 0.9 && SB_Render_Jitter.w > 0.001; }
bool   SB_IsGamePaused()       { return SB_Render_StencilInfo.w > 0.5; }

// Player combat helpers
bool   SB_IsWerewolf()         { return SB_Player_Combat.y > 0.5 && SB_Player_Combat.y < 1.5; }
bool   SB_IsVampireLord()      { return SB_Player_Combat.y > 1.5; }
bool   SB_IsBeastForm()        { return SB_Player_Combat.y > 0.5; }
float  SB_GameTimeScale()      { return SB_Player_Combat.z; }
float  SB_CombatTargetCount()  { return SB_Player_Combat.w; }

// NPC density helpers
float  SB_HighActorCount()     { return SB_NPC_Threat.z; }
float  SB_MaxDetectionLevel()  { return SB_NPC_Threat.w; }
bool   SB_IsDetected()         { return SB_NPC_Threat.w > 50.0; }

// Camera convenience accessors
float  SB_FOVRadians()         { return SB_Camera_Params.x; }
float  SB_FOVDegrees()         { return SB_Camera_Params.x * 57.2957795; }
float  SB_NearClip()           { return SB_Camera_Params.y; }
float  SB_FarClip()            { return SB_Camera_Params.z; }
float  SB_AspectRatio()        { return SB_Camera_Params.w; }
float3 SB_CameraPos()          { return SB_Camera_WorldPos.xyz; }

//-----------------------------------------------------------------------------
//  GPU compute textures — injected by SkyrimBridge C++ backend
//
//  These are bound via SRVInjector at high t-slots that ENB doesn't touch.
//  Available in all ENB pipeline shaders (enbeffectprepass through enbunderwater).
//-----------------------------------------------------------------------------

#ifdef SB_DECLARE_COMPUTE_TEXTURES

Texture2D<float>  SB_Histogram      : register(t17); // 256x1 R32_FLOAT luminance histogram
Texture2D<float>  SB_HiZPyramid     : register(t19); // R32_FLOAT hierarchical depth (mipped)
Texture2D<float4> SB_TemporalHistory : register(t22); // R16G16B16A16F resolved previous frame
SamplerState      SB_HistorySampler  : register(s3);  // Linear clamp for temporal reads

// Sample the TAA-resolved previous frame at a UV coordinate
float3 SB_SampleHistory(float2 uv)
{
    return SB_TemporalHistory.SampleLevel(SB_HistorySampler, uv, 0).rgb;
}

// Read a specific bin from the luminance histogram (bin 0..255)
float SB_ReadHistogramBin(uint bin)
{
    return SB_Histogram.Load(int3(bin, 0, 0));
}

// Sample Hi-Z pyramid at a given mip level (conservative depth for SSR/SSAO)
float SB_SampleHiZ(int2 coord, int mipLevel)
{
    return SB_HiZPyramid.Load(int3(coord, mipLevel));
}

#endif // SB_DECLARE_COMPUTE_TEXTURES

#endif // SKYRIMBRIDGE_CB_FXH
