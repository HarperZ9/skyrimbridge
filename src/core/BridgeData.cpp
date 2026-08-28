#include "BridgeData.h"
#include <cmath>
#include <cstddef>

//=============================================================================
//  BridgeData.cpp — Parameter name table for ENB push
//
//  Each entry maps a Float4 in AllData to the string name the shader uses.
//  The push loop reads: for each entry, memcpy 16 bytes from AllData + offset,
//  call ENBSetParameter with the name string.
//
//  For Float4x4 (matrices), we push 4 consecutive Float4 rows.
//=============================================================================

#define ENTRY(field, name) \
    { name, offsetof(SB::AllData, field) }

namespace SB
{
    const ParamEntry kParamTable[] = {
        // ── Celestial ───────────────────────────────────────────────────
        // NDC params live in DerivedData (end of AllData) so single-pass
        // stages get screen positions without per-pixel VP math; the table
        // groups them here because push order is name-keyed, not offset-keyed.
        ENTRY(derived.SunNDC,               "SB_Sun_NDC"),
        ENTRY(derived.MasserNDC,            "SB_Masser_NDC"),
        ENTRY(derived.SecundaNDC,           "SB_Secunda_NDC"),
        ENTRY(celestial.SunDirection,       "SB_Sun_Direction"),
        ENTRY(celestial.SunColor,           "SB_Sun_Color"),
        ENTRY(celestial.MasserDirection,    "SB_Masser_Direction"),
        ENTRY(celestial.SecundaDirection,   "SB_Secunda_Direction"),
        ENTRY(celestial.TimeData,           "SB_Time"),
        ENTRY(celestial.TimeSegments1,      "SB_Time_Segments1"),
        ENTRY(celestial.TimeSegments2,      "SB_Time_Segments2"),

        // ── Atmosphere ──────────────────────────────────────────────────
        ENTRY(atmosphere.SkyUpper,          "SB_Atmos_SkyUpper"),
        ENTRY(atmosphere.SkyLower,          "SB_Atmos_SkyLower"),
        ENTRY(atmosphere.Horizon,           "SB_Atmos_Horizon"),
        ENTRY(atmosphere.Ambient,           "SB_Atmos_Ambient"),
        ENTRY(atmosphere.SunlightColor,     "SB_Atmos_Sunlight"),
        ENTRY(atmosphere.CloudLODDiffuse,   "SB_Atmos_CloudDiffuse"),
        ENTRY(atmosphere.CloudLODAmbient,   "SB_Atmos_CloudAmbient"),
        ENTRY(atmosphere.EffectLighting,    "SB_Atmos_EffectLight"),

        // ── Fog ─────────────────────────────────────────────────────────
        ENTRY(fog.NearColor,                "SB_Fog_NearColor"),
        ENTRY(fog.FarColor,                 "SB_Fog_FarColor"),
        ENTRY(fog.Density,                  "SB_Fog_Density"),
        ENTRY(fog.HeightFog,                "SB_Fog_Height"),

        // ── Weather ─────────────────────────────────────────────────────
        ENTRY(weather.Wind,                 "SB_Wind"),
        ENTRY(weather.Precipitation,        "SB_Precipitation"),
        ENTRY(weather.Lightning,            "SB_Lightning"),
        ENTRY(weather.Flags,                "SB_Weather_Flags"),
        ENTRY(weather.Transition,           "SB_Weather_Transition"),
        ENTRY(weather.PrecipSurface,        "SB_Precip_Surface"),
        ENTRY(weather.WindLive,             "SB_Wind_Live"),
        ENTRY(weather.PrecipLive,           "SB_Precip_Live"),
        ENTRY(weather.CloudCover,           "SB_Cloud_Cover"),
        ENTRY(weather.AuroraFade,           "SB_Aurora_Fade"),

        // ── Player ──────────────────────────────────────────────────────
        ENTRY(player.Position,              "SB_Player_Position"),
        ENTRY(player.Vitals,                "SB_Player_Vitals"),
        ENTRY(player.Movement,              "SB_Player_Movement"),
        ENTRY(player.Combat,                "SB_Player_Combat"),
        ENTRY(player.Water,                 "SB_Player_Water"),

        // ── Camera (optimized: 8 params, rest derivable in shader) ──────
        ENTRY(camera.Params,                "SB_Camera_Params"),
        ENTRY(camera.WorldPos,              "SB_Camera_WorldPos"),
        ENTRY(camera.ViewRow0,              "SB_View_Row0"),
        ENTRY(camera.ViewRow1,              "SB_View_Row1"),
        ENTRY(camera.ViewRow2,              "SB_View_Row2"),
        ENTRY(camera.PrevWorldPos,          "SB_PrevCamera_Pos"),
        ENTRY(camera.PrevViewRow0,          "SB_PrevView_Row0"),
        ENTRY(camera.PrevViewRow1,          "SB_PrevView_Row1"),

        // ── Interior ────────────────────────────────────────────────────
        ENTRY(interior.IsInterior,          "SB_Interior_Flags"),
        ENTRY(interior.AmbientColor,        "SB_Interior_Ambient"),
        ENTRY(interior.DirectionalColor,    "SB_Interior_DirColor"),
        ENTRY(interior.DirectionalDir,      "SB_Interior_DirDir"),
        ENTRY(interior.InteriorFogColor,    "SB_Interior_FogColor"),
        ENTRY(interior.InteriorFogDist,     "SB_Interior_FogDist"),
        ENTRY(interior.LightingTemplate,    "SB_Interior_Template"),

        // ── Shadow/Directional ──────────────────────────────────────────
        ENTRY(shadow.LightDirection,        "SB_Shadow_Direction"),
        ENTRY(shadow.LightDiffuse,          "SB_Shadow_Diffuse"),
        ENTRY(shadow.LightAmbient,          "SB_Shadow_Ambient"),

        // ── Active Effects ──────────────────────────────────────────────
        ENTRY(effects.VisionEffects,        "SB_FX_Vision"),
        ENTRY(effects.TimeEffects,          "SB_FX_Time"),
        ENTRY(effects.DamageEffects,        "SB_FX_Damage"),
        ENTRY(effects.MiscEffects,          "SB_FX_Misc"),

        // ── Render State ────────────────────────────────────────────────
        // DepthParams removed — derivable from Camera_Params (near/far)
        ENTRY(render.FrameInfo,             "SB_Render_Frame"),
        ENTRY(render.Jitter,                "SB_Render_Jitter"),
        ENTRY(render.StencilInfo,           "SB_Render_StencilInfo"),

        // ── ImageSpace ─────────────────────────────────────────────────
        ENTRY(imageSpace.HDR,               "SB_IS_HDR"),
        ENTRY(imageSpace.Cinematic,         "SB_IS_Cinematic"),
        ENTRY(imageSpace.CineTint,          "SB_IS_CineTint"),
        ENTRY(imageSpace.DOF,               "SB_IS_DOF"),
        ENTRY(imageSpace.IMOD,              "SB_IS_IMOD"),
        ENTRY(imageSpace.IMODTint,          "SB_IS_IMODTint"),

        // ── Nearby Lights ──────────────────────────────────────────────
        ENTRY(lights.Light0PosRad,          "SB_Light0_PosRad"),
        ENTRY(lights.Light0Color,           "SB_Light0_Color"),
        ENTRY(lights.Light1PosRad,          "SB_Light1_PosRad"),
        ENTRY(lights.Light1Color,           "SB_Light1_Color"),
        ENTRY(lights.Light2PosRad,          "SB_Light2_PosRad"),
        ENTRY(lights.Light2Color,           "SB_Light2_Color"),
        ENTRY(lights.Summary,               "SB_Light_Summary"),

        // ── Actor Values ───────────────────────────────────────────────
        ENTRY(actorValues.Resist,           "SB_AV_Resist"),
        ENTRY(actorValues.Resist2,          "SB_AV_Resist2"),
        ENTRY(actorValues.Combat,           "SB_AV_Combat"),
        ENTRY(actorValues.Movement,         "SB_AV_Movement"),
        ENTRY(actorValues.SkillCombat,      "SB_AV_SkillCombat"),
        ENTRY(actorValues.SkillMagic,       "SB_AV_SkillMagic"),
        ENTRY(actorValues.SkillMagic2,      "SB_AV_SkillMagic2"),
        ENTRY(actorValues.SkillStealth,     "SB_AV_SkillStealth"),

        // ── Crosshair ─────────────────────────────────────────────────
        ENTRY(crosshair.Info,               "SB_XHair_Info"),
        ENTRY(crosshair.Pos,                "SB_XHair_Pos"),
        ENTRY(crosshair.Actor,              "SB_XHair_Actor"),

        // ── Equipment ──────────────────────────────────────────────────
        ENTRY(equipment.Right,              "SB_Equip_Right"),
        ENTRY(equipment.Left,               "SB_Equip_Left"),
        ENTRY(equipment.Armor,              "SB_Equip_Armor"),
        ENTRY(equipment.Flags,              "SB_Equip_Flags"),

        // ── Quest ──────────────────────────────────────────────────────
        ENTRY(quest.Progress,               "SB_Quest_Progress"),
        ENTRY(quest.Tracked,                "SB_Quest_Tracked"),

        // ── UI State ───────────────────────────────────────────────────
        ENTRY(uiState.Menus,                "SB_UI_Menus"),
        ENTRY(uiState.HUD,                  "SB_UI_HUD"),
        ENTRY(uiState.Detail,               "SB_UI_Detail"),

        // ── Computed Feedback ─────────────────────────────────────────
        ENTRY(feedback.Luminance,           "SB_Computed_Luminance"),
        ENTRY(feedback.Scene,               "SB_Computed_Scene"),
        ENTRY(feedback.SceneStats,          "SB_Computed_SceneStats"),
        ENTRY(feedback.SceneColor,          "SB_Computed_SceneColor"),
        ENTRY(feedback.Histogram,           "SB_Computed_Histogram"),
        ENTRY(feedback.Temporal,            "SB_Computed_Temporal"),
        ENTRY(feedback.ENBReadback,         "SB_ENB_Readback"),
        ENTRY(feedback.ENBReadback4,        "SB_ENB_Readback4"),

        // ── Region / Location ────────────────────────────────────────
        ENTRY(region.Location,              "SB_Region_Location"),
        ENTRY(region.Region,                "SB_Region_Region"),
        ENTRY(region.Worldspace,            "SB_Region_Worldspace"),

        // ── Audio / Music ────────────────────────────────────────────
        ENTRY(audio.Music,                  "SB_Audio_Music"),
        ENTRY(audio.Ambient,                "SB_Audio_Ambient"),

        // ── NPC Detection ────────────────────────────────────────────
        ENTRY(npcDetect.Nearest,            "SB_NPC_Nearest"),
        ENTRY(npcDetect.NearestPos,         "SB_NPC_NearestPos"),
        ENTRY(npcDetect.Summary,            "SB_NPC_Summary"),
        ENTRY(npcDetect.Threat,             "SB_NPC_Threat"),

        // ── Performance ──────────────────────────────────────────────
        ENTRY(perf.Timing,                  "SB_Perf_Timing"),
        ENTRY(perf.Budget,                  "SB_Perf_Budget"),

        // ── Scene Composition ───────────────────────────────────────
        ENTRY(scene.MaterialCounts1,        "SB_Scene_MatCount1"),
        ENTRY(scene.MaterialCounts2,        "SB_Scene_MatCount2"),
        ENTRY(scene.DrawStats,              "SB_Scene_DrawStats"),
        ENTRY(scene.CharLight,              "SB_Scene_CharLight"),
        ENTRY(scene.AmbientSpec,            "SB_Scene_AmbientSpec"),
        ENTRY(scene.MaterialProps1,         "SB_Scene_MatProps1"),
        ENTRY(scene.MaterialProps2,         "SB_Scene_MatProps2"),
        ENTRY(scene.ShaderFlags,            "SB_Scene_ShaderFlags"),
        ENTRY(scene.EngineState,            "SB_Engine_State"),
        ENTRY(scene.EngineTimers,           "SB_Engine_Timers"),
        ENTRY(scene.DirAmbient1,            "SB_DirAmbient_X"),
        ENTRY(scene.DirAmbient2,            "SB_DirAmbient_Y"),
        ENTRY(scene.DirAmbient3,            "SB_DirAmbient_Z"),
        ENTRY(scene.SunGlare,              "SB_Sun_Glare"),

        // Tier B: Per-draw geometry + water/effect shader observation
        ENTRY(scene.GeometryInfo,          "SB_Scene_GeomInfo"),
        ENTRY(scene.WaterPlane,            "SB_Water_Plane"),
        ENTRY(scene.WaterColor,            "SB_Water_Color"),
        ENTRY(scene.WaterParams,           "SB_Water_Params"),
        ENTRY(scene.WaterWave,             "SB_Water_Wave"),
        ENTRY(scene.EffectShader,          "SB_Effect_Shader"),
        ENTRY(scene.EffectColor,           "SB_Effect_Color"),

        // ── Theme ─────────────────────────────────────────────────────
        ENTRY(theme.Config,                "SB_Theme_Config"),
    };

