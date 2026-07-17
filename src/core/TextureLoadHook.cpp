//=============================================================================
//  TextureLoadHook.cpp — in-flight foreign-texture substitution
//
//  Strategy: transcode-to-cache + path redirect. The detours only ever act
//  after the original call already failed, and only re-invoke the original
//  implementation on a different (cache) path. All four resolution surfaces
//  are covered: sync stream, async stream, and both info queries.
//=============================================================================

#include "TextureLoadHook.h"
#include "TextureCodec.h"
#include "SBConfig.h"

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>

#include <atomic>
#include <cctype>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

namespace SB
{
    namespace
    {
        using ErrorCode = RE::BSResource::ErrorCode;

        constexpr const char* kCacheRel = "SKSE\\Plugins\\SkyrimBridge\\texcache";

        std::mutex                                    s_mx;
        std::unordered_map<std::string, std::string>  s_redirect;   // lower rel .dds -> cache rel ("" = negative)
        int                                           s_format = 2; // 0=RGBA8 1=BC1 2=BC3
        bool                                          s_mips = true;
        std::atomic<std::uint32_t>                    s_served{ 0 };

        std::string Lower(const char* p)
        {
            std::string s(p);
            for (auto& c : s) {
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                if (c == '/') c = '\\';
            }
            return s;
        }

        bool InScope(const std::string& lower)
        {
            // textures\*.dds only; skip our own cache paths outright.
            if (lower.size() < 14) return false;                  // "textures\a.dds"
            std::size_t start = (lower[0] == '\\') ? 1 : 0;
            if (lower.compare(start, 9, "textures\\") != 0) return false;
            if (lower.compare(lower.size() - 4, 4, ".dds") != 0) return false;
            return lower.find("skyrimbridge\\texcache") == std::string::npos;
        }

        std::uint64_t Fnv1a(const std::string& s)
        {
            std::uint64_t h = 0xCBF29CE484222325ull;
            for (unsigned char c : s) { h ^= c; h *= 0x100000001B3ull; }
            return h;
        }

        // Missing .dds -> cache-relative redirect path, or "" when no foreign
        // sibling exists. Transcode happens at most once per source state.
        // Both the probe and the cache root use the location instance's own
        // prefix, so a non-Data-rooted instance stays consistent (or inert).
        std::string ResolveForeign(const char* a_prefix, const std::string& lowerRel)
        {
            const std::string prefix = (a_prefix && *a_prefix) ? a_prefix : "Data";
            const std::string key = prefix + "|" + lowerRel;

            std::lock_guard lock(s_mx);
            if (auto it = s_redirect.find(key); it != s_redirect.end())
                return it->second;

            std::string result;
            try {
                const std::filesystem::path dataRel =
                    std::filesystem::path(prefix) / lowerRel;
                static const char* kExts[] = { ".png", ".tga", ".bmp" };
                for (auto* ext : kExts) {
                    auto src = dataRel;
                    src.replace_extension(ext);
                    std::error_code ec;
                    if (!std::filesystem::exists(src, ec)) continue;

                    char name[40];
                    std::snprintf(name, sizeof name, "%016llX.dds",
                                  static_cast<unsigned long long>(Fnv1a(key)));
                    auto cacheAbs = std::filesystem::path(prefix) / kCacheRel / name;

                    bool fresh = std::filesystem::exists(cacheAbs, ec) &&
                                 std::filesystem::last_write_time(cacheAbs, ec) >=
                                 std::filesystem::last_write_time(src, ec);
                    if (!fresh) {
                        std::filesystem::create_directories(cacheAbs.parent_path(), ec);
                        const auto fmt = s_format == 0 ? TexCodec::DDSFormat::RGBA8
                                       : s_format == 1 ? TexCodec::DDSFormat::BC1
                                                       : TexCodec::DDSFormat::BC3;
                        if (!TexCodec::Convert(src, cacheAbs, fmt, s_mips)) {
                            SKSE::log::warn("TextureLoadHook: transcode failed for {}", src.string());
                            continue;
                        }
                        SKSE::log::info("TextureLoadHook: {} -> texcache/{}", src.string(), name);
                    }
                    result = std::string(kCacheRel) + "\\" + name;
                    break;
                }
            } catch (const std::exception& e) {
                SKSE::log::error("TextureLoadHook: exception resolving {}: {}", lowerRel, e.what());
                result.clear();
            }
            s_redirect.emplace(key, result);
            return result;
        }

