//=============================================================================
//  WorldspaceWeatherlist.cpp
//=============================================================================

#include "WorldspaceWeatherlist.h"
#include "CompatDetect.h"
#include "ENBInterface.h"
#include "SBConfig.h"

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>

#include <fstream>
#include <sstream>

namespace SB
{
    // ── Our [Routes] grammar (the hot path) ──────────────────────────────
    // Each entry:  Label = <id>[, <id>...] : <fileName>
    std::vector<WorldspaceWeatherlist::Rule>
    WorldspaceWeatherlist::ParseRouting(const std::string& text, std::string* defaultFile)
    {
        std::vector<Rule> rules;
        auto doc = Cfg::Parse(text);

        if (defaultFile) {
            if (auto* opt = doc.Find("Options"))
                *defaultFile = opt->Get("Default");
        }

        if (auto* routes = doc.Find("Routes")) {
            for (auto& [label, value] : routes->entries) {
                auto colon = value.find(':');
                if (colon == std::string::npos) continue;   // needs "ids : file"
                Rule r;
                r.label = label;
                r.worldspaceIDs = Cfg::AsIDList(value.substr(0, colon));
                r.fileName = Cfg::Trim(value.substr(colon + 1));
                if (!r.fileName.empty() && !r.worldspaceIDs.empty())
                    rules.push_back(std::move(r));
            }
        }
        return rules;
    }

    // ── Legacy third-party grammar (import path only) ────────────────────
    std::vector<WorldspaceWeatherlist::Rule>
    WorldspaceWeatherlist::ParseLegacy(const std::string& text)
    {
        std::vector<Rule> rules;
        auto doc = Cfg::Parse(text);
        for (auto& sec : doc.sections) {
            // Legacy sections are WEATHERLIST01..99 with FileName + WorldspaceIDs.
            if (sec.Base().rfind("WEATHERLIST", 0) != 0) continue;
            Rule r;
            r.label = sec.name;
            r.fileName = sec.Get("FileName");
            if (auto* ids = sec.Find("WorldspaceIDs"))
                r.worldspaceIDs = Cfg::AsIDList(*ids);
            if (!r.fileName.empty() && !r.worldspaceIDs.empty())
                rules.push_back(std::move(r));
        }
        return rules;
    }

    std::string WorldspaceWeatherlist::EmitRouting(const std::vector<Rule>& rules,
                                                   const std::string& defaultFile)
    {
        std::ostringstream out;
        out << "; SkyrimBridge - Worldspace -> ENB weatherlist routing\n";
        out << "; Imported from a legacy _worldspaceweatherlist.ini. Edit freely.\n";
        out << "; Label = <FormID>[, <FormID>...] : <weatherlist file in enbseries>\n\n";
        out << "[Routes]\n";
        for (const auto& r : rules) {
            out << r.label << " = ";
            for (std::size_t i = 0; i < r.worldspaceIDs.size(); ++i) {
                if (i) out << ", ";
                char buf[16];
                std::snprintf(buf, sizeof(buf), "0x%06X", r.worldspaceIDs[i]);
                out << buf;
            }
            out << " : " << r.fileName << "\n";
        }
        out << "\n[Options]\n";
        out << "Default = " << defaultFile << "\n";
        return out.str();
    }

    WorldspaceWeatherlist& WorldspaceWeatherlist::Get()
    {
        static WorldspaceWeatherlist instance;
        return instance;
    }

    void WorldspaceWeatherlist::Initialize(const std::filesystem::path& enbseriesDir)
    {
        // ENB Worldspace Weatherlists owns this routing when installed. Stand
        // down rather than fight it over `_weatherlist.ini`. See CREDITS.md.
        if (CompatDetect::Get().HasWorldspaceWeatherlists()) {
            SKSE::log::info("WorldspaceWeatherlist: ENB Worldspace Weatherlists "
                "present — deferring weatherlist routing to it, inactive");
            return;
        }

        m_enbseriesDir = enbseriesDir;

        std::error_code ec;
        auto routingPath = m_enbseriesDir / "WeatherRouting.ini";

        bool found = false;
        auto text = ([&] {
            std::ifstream in(routingPath);
            if (!in) return std::string{};
            std::stringstream b; b << in.rdbuf(); found = true; return b.str();
        })();

        if (found) {
            m_rules = ParseRouting(text, &m_defaultFile);
        } else {
            // Import a legacy file once, then write our format alongside it.
            auto legacyPath = m_enbseriesDir / "_worldspaceweatherlist.ini";
            std::ifstream legacy(legacyPath);
            if (legacy) {
                std::stringstream b; b << legacy.rdbuf();
                m_rules = ParseLegacy(b.str());
                if (!m_rules.empty()) {
                    std::ofstream out(routingPath, std::ios::trunc);
                    if (out) out << EmitRouting(m_rules, m_defaultFile);
                    SKSE::log::info("WorldspaceWeatherlist: imported {} legacy rules -> WeatherRouting.ini",
                        m_rules.size());
                }
            }
        }

        if (m_rules.empty()) {
            SKSE::log::info("WorldspaceWeatherlist: no routing config, inactive");
            return;
        }

        // Preserve ENB's shipped weatherlist so an empty route restores it.
        std::ifstream def(m_enbseriesDir / "_weatherlist.ini", std::ios::binary);
        if (def) {
            std::stringstream d; d << def.rdbuf();
            m_defaultBackup = d.str();
            m_haveDefault = true;
        }

        m_active = true;
        SKSE::log::info("WorldspaceWeatherlist: {} routes active", m_rules.size());
    }

    bool WorldspaceWeatherlist::ApplyWeatherlist(const std::string& fileName)
    {
        auto target = m_enbseriesDir / "_weatherlist.ini";

        if (fileName.empty()) {
            if (!m_haveDefault) return false;
            std::ofstream out(target, std::ios::binary | std::ios::trunc);
            if (!out) return false;
            out << m_defaultBackup;
        } else {
            std::ifstream in(m_enbseriesDir / fileName, std::ios::binary);
            if (!in) {
                SKSE::log::warn("WorldspaceWeatherlist: weatherlist file missing: {}", fileName);
                return false;
            }
            std::stringstream content; content << in.rdbuf();
            std::ofstream out(target, std::ios::binary | std::ios::trunc);
            if (!out) return false;
            out << content.str();
        }

        m_activeFile = fileName;
        bool reloaded = ENBInterface::ReloadConfig();
        SKSE::log::info("WorldspaceWeatherlist: swapped to '{}'{}",
            fileName.empty() ? "(default)" : fileName,
            reloaded ? ", ENB reloaded" : " (reload unavailable)");
        return true;
    }

    void WorldspaceWeatherlist::Update()
    {
        if (!m_active) return;

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return;
        auto* ws = player->GetWorldspace();
        std::uint32_t wsID = ws ? ws->GetFormID() : 0;

        if (wsID == m_lastWorldspaceID) return;   // no change
        m_lastWorldspaceID = wsID;

        const Rule* match = nullptr;
        for (const auto& rule : m_rules) {
            for (auto id : rule.worldspaceIDs)
                if (id == wsID) { match = &rule; break; }
            if (match) break;
        }

        std::string wanted = match ? match->fileName : m_defaultFile;
        if (wanted == m_activeFile) return;   // already applied
        ApplyWeatherlist(wanted);             // "" restores backup
    }
}
