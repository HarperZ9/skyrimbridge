//═════════════════════════════════════════════════════════════════════════════
//
//  enbUI_SkyrimBridge.fxh — SkyrimBridge Data Monitor for ENB GUI
//
//  Creates a read-only monitoring section in the ENB shader editor that
//  displays live SkyrimBridge data. This serves two critical purposes:
//
//    1. VERIFICATION — Confirms that ENBSetParameter is successfully
//       writing data into the shader's constant buffer. If values remain
//       at 0.0 while the ImGui debug overlay shows data, the KeepAlive
//       system or parameter names have a mismatch.
//
//    2. DEBUGGING — Shader authors can see exact game-state values
//       without opening the ImGui overlay, directly alongside the
//       shader parameters that consume them.
//
//  Include this header AFTER SkyrimBridge.fxh and enbUI_Primer.fxh.
//  Wrap in #if SB_ENABLE_MONITOR to make inclusion optional.
//
//  Author: Zain Dana Harper
//  Version: 2.0.0
//═════════════════════════════════════════════════════════════════════════════

#ifndef _UI_SKYRIMBRIDGE_MONITOR_
#define _UI_SKYRIMBRIDGE_MONITOR_

#ifndef _UI_PRIMER_
#error enbUI_SkyrimBridge.fxh requires enbUI_Primer.fxh — include it first.
#endif

#ifndef SKYRIMBRIDGE_FXH
#error enbUI_SkyrimBridge.fxh requires SkyrimBridge.fxh — include it first.
#endif


//─────────────────────────────────────────────────────────────────────────────
//  MONITOR SECTION HEADER
//─────────────────────────────────────────────────────────────────────────────

UI_SeparatorThick()
UI_Section("SkyrimBridge Monitor v2.0")
UI_Space()

// Master enable — controls whether the monitor pixel shader runs
UI_BOOL(_SBMon_Enable, "\xFE SkyrimBridge Monitor - Enable", false)

UI_Space()


//─────────────────────────────────────────────────────────────────────────────
//  CONNECTION STATUS
//─────────────────────────────────────────────────────────────────────────────

UI_SubSection("Connection Status")

// These float monitors will show non-zero when data is flowing
UI_MONITOR_FLOAT(_SBMon_FrameCount,    "Frame Count",      0.0, 1048576.0)
UI_MONITOR_FLOAT(_SBMon_DeltaTime,     "Delta Time (s)",   0.0, 1.0)
UI_MONITOR_FLOAT(_SBMon_ScreenW,       "Screen Width",     0.0, 7680.0)
UI_MONITOR_FLOAT(_SBMon_ScreenH,       "Screen Height",    0.0, 4320.0)

UI_Space()


//─────────────────────────────────────────────────────────────────────────────
//  CELESTIAL
//─────────────────────────────────────────────────────────────────────────────

UI_SubSection("Celestial")

UI_MONITOR_FLOAT(_SBMon_GameHour,      "Game Hour [0-24]",   0.0, 24.0)
UI_MONITOR_FLOAT(_SBMon_SunElev,       "Sun Elevation (rad)", -1.6, 1.6)
UI_MONITOR_BOOL (_SBMon_SunOnScreen,   "Sun On Screen")
UI_MONITOR_FLOAT(_SBMon_MasserPhase,   "Masser Phase",       0.0, 1.0)
UI_MONITOR_FLOAT(_SBMon_SecundaPhase,  "Secunda Phase",      0.0, 1.0)

UI_Space()


//─────────────────────────────────────────────────────────────────────────────
//  ATMOSPHERE & FOG
//─────────────────────────────────────────────────────────────────────────────

UI_SubSection("Atmosphere / Fog")

UI_MONITOR_FLOAT(_SBMon_AmbientInt,    "Ambient Intensity",    0.0, 5.0)
UI_MONITOR_FLOAT(_SBMon_SunlightScale, "Sunlight Scale",       0.0, 5.0)
UI_MONITOR_FLOAT(_SBMon_FogNearDist,   "Fog Near Dist",        0.0, 100000.0)
UI_MONITOR_FLOAT(_SBMon_FogFarDist,    "Fog Far Dist",         0.0, 100000.0)
UI_MONITOR_FLOAT(_SBMon_FogDensity,    "Fog Density Power",    0.0, 10.0)
UI_MONITOR_FLOAT(_SBMon_PlayerAlt,     "Player Altitude",     -5000.0, 50000.0)

UI_Space()


//─────────────────────────────────────────────────────────────────────────────
//  WEATHER
//─────────────────────────────────────────────────────────────────────────────

UI_SubSection("Weather")

