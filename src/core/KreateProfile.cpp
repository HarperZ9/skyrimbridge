//=============================================================================
//  KreateProfile.cpp — native KreatE profile loader
//=============================================================================

#include "KreateProfile.h"

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <fstream>
#include <sstream>

namespace SB
{
    // ── Parse helpers (pure) ─────────────────────────────────────────────

    static std::string TrimWS(const std::string& s)
    {
        auto a = s.find_first_not_of(" \t\r\n");
        if (a == std::string::npos) return {};
        auto b = s.find_last_not_of(" \t\r\n");
        return s.substr(a, b - a + 1);
    }

    static float ReadF(const std::string& v, float fallback)
    {
        try { return std::stof(v); } catch (...) { return fallback; }
    }

    static RE::FormID ReadFormID(const std::string& v)
    {
        std::string s = TrimWS(v);
        int base = 10;
        std::size_t off = 0;
        if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
            base = 16;
            off = 2;
        }
        RE::FormID id = 0;
        std::from_chars(s.data() + off, s.data() + s.size(), id, base);
        return id;
    }

    bool KreateProfile::ParseImageSpaceIni(const std::string& text,
                                           ImageSpaceSnapshot& out)
    {
        out = ImageSpaceSnapshot{};
        std::string section;   // "" = top-level HDR/cinematic block
        std::istringstream stream(text);
        std::string line;
        bool haveID = false;

        while (std::getline(stream, line)) {
            auto t = TrimWS(line);
            if (t.empty() || t[0] == ';' || (t.size() >= 2 && t[0] == '/' && t[1] == '/'))
                continue;
            if (t[0] == '[') {
                auto close = t.find(']');
                section = (close != std::string::npos) ? t.substr(1, close - 1) : t.substr(1);
                continue;
            }
            auto eq = t.find('=');
            if (eq == std::string::npos) continue;
            auto key = TrimWS(t.substr(0, eq));
            auto val = TrimWS(t.substr(eq + 1));

            // Some KreatE files glue the next section header onto the end of
            // a value line with no newline (e.g. "Tint = ..., 0[DepthOfField]").
            // Split it off so the following DoF keys still land in-section.
            std::string pendingSection;
            auto bracket = val.find('[');
            if (bracket != std::string::npos) {
                auto close = val.find(']', bracket);
                if (close != std::string::npos)
                    pendingSection = val.substr(bracket + 1, close - bracket - 1);
                val = TrimWS(val.substr(0, bracket));
            }

            if (section.empty()) {
                if (key == "ID") { out.formID = ReadFormID(val); haveID = true; }
                else if (key == "EyeAdaptSpeed")         out.eyeAdaptSpeed = ReadF(val, out.eyeAdaptSpeed);
                else if (key == "EyeAdaptStrength")      out.eyeAdaptStrength = ReadF(val, out.eyeAdaptStrength);
                // The KreatE files carry a spelling variant of the blur-radius key.
                else if (key == "BloomBlurRadius" || key == "BloomBlurReadius")
                    out.bloomBlurRadius = ReadF(val, out.bloomBlurRadius);
                else if (key == "BloomThreshold")        out.bloomThreshold = ReadF(val, out.bloomThreshold);
                else if (key == "BloomScale")            out.bloomScale = ReadF(val, out.bloomScale);
                else if (key == "ReceiveBloomThreshold") out.receiveBloomThreshold = ReadF(val, out.receiveBloomThreshold);
                else if (key == "White")                 out.white = ReadF(val, out.white);
                else if (key == "SunlightScale")         out.sunlightScale = ReadF(val, out.sunlightScale);
                else if (key == "SkyScale")              out.skyScale = ReadF(val, out.skyScale);
                else if (key == "Saturation")            out.saturation = ReadF(val, out.saturation);
                else if (key == "Brightness")            out.brightness = ReadF(val, out.brightness);
                else if (key == "Contrast")              out.contrast = ReadF(val, out.contrast);
                else if (key == "Tint") {
                    float c[4] = { out.tintR, out.tintG, out.tintB, out.tintAmount };
                    std::istringstream cs(val);
                    for (int i = 0; i < 4; ++i) {
                        std::string tok;
                        if (!std::getline(cs, tok, ',')) break;
                        c[i] = ReadF(TrimWS(tok), c[i]);
                    }
                    // KreatE writes "R, G, B, Amount".
                    out.tintR = c[0]; out.tintG = c[1]; out.tintB = c[2]; out.tintAmount = c[3];
                }
            }
            else if (section == "DepthOfField") {
                if (key == "Strength")      out.dofStrength = ReadF(val, out.dofStrength);
                else if (key == "Distance") out.dofDistance = ReadF(val, out.dofDistance);
                else if (key == "Range")    out.dofRange = ReadF(val, out.dofRange);
            }

            if (!pendingSection.empty())
                section = pendingSection;
        }

        out.valid = haveID;
        return haveID;
    }

    // ── Singleton ────────────────────────────────────────────────────────

    KreateProfile& KreateProfile::Get()
    {
        static KreateProfile instance;
        return instance;
    }

    std::vector<std::string> KreateProfile::ListProfiles() const
    {
        std::vector<std::string> names;
        std::error_code ec;
        if (m_root.empty() || !std::filesystem::is_directory(m_root, ec))
            return names;
        for (auto& entry : std::filesystem::directory_iterator(m_root, ec)) {
            if (entry.is_directory(ec))
                names.push_back(entry.path().filename().string());
        }
        std::sort(names.begin(), names.end());
        return names;
    }

    int KreateProfile::LoadProfile(const std::string& name)
    {
        m_overrides.clear();
        m_records.Clear();
        m_loadedName.clear();

        std::error_code ec;
        auto profileDir = m_root / name;

        // ImageSpaces (tolerant parser for the KreatE spelling/section quirk).
        auto imgDir = profileDir / "ImageSpaces";
        if (std::filesystem::is_directory(imgDir, ec)) {
            for (auto& entry : std::filesystem::directory_iterator(imgDir, ec)) {
                if (!entry.is_regular_file(ec)) continue;
                if (entry.path().extension() != ".ini") continue;
                std::ifstream in(entry.path());
                if (!in) continue;
                std::stringstream buf;
                buf << in.rdbuf();
                ImageSpaceSnapshot snap;
                if (ParseImageSpaceIni(buf.str(), snap))
                    m_overrides.push_back(snap);
            }
        }

        // The other four record types.
        m_records.LoadFrom(profileDir);

        m_loadedName = name;
        SKSE::log::info("KreateProfile: loaded '{}' — {} imagespaces, {} weathers, "
            "{} lighting templates, {} volumetrics, {} particle geometries",
            name, m_overrides.size(), m_records.weathers.size(),
            m_records.lightingTemplates.size(), m_records.volumetrics.size(),
            m_records.shaderParticles.size());
        return LoadedCount();
    }

    int KreateProfile::ApplyLoaded()
    {
        int applied = 0;
        for (const auto& snap : m_overrides) {
            auto* is = RE::TESForm::LookupByID<RE::TESImageSpace>(snap.formID);
            if (!is) continue;
            snap.WriteTo(is);
            ++applied;
        }
        applied += m_records.ApplyAll();
        SKSE::log::info("KreateProfile: applied {} overrides from '{}'",
            applied, m_loadedName);
        return applied;
    }
}
