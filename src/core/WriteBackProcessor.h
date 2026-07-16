#pragma once
//=============================================================================
//  WriteBackProcessor.h — Domain A: Write game state via CommonLibSSE
//
//  INI-driven rule system that maps computed data (AllData fields, feedback
//  values, ENB readback slots, or fixed constants) to game engine targets
//  (FOV, fog, lights, actor values) using typed CommonLibSSE access.
//
//  No raw memory addresses — all targets are named game objects resolved
//  through RE:: singletons at runtime.
//
//  Author: Zain Dana Harper
//=============================================================================

#include "BridgeData.h"
#include <filesystem>
#include <vector>
#include <string>

namespace SB
{
    // ── Write-back targets: named game engine properties ─────────────────────
    enum class WriteBackTarget : uint8_t
    {
        CameraFOV,         // RE::PlayerCamera::worldFOV
        CameraFOV1st,      // RE::PlayerCamera::firstPersonFOV
        FogNearDist,       // RE::Sky::currentWeather fog near distance
        FogFarDist,        // RE::Sky::currentWeather fog far distance
        SunlightDiffuse_R, // RE::Sky sun NiDirectionalLight diffuse.red
        SunlightDiffuse_G,
        SunlightDiffuse_B,
        AmbientDiffuse_R,  // RE::Sky ambient light
        AmbientDiffuse_G,
        AmbientDiffuse_B,
        ActorValue,        // RE::PlayerCharacter ActorValue (needs avId)
        TimeScale,         // RE::Calendar::GetSingleton()->GetTimescale()
        GameHour,          // RE::Calendar::GetSingleton()->GetHour()
        Count
    };

    // ── Write-back sources: where the value comes from ──────────────────────
    enum class WriteBackSource : uint8_t
    {
        Fixed,          // Constant float from INI
        AllDataField,   // Named field in AllData (e.g. "SB_Computed_Luminance.x")
        ENBParameter,   // Live ENB shader parameter via the SDK
                        // (SourceShader=ENBEFFECT.FX, SourceField=<UIName>)
    };

    // ── Transform applied to source value before writing to target ──────────
    struct WriteBackTransform
    {
        float scale    = 1.0f;
        float offset   = 0.0f;
        float clampMin = -1e30f;
        float clampMax =  1e30f;
        float lerpAlpha = 1.0f;   // temporal smoothing (1.0 = instant write)
    };

    // ── One write-back rule ─────────────────────────────────────────────────
    struct WriteBackRule
    {
        bool              enabled = false;
        std::string       name;           // human-readable label
        WriteBackTarget   target = WriteBackTarget::CameraFOV;
        WriteBackSource   source = WriteBackSource::Fixed;
        std::string       sourceField;    // AllDataField name, or ENB parameter UIName
        std::string       sourceShader;   // ENBParameter: shader file (ENBEFFECT.FX)
        int               sourceIndex = 0;// reserved
        float             fixedValue = 0.0f;
        int               actorValueId = 0; // for ActorValue target
        WriteBackTransform transform;

        // Runtime state for temporal smoothing
        float             currentValue = 0.0f;
        bool              initialized = false;
    };

    // ── WriteBackProcessor singleton ────────────────────────────────────────
    class WriteBackProcessor
    {
    public:
        static WriteBackProcessor& Get();

        // Load rules from INI file. Call once at startup.
        void LoadConfig(const std::filesystem::path& configDir);

        // Execute all enabled rules. Call per-frame after FeedbackProcessor.
        void Execute(const AllData& data);

        // Status
        int GetRuleCount() const { return static_cast<int>(m_rules.size()); }
        int GetEnabledRuleCount() const;
        const WriteBackRule& GetRule(int index) const { return m_rules[index]; }

    private:
        WriteBackProcessor() = default;

        float ResolveSource(const WriteBackRule& rule, const AllData& data);
        void ApplyTarget(WriteBackTarget target, float value, int avId);

        // Parse "SB_Computed_Luminance.x" → byte offset + component index
        // Returns {offset, component} or {SIZE_MAX, 0} on failure
        std::pair<std::size_t, int> ParseFieldName(const std::string& fieldName);

        // Parse target/source enum names from INI strings
        static WriteBackTarget ParseTarget(const std::string& s);
        static WriteBackSource ParseSource(const std::string& s);

        std::vector<WriteBackRule> m_rules;
    };
}