UI_MONITOR_FLOAT(_SBMon_WindSpeed,     "Wind Speed [0-1]",   0.0, 1.0)
UI_MONITOR_FLOAT(_SBMon_WindDir,       "Wind Direction (rad)", -3.2, 3.2)
UI_MONITOR_FLOAT(_SBMon_PrecipType,    "Precip Type (0/1/2)",  0.0, 2.0)
UI_MONITOR_FLOAT(_SBMon_PrecipInt,     "Precip Intensity",     0.0, 1.0)
UI_MONITOR_FLOAT(_SBMon_LightningFlash,"Lightning Flash",      0.0, 1.0)
UI_MONITOR_FLOAT(_SBMon_SfcWetness,    "Surface Wetness",      0.0, 1.0)
UI_MONITOR_FLOAT(_SBMon_WxTransition,  "Weather Transition %", 0.0, 1.0)
UI_MONITOR_BOOL (_SBMon_IsPleasant,    "Pleasant")
UI_MONITOR_BOOL (_SBMon_IsCloudy,      "Cloudy")
UI_MONITOR_BOOL (_SBMon_IsRainy,       "Rainy")
UI_MONITOR_BOOL (_SBMon_IsSnowy,       "Snowy")

UI_Space()


//─────────────────────────────────────────────────────────────────────────────
//  PLAYER
//─────────────────────────────────────────────────────────────────────────────

UI_SubSection("Player")

UI_MONITOR_FLOAT(_SBMon_Health,        "Health %",           0.0, 1.0)
UI_MONITOR_FLOAT(_SBMon_Stamina,       "Stamina %",          0.0, 1.0)
UI_MONITOR_FLOAT(_SBMon_Magicka,       "Magicka %",          0.0, 1.0)
UI_MONITOR_FLOAT(_SBMon_Speed,         "Speed (units/s)",    0.0, 2000.0)
UI_MONITOR_BOOL (_SBMon_InCombat,      "In Combat")
UI_MONITOR_BOOL (_SBMon_Underwater,    "Underwater")
UI_MONITOR_FLOAT(_SBMon_Submersion,    "Submersion Depth",   0.0, 5000.0)

UI_Space()


//─────────────────────────────────────────────────────────────────────────────
//  CAMERA
//─────────────────────────────────────────────────────────────────────────────

UI_SubSection("Camera")

UI_MONITOR_FLOAT(_SBMon_FOV,           "FOV (deg)",          10.0, 180.0)
UI_MONITOR_FLOAT(_SBMon_NearClip,      "Near Clip",          0.0, 100.0)
UI_MONITOR_FLOAT(_SBMon_FarClip,       "Far Clip",           0.0, 500000.0)
UI_MONITOR_FLOAT(_SBMon_Aspect,        "Aspect Ratio",       0.5, 4.0)
UI_MONITOR_FLOAT(_SBMon_CamPitch,      "Pitch (rad)",       -3.2, 3.2)
UI_MONITOR_FLOAT(_SBMon_CamYaw,        "Yaw (rad)",         -3.2, 3.2)

UI_Space()


//─────────────────────────────────────────────────────────────────────────────
//  INTERIOR LIGHTING
//─────────────────────────────────────────────────────────────────────────────

UI_SubSection("Interior Lighting")

UI_MONITOR_BOOL (_SBMon_IsInterior,    "Is Interior")
UI_MONITOR_FLOAT(_SBMon_IntAmbientR,   "Ambient R",          0.0, 2.0)
UI_MONITOR_FLOAT(_SBMon_IntAmbientG,   "Ambient G",          0.0, 2.0)
UI_MONITOR_FLOAT(_SBMon_IntAmbientB,   "Ambient B",          0.0, 2.0)
UI_MONITOR_FLOAT(_SBMon_IntFogNear,    "Interior Fog Near",  0.0, 100000.0)
UI_MONITOR_FLOAT(_SBMon_IntFogFar,     "Interior Fog Far",   0.0, 100000.0)

UI_Space()


//─────────────────────────────────────────────────────────────────────────────
//  MAGIC EFFECTS & IMAGE SPACE
//─────────────────────────────────────────────────────────────────────────────

UI_SubSection("Effects / ImageSpace")