        // Shared detour skeleton: original first; on miss, redirect the same
        // original function to the cache path; on any failure return the
        // original error untouched.
        template <class Hook, class... Args>
        ErrorCode Detour(RE::BSResource::LooseFileLocation* self, const char* path, Args&&... args)
        {
            ErrorCode ec = Hook::func(self, path, std::forward<Args>(args)...);
            if (ec == ErrorCode::kNone || !path || !*path) return ec;
            try {
                auto lower = Lower(path);
                if (!InScope(lower)) return ec;
                auto redirect = ResolveForeign(self->prefix.c_str(), lower);
                if (redirect.empty()) return ec;
                ErrorCode ec2 = Hook::func(self, redirect.c_str(), std::forward<Args>(args)...);
                if (ec2 == ErrorCode::kNone) {
                    ++s_served;
                    return ec2;
                }
            } catch (...) {
                SKSE::log::error("TextureLoadHook: detour exception on '{}'", path);
            }
            return ec;
        }

        struct CreateStreamHook   // vfunc 03
        {
            static ErrorCode thunk(RE::BSResource::LooseFileLocation* a_self, const char* a_path,
                                   RE::BSTSmartPointer<RE::BSResource::Stream>& a_stream,
                                   RE::BSResource::Location*& a_loc, bool a_readOnly)
            {
                return Detour<CreateStreamHook>(a_self, a_path, a_stream, a_loc, a_readOnly);
            }
            static inline REL::Relocation<decltype(thunk)> func;
        };

        struct CreateAsyncStreamHook   // vfunc 04
        {
            static ErrorCode thunk(RE::BSResource::LooseFileLocation* a_self, const char* a_path,
                                   RE::BSTSmartPointer<RE::BSResource::AsyncStream>& a_stream,
                                   RE::BSResource::Location*& a_loc, bool a_readOnly)
            {
                return Detour<CreateAsyncStreamHook>(a_self, a_path, a_stream, a_loc, a_readOnly);
            }
            static inline REL::Relocation<decltype(thunk)> func;
        };

        struct GetInfo1Hook   // vfunc 06
        {
            static ErrorCode thunk(RE::BSResource::LooseFileLocation* a_self, const char* a_path,
                                   RE::BSResource::Info& a_info, RE::BSResource::Location*& a_loc)
            {
                return Detour<GetInfo1Hook>(a_self, a_path, a_info, a_loc);
            }
            static inline REL::Relocation<decltype(thunk)> func;
        };

        struct GetInfo2Hook   // vfunc 07
        {
            static ErrorCode thunk(RE::BSResource::LooseFileLocation* a_self, const char* a_path,
                                   RE::BSResource::Info& a_info, RE::BSResource::LocationTraverser* a_trav)
            {
                return Detour<GetInfo2Hook>(a_self, a_path, a_info, a_trav);
            }
            static inline REL::Relocation<decltype(thunk)> func;
        };
    }

    TextureLoadHook& TextureLoadHook::Get()
    {
        static TextureLoadHook instance;
        return instance;
    }

    void TextureLoadHook::Install(const std::filesystem::path& configDir)
    {
        if (m_installed) return;

        bool found = false;
        auto doc = Cfg::ParseFile(configDir / "SkyrimBridge.ini", &found);
        if (found) {
            if (auto* s = doc.Find("TextureConvert")) {
                auto fmt = s->Get("Format", "BC3");
                if (fmt == "RGBA8")    s_format = 0;
                else if (fmt == "BC1") s_format = 1;
                else                   s_format = 2;
                s_mips = s->Bool("Mipmaps", s_mips);
            }
        }

        REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE_BSResource__LooseFileLocation[0] };
        CreateStreamHook::func      = vtbl.write_vfunc(0x3, CreateStreamHook::thunk);
        CreateAsyncStreamHook::func = vtbl.write_vfunc(0x4, CreateAsyncStreamHook::thunk);
        GetInfo1Hook::func          = vtbl.write_vfunc(0x6, GetInfo1Hook::thunk);
        GetInfo2Hook::func          = vtbl.write_vfunc(0x7, GetInfo2Hook::thunk);

        m_installed = true;
        SKSE::log::info("TextureLoadHook: LooseFileLocation detours installed "
                        "(stream/async/info x2), cache at Data/{}", kCacheRel);
    }
}