    const std::size_t kParamCount = sizeof(kParamTable) / sizeof(kParamTable[0]);

    // ── Derived screen-space projections ────────────────────────────────
    // Mirrors the shader-side convention in shaders/SkyrimBridge_CB.fxh:
    // view rows are right/up/forward, FOV is radians, aspect is w/h, and
    // a projected direction lands at ndc = (vx/(vz*a*t), vy/(vz*t)) with
    // t = tan(fov/2). Translation cancels for pure directions.

    static Float4 ProjectDirection(const AllData& a_data, const Float4& a_dir, float a_passThroughW)
    {
        Float4 ndc{ 0.f, 0.f, 0.f, a_passThroughW };

        const float fov    = a_data.camera.Params.x;
        const float aspect = a_data.camera.Params.w;
        if (!(fov > 0.f) || !(aspect > 0.f))
            return ndc;

        const float t = std::tan(fov * 0.5f);
        if (!(t > 0.f))
            return ndc;

        const Float4& r = a_data.camera.ViewRow0;
        const Float4& u = a_data.camera.ViewRow1;
        const Float4& f = a_data.camera.ViewRow2;

        const float vx = r.x * a_dir.x + r.y * a_dir.y + r.z * a_dir.z;
        const float vy = u.x * a_dir.x + u.y * a_dir.y + u.z * a_dir.z;
        const float vz = f.x * a_dir.x + f.y * a_dir.y + f.z * a_dir.z;

        if (vz <= 1e-4f)   // behind or grazing the camera plane
            return ndc;

        ndc.x = vx / (vz * aspect * t);
        ndc.y = vy / (vz * t);

        // .xy stays valid slightly past the frame edge so consumers can
        // fade instead of pop; .z is the strict on-screen gate.
        const bool onScreen = std::isfinite(ndc.x) && std::isfinite(ndc.y) &&
                              std::fabs(ndc.x) <= 1.05f && std::fabs(ndc.y) <= 1.05f;
        ndc.z = onScreen ? 1.f : 0.f;
        return ndc;
    }

    void ComputeDerivedData(AllData& a_data)
    {
        auto& c = a_data.celestial;
        a_data.derived.SunNDC     = ProjectDirection(a_data, c.SunDirection,     c.SunDirection.w);
        a_data.derived.MasserNDC  = ProjectDirection(a_data, c.MasserDirection,  c.MasserDirection.w);
        a_data.derived.SecundaNDC = ProjectDirection(a_data, c.SecundaDirection, c.SecundaDirection.w);
    }
}

#undef ENTRY