UI_MONITOR_BOOL (_SBMon_NightEye,      "Night Eye")
UI_MONITOR_BOOL (_SBMon_DetectLife,     "Detect Life")
UI_MONITOR_BOOL (_SBMon_Ethereal,      "Ethereal")
UI_MONITOR_BOOL (_SBMon_Invisible,     "Invisible")
UI_MONITOR_FLOAT(_SBMon_SlowTime,      "Slow Time Factor",   0.0, 1.0)
UI_MONITOR_FLOAT(_SBMon_ISSaturation,  "IS Saturation",      0.0, 3.0)
UI_MONITOR_FLOAT(_SBMon_ISBrightness,  "IS Brightness",      0.0, 3.0)
UI_MONITOR_FLOAT(_SBMon_ISContrast,    "IS Contrast",        0.0, 3.0)
UI_MONITOR_BOOL (_SBMon_HasIMOD,       "Active IMOD")

UI_Space()


//─────────────────────────────────────────────────────────────────────────────
//  NEARBY LIGHTS
//─────────────────────────────────────────────────────────────────────────────

UI_SubSection("Nearby Lights")

UI_MONITOR_FLOAT(_SBMon_LightCount,    "Nearby Light Count", 0.0, 3.0)
UI_MONITOR_FLOAT(_SBMon_NearestDist,   "Nearest Distance",   0.0, 10000.0)
UI_MONITOR_FLOAT(_SBMon_TotalFlux,     "Total Luminous Flux", 0.0, 10.0)

UI_Space()


//─────────────────────────────────────────────────────────────────────────────
//  CROSSHAIR / TARGET
//─────────────────────────────────────────────────────────────────────────────

UI_SubSection("Crosshair Target")

UI_MONITOR_BOOL (_SBMon_HasTarget,     "Has Target")
UI_MONITOR_FLOAT(_SBMon_TargetDist,    "Target Distance",    0.0, 50000.0)
UI_MONITOR_BOOL (_SBMon_TargetIsActor, "Target Is Actor")
UI_MONITOR_FLOAT(_SBMon_TargetHealth,  "Target Health %",    0.0, 1.0)
UI_MONITOR_BOOL (_SBMon_TargetHostile, "Target Hostile")

UI_Space()


//─────────────────────────────────────────────────────────────────────────────
//  EQUIPMENT
//─────────────────────────────────────────────────────────────────────────────

UI_SubSection("Equipment")

UI_MONITOR_FLOAT(_SBMon_WeaponType,    "Weapon Type",        0.0, 21.0)
UI_MONITOR_BOOL (_SBMon_WeaponDrawn,   "Weapon Drawn")
UI_MONITOR_BOOL (_SBMon_HasBow,        "Has Bow")
UI_MONITOR_BOOL (_SBMon_HasTorch,      "Has Torch")
UI_MONITOR_FLOAT(_SBMon_ArmorRating,   "Armor Rating",       0.0, 1000.0)

UI_Space()


//─────────────────────────────────────────────────────────────────────────────
//  UI / MENU STATE
//─────────────────────────────────────────────────────────────────────────────

UI_SubSection("UI State")

UI_MONITOR_BOOL (_SBMon_InMenu,        "In Menu")
UI_MONITOR_BOOL (_SBMon_InDialogue,    "In Dialogue")
UI_MONITOR_BOOL (_SBMon_Loading,       "Loading")
UI_MONITOR_BOOL (_SBMon_CinematicMode, "Cinematic Mode")

UI_SeparatorThick()
UI_Space()


//═════════════════════════════════════════════════════════════════════════════
//
//  MONITOR UPDATE FUNCTION
//
//  Call this in a pixel shader to copy SkyrimBridge data into the monitor
//  variables. The ENB GUI displays the monitor variable values.
//
//  Usage: In any PS that runs every frame (e.g., the main composite pass):
//
//    if (_SBMon_Enable) SB_UpdateMonitor();
//
//  NOTE: Only the first pixel to execute each frame matters, since all
//  pixels write the same global data. The monitor just needs to run once.
//
//═════════════════════════════════════════════════════════════════════════════

