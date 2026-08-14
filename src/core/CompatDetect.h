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

#include <cstddef>
#include <cstdint>
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

    // ── Kitsuune plugin suite ───────────────────────────────────────────
    //  SkyrimBridge's native replacements exist so a user does not NEED these
    //  plugins. They are not a reason to displace them. When the original is
    //  installed it is authoritative and our replacement stands down, so both
    //  can sit in one load order. See CREDITS.md.
    bool HasKreatE() const        { return m_hasKreatE; }
    bool HasELIF() const          { return m_hasELIF; }
    bool HasEVLaS() const         { return m_hasEVLaS; }
    bool HasAELAS() const         { return m_hasAELAS; }
    bool HasWorldspaceWeatherlists() const { return m_hasWorldspaceWeatherlists; }
    bool HasKiLoader() const      { return m_hasKiLoader; }

    // Celestial lighting is owned by either AELAS or its predecessor EVLaS.
    bool HasExternalCelestialProvider() const { return m_hasAELAS || m_hasEVLaS; }

    // All detected plugins that overlap with SB features
    const std::vector<DetectedPlugin>& GetDetected() const { return m_detected; }

    // Build a notification string for in-game display
    std::string GetNotificationText() const;

private:
    CompatDetect() = default;

    void DetectEditorIDPlugins();
    void DetectKitsuunePlugins();
    void DetectSBProxy();

    // Probe a module by any of several candidate names. Returns the first hit.
    static void* ProbeModule(const wchar_t* const* names, std::size_t count);

    // Legacy flags
    bool m_hasNativeEditorIDFix = false;
    bool m_hasPo3EditorIDCache  = false;

    // Kitsuune suite flags
    bool m_hasKreatE                 = false;
    bool m_hasELIF                   = false;
    bool m_hasEVLaS                  = false;
    bool m_hasAELAS                  = false;
    bool m_hasWorldspaceWeatherlists = false;
    bool m_hasKiLoader               = false;

    // Proxy detection
    bool m_hasSBProxy           = false;

    // Detect() may be called more than once; the summary logs once.
    bool m_summaryLogged        = false;

    GetFormEditorIDFn m_externalGetEditorID = nullptr;
    std::vector<DetectedPlugin> m_detected;
};

}  // namespace SB
