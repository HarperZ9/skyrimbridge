#pragma once
//=============================================================================
//  CompatDetect.h — External plugin compatibility detection
//
//  Detects NativeEditorID Fix and po3_Tweaks at runtime. When an overlapping
//  feature is found, SkyrimBridge defers to the external plugin.
//
//  Author: Zain Dana Harper
//  License: MIT
//=============================================================================

#include <string>
#include <vector>

namespace SB
{

struct DetectedPlugin
{
    std::string name;        // Human-readable name
    std::string dllName;     // Module filename
    std::string sbFeature;   // Which SB feature it overlaps with
};

class CompatDetect
{
public:
    static CompatDetect& Get()
    {
        static CompatDetect inst;
        return inst;
    }

    // Run detection at kPostLoad — checks for loaded modules
    void Detect();

    // ── Legacy queries ──────────────────────────────────────────────────
    bool HasNativeEditorIDFix() const { return m_hasNativeEditorIDFix; }
    bool HasPo3EditorIDCache() const  { return m_hasPo3EditorIDCache; }
    bool HasENBParmLink() const       { return false; } // ENB support removed

    bool HasExternalEditorIDProvider() const
    {
        return m_hasNativeEditorIDFix || m_hasPo3EditorIDCache;
    }

    using GetFormEditorIDFn = const char*(*)(std::uint32_t);
    GetFormEditorIDFn GetExternalEditorIDFunc() const { return m_externalGetEditorID; }

    // ── SB Proxy detection ──────────────────────────────────────────────
    bool HasSBProxy() const { return m_hasSBProxy; }

    // All detected plugins that overlap with SB features
    const std::vector<DetectedPlugin>& GetDetected() const { return m_detected; }

    // Build a notification string for in-game display
    std::string GetNotificationText() const;

private:
    CompatDetect() = default;

    void DetectEditorIDPlugins();
    void DetectSBProxy();

    // Legacy flags
    bool m_hasNativeEditorIDFix = false;
    bool m_hasPo3EditorIDCache  = false;

    // Proxy detection
    bool m_hasSBProxy           = false;

    GetFormEditorIDFn m_externalGetEditorID = nullptr;
    std::vector<DetectedPlugin> m_detected;
};

}  // namespace SB
