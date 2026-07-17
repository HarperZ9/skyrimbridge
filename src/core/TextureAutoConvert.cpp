//=============================================================================
//  TextureAutoConvert.cpp — startup transcode of foreign-format textures
//=============================================================================

#include "TextureAutoConvert.h"
#include "TextureCodec.h"
#include "SBConfig.h"

#include <SKSE/SKSE.h>

#include <string>
#include <thread>

namespace SB
{
    TextureAutoConvert& TextureAutoConvert::Get()
    {
        static TextureAutoConvert instance;
        return instance;
    }

    static bool IsForeignTexture(const std::filesystem::path& p)
    {
        auto ext = p.extension().string();
        for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return ext == ".png" || ext == ".tga" || ext == ".bmp";
    }

    void TextureAutoConvert::Initialize(const std::filesystem::path& configDir)
    {
        bool found = false;
        auto doc = Cfg::ParseFile(configDir / "SkyrimBridge.ini", &found);
        if (found) {
            if (auto* s = doc.Find("TextureConvert")) {
                auto fmt = s->Get("Format", "BC3");
                if (fmt == "RGBA8")     m_format = 0;
                else if (fmt == "BC1")  m_format = 1;
                else if (fmt == "BC7")  m_format = 3;
                else                    m_format = 2;
                m_mipmaps = s->Bool("Mipmaps", m_mipmaps);
                m_refresh = s->Bool("Refresh", m_refresh);
                if (s->Bool("CoverageMips", false)) {
                    int t = Cfg::AsInt(s->Get("CoverageThreshold", "128"), 128);
                    m_coverage = t < 1 ? 1 : (t > 255 ? 255 : t);
                }
                auto root = s->Get("Root");
                if (!root.empty()) m_root = root;
            }
        }

        std::thread([this]() {
            auto r = RunScan(false);
            SKSE::log::info("TextureAutoConvert: scan done — {} foreign, {} converted, "
                            "{} already had .dds, {} failed",
                            r.candidates, r.converted, r.skipped, r.failed);
        }).detach();
    }

    TextureAutoConvert::ScanResult TextureAutoConvert::RunScan(bool dryRun)
    {
        ScanResult r;
        bool expected = false;
        if (!m_busy.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
            SKSE::log::warn("TextureAutoConvert: scan already running");
            return r;
        }

        std::error_code ec;
        if (!std::filesystem::exists(m_root, ec)) {
            SKSE::log::info("TextureAutoConvert: root '{}' not found, nothing to do", m_root.string());
            m_busy.store(false, std::memory_order_release);
            return r;
        }

        const auto fmt = m_format == 0 ? TexCodec::DDSFormat::RGBA8
                       : m_format == 1 ? TexCodec::DDSFormat::BC1
                       : m_format == 3 ? TexCodec::DDSFormat::BC7
                                       : TexCodec::DDSFormat::BC3;

        for (std::filesystem::recursive_directory_iterator
                 it(m_root, std::filesystem::directory_options::skip_permission_denied, ec), end;
             it != end; it.increment(ec)) {
            if (ec) { ec.clear(); continue; }
            if (!it->is_regular_file(ec) || !IsForeignTexture(it->path())) continue;
            ++r.candidates;

            auto target = it->path();
            target.replace_extension(".dds");

            std::error_code tec;
            if (std::filesystem::exists(target, tec)) {
                bool stale = m_refresh &&
                    std::filesystem::last_write_time(it->path(), tec) >
                    std::filesystem::last_write_time(target, tec);
                if (!stale) { ++r.skipped; continue; }
            }

            if (dryRun) { ++r.converted; continue; }

            try {
                if (TexCodec::Convert(it->path(), target, fmt, m_mipmaps, m_coverage)) {
                    ++r.converted;
                    SKSE::log::info("TextureAutoConvert: {} -> {}",
                                    it->path().filename().string(), target.filename().string());
                } else {
                    ++r.failed;
                    SKSE::log::warn("TextureAutoConvert: decode/write failed for {}",
                                    it->path().string());
                }
            } catch (const std::exception& e) {
                ++r.failed;
                SKSE::log::error("TextureAutoConvert: exception on {}: {}",
                                 it->path().string(), e.what());
            }
        }

        m_busy.store(false, std::memory_order_release);
        return r;
    }
}
