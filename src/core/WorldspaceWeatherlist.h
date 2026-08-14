#pragma once
//=============================================================================
//  WorldspaceWeatherlist.h — per-worldspace ENB weatherlist switching
//
//  Native replacement for ENBWorldspaceWeatherlists and its KiLoader framework
//  by Kitsuune (LonelyKitsuune). Derived by reverse-engineering Kitsuune's
//  compiled binaries, NOT clean-room; see CREDITS.md, permission-gated and not
//  for public release.
//
//  SkyrimBridge reads its OWN routing config,
//  detects the player's worldspace natively (CommonLibSSE-NG), and when it
//  changes to a routed worldspace overwrites ENB's `enbseries\_weatherlist.ini`
//  with the mapped file and asks ENB to reload. No external loader involved.
//
//  Our config: `enbseries\WeatherRouting.ini` (SkyrimBridge flat-INI):
//
//    [Routes]                       ; Label = <FormID list> : <weatherlist file>
//    Tamriel   = 0x00003C           : Skyrim.ini
//    Solstheim = 0x0300302A         : Solstheim.ini
//    SoulCairn = 0x0002F6C, 0x2F6D  : SoulCairn.ini
//    [Options]
//    Default   =                    ; empty = restore ENB's shipped list
//
//  A legacy `_worldspaceweatherlist.ini` (the third-party format) is imported
//  once into WeatherRouting.ini when our file is absent, so existing setups
//  keep working without the runtime ever parsing that format again.
//=============================================================================

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace SB
{
    class WorldspaceWeatherlist
    {
    public:
        static WorldspaceWeatherlist& Get();

        // enbseries directory (holds _weatherlist.ini, the weatherlist files,
        // and our WeatherRouting.ini). Loads routing config here.
        void Initialize(const std::filesystem::path& enbseriesDir);

        bool IsActive() const { return m_active; }
        std::size_t RuleCount() const { return m_rules.size(); }

        // Poll the player's worldspace; on a change to a routed worldspace,
        // swap the ENB weatherlist and reload. Cheap when unchanged.
        void Update();

        struct Rule
        {
            std::string label;                          // human label (for logs)
            std::string fileName;                       // weatherlist file, relative to enbseries
            std::vector<std::uint32_t> worldspaceIDs;   // matching worldspace form IDs
        };

        // Parse our [Routes] grammar. Pure and unit-testable.
        static std::vector<Rule> ParseRouting(const std::string& text, std::string* defaultFile = nullptr);

        // Parse the legacy third-party grammar (WEATHERLIST01..99 sections).
        // Retained only for the one-time import path, never the hot path.
        static std::vector<Rule> ParseLegacy(const std::string& text);

        // Render a rule set back out as WeatherRouting.ini text (for migration).
        static std::string EmitRouting(const std::vector<Rule>& rules, const std::string& defaultFile);

    private:
        WorldspaceWeatherlist() = default;

        bool ApplyWeatherlist(const std::string& fileName);

        std::filesystem::path m_enbseriesDir;
        std::vector<Rule> m_rules;
        std::string       m_defaultFile;   // "" = restore backup
        bool m_active = false;

        std::uint32_t m_lastWorldspaceID = 0;
        std::string   m_activeFile;
        std::string   m_defaultBackup;
        bool          m_haveDefault = false;
    };
}