void SB_UpdateMonitor()
{
    // Connection Status
    _SBMon_FrameCount   = SB_Render_Frame.x;
    _SBMon_DeltaTime    = SB_Render_Frame.y;
    _SBMon_ScreenW      = SB_Render_Frame.z;
    _SBMon_ScreenH      = SB_Render_Frame.w;

    // Celestial
    _SBMon_GameHour     = SB_Time.x;
    _SBMon_SunElev      = SB_Sun_NDC.w;
    _SBMon_SunOnScreen  = SB_Sun_NDC.z > 0.5;
    _SBMon_MasserPhase  = SB_Masser_NDC.w;
    _SBMon_SecundaPhase = SB_Secunda_NDC.w;

    // Atmosphere / Fog
    _SBMon_AmbientInt   = SB_Atmos_Ambient.a;
    _SBMon_SunlightScale= SB_Atmos_Sunlight.a;
    _SBMon_FogNearDist  = SB_Fog_NearColor.a;
    _SBMon_FogFarDist   = SB_Fog_FarColor.a;
    _SBMon_FogDensity   = SB_Fog_Density.x;
    _SBMon_PlayerAlt    = SB_Fog_Height.y;

    // Weather
    _SBMon_WindSpeed    = SB_Wind.x;
    _SBMon_WindDir      = SB_Wind.y;
    _SBMon_PrecipType   = SB_Precipitation.x;
    _SBMon_PrecipInt    = SB_Precipitation.y;
    _SBMon_LightningFlash = SB_Lightning.z;
    _SBMon_SfcWetness   = SB_Precip_Surface.x;
    _SBMon_WxTransition = SB_Weather_Transition.x;
    _SBMon_IsPleasant   = SB_Weather_Flags.x > 0.5;
    _SBMon_IsCloudy     = SB_Weather_Flags.y > 0.5;
    _SBMon_IsRainy      = SB_Weather_Flags.z > 0.5;
    _SBMon_IsSnowy      = SB_Weather_Flags.w > 0.5;

    // Player
    _SBMon_Health       = SB_Player_Vitals.x;
    _SBMon_Stamina      = SB_Player_Vitals.y;
    _SBMon_Magicka      = SB_Player_Vitals.z;
    _SBMon_Speed        = SB_Player_Movement.x;
    _SBMon_InCombat     = SB_Player_Combat.x > 0.5;
    _SBMon_Underwater   = SB_Player_Water.x > 0.5;
    _SBMon_Submersion   = SB_Player_Water.z;

    // Camera
    _SBMon_FOV          = SB_Camera_Info.x;
    _SBMon_NearClip     = SB_Camera_Info.y;
    _SBMon_FarClip      = SB_Camera_Info.z;
    _SBMon_Aspect       = SB_Camera_Info.w;
    _SBMon_CamPitch     = SB_Camera_Angles.x;
    _SBMon_CamYaw       = SB_Camera_Angles.y;

    // Interior
    _SBMon_IsInterior   = SB_Interior_Flags.x > 0.5;
    _SBMon_IntAmbientR  = SB_Interior_Ambient.r;
    _SBMon_IntAmbientG  = SB_Interior_Ambient.g;
    _SBMon_IntAmbientB  = SB_Interior_Ambient.b;
    _SBMon_IntFogNear   = SB_Interior_FogDist.x;
    _SBMon_IntFogFar    = SB_Interior_FogDist.y;

    // Effects / ImageSpace
    _SBMon_NightEye     = SB_FX_Vision.x > 0.5;
    _SBMon_DetectLife   = SB_FX_Vision.y > 0.5;
    _SBMon_Ethereal     = SB_FX_Vision.w > 0.5;
    _SBMon_Invisible    = SB_FX_Misc.x > 0.5;
    _SBMon_SlowTime     = SB_FX_Time.x;
    _SBMon_ISSaturation = SB_IS_Cinematic.x;
    _SBMon_ISBrightness = SB_IS_Cinematic.y;
    _SBMon_ISContrast   = SB_IS_Cinematic.z;
    _SBMon_HasIMOD      = SB_IS_IMOD.x > 0.5;

    // Nearby Lights
    _SBMon_LightCount   = SB_Light_Summary.x;
    _SBMon_NearestDist  = SB_Light_Summary.y;
    _SBMon_TotalFlux    = SB_Light_Summary.z;

    // Crosshair
    _SBMon_HasTarget    = SB_XHair_Info.x > 0.5;
    _SBMon_TargetDist   = SB_XHair_Info.y;
    _SBMon_TargetIsActor= SB_XHair_Info.w > 0.5;
    _SBMon_TargetHealth = SB_XHair_Actor.x;
    _SBMon_TargetHostile= SB_XHair_Actor.z > 0.5;

    // Equipment
    _SBMon_WeaponType   = SB_Equip_Right.x;
    _SBMon_WeaponDrawn  = SB_Equip_Flags.x > 0.5;
    _SBMon_HasBow       = SB_Equip_Flags.y > 0.5;
    _SBMon_HasTorch     = SB_Equip_Flags.z > 0.5;
    _SBMon_ArmorRating  = SB_Equip_Armor.x;

    // UI State
    _SBMon_InMenu       = SB_UI_Menus.x > 0.5;
    _SBMon_InDialogue   = SB_UI_Menus.y > 0.5;
    _SBMon_Loading      = SB_UI_HUD.w > 0.5;
    _SBMon_CinematicMode= SB_UI_HUD.z > 0.5;
}


#endif // _UI_SKYRIMBRIDGE_MONITOR_
