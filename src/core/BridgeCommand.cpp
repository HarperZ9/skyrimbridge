//=============================================================================
//  BridgeCommand.cpp — external command channel dispatch
//=============================================================================

#include "BridgeCommand.h"
#include "SB_CommandLayout.h"
#include "SBConfig.h"
#include "EngineReflect.h"
#include "RegionWalker.h"
#include "TextureCodec.h"
#include "TextureAutoConvert.h"
#include "ModelCodec.h"

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace SB
{
    static const char* kDumpDir = "Data/SKSE/Plugins/SkyrimBridge/dumps";

    BridgeCommand& BridgeCommand::Get() { static BridgeCommand inst; return inst; }
    BridgeCommand::~BridgeCommand() { Shutdown(); }
    bool BridgeCommand::IsActive() const { return m_block != nullptr; }

    bool BridgeCommand::Initialize()
    {
        if (m_block) return true;
        m_hMap = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                    static_cast<DWORD>(sizeof(SB_CommandBlock)), kCommandMemName);
        if (!m_hMap) {
            SKSE::log::error("BridgeCommand: CreateFileMapping failed (err={})", GetLastError());
            return false;
        }
        m_block = MapViewOfFile(m_hMap, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SB_CommandBlock));
        if (!m_block) {
            SKSE::log::error("BridgeCommand: MapViewOfFile failed (err={})", GetLastError());
            CloseHandle(static_cast<HANDLE>(m_hMap)); m_hMap = nullptr;
            return false;
        }
        auto* b = static_cast<SB_CommandBlock*>(m_block);
        std::memset(b, 0, sizeof(*b));
        b->magic = kCommandMagic;
        b->version = kCommandVersion;
        m_hEvent = CreateEventW(nullptr, FALSE, FALSE, kCommandEventName);
        SKSE::log::info("BridgeCommand: command channel active ('{}', {} bytes)",
                        "SkyrimBridge_Command", static_cast<int>(sizeof(SB_CommandBlock)));
        return true;
    }

    void BridgeCommand::Shutdown()
    {
        if (m_block) { UnmapViewOfFile(m_block); m_block = nullptr; }
        if (m_hMap) { CloseHandle(static_cast<HANDLE>(m_hMap)); m_hMap = nullptr; }
        if (m_hEvent) { CloseHandle(static_cast<HANDLE>(m_hEvent)); m_hEvent = nullptr; }
    }

    namespace
    {
        void SetText(SB_CommandBlock* b, const std::string& s)
        {
            std::size_t n = std::min(s.size(), sizeof(b->resultText) - 1);
            std::memcpy(b->resultText, s.data(), n);
            b->resultText[n] = '\0';
        }

        std::uint32_t ArgFormID(const char* s) { return Cfg::AsFormID(std::string(s)); }

        std::string ReadDumpFile(std::uint32_t id, const char* suffix)
        {
            char path[96];
            std::snprintf(path, sizeof path, "%s/%08X%s", kDumpDir, id, suffix);
            std::ifstream in(path);
            if (!in) return {};
            std::stringstream ss; ss << in.rdbuf(); return ss.str();
        }

        void WriteDumpFile(std::uint32_t id, const char* suffix, const std::string& text)
        {
            std::error_code ec; std::filesystem::create_directories(kDumpDir, ec);
            char path[96];
            std::snprintf(path, sizeof path, "%s/%08X%s", kDumpDir, id, suffix);
            std::ofstream out(path);
            if (out) out << text;
        }

        // Dispatch a single verb. Returns the status; fills resultInt/resultText.
        std::int32_t Dispatch(SB_CommandBlock* b)
        {
            std::string verb(b->verb, strnlen(b->verb, sizeof(b->verb)));
            b->resultInt = 0;
            b->resultText[0] = '\0';

            if (verb == "ping") { b->resultInt = 1; SetText(b, "pong"); return kCmdOK; }

            if (verb == "reflect.list") {
                std::uint32_t id = ArgFormID(b->arg0);
                if (id == 0) { SetText(b, Reflect::ListSchemas()); return kCmdOK; }
                auto* form = RE::TESForm::LookupByID(id);
                if (!form) return kCmdNotFound;
                auto* s = Reflect::SchemaFor(form->GetFormType());
                if (!s) return kCmdNotFound;
                SetText(b, Reflect::DescribeSchema(*s));
                return kCmdOK;
            }
            if (verb == "reflect.dump") {
                std::uint32_t id = ArgFormID(b->arg0);
                auto text = Reflect::Dump(id);
                if (text.empty()) return kCmdNotFound;
                WriteDumpFile(id, ".ini", text);
                SetText(b, text);
                b->resultInt = static_cast<std::int32_t>(std::count(text.begin(), text.end(), '\n'));
                return kCmdOK;
            }
            if (verb == "reflect.apply") {
                std::uint32_t id = ArgFormID(b->arg0);
                std::string ini = ReadDumpFile(id, ".ini");   // client edits the dump file
                if (ini.empty()) return kCmdNotFound;
                b->resultInt = Reflect::Apply(id, ini);
                return b->resultInt > 0 ? kCmdOK : kCmdFailed;
            }
            if (verb == "reflect.verify") {
                std::uint32_t id = ArgFormID(b->arg0);
                auto r = b->argInt ? Reflect::VerifyStrict(id) : Reflect::Verify(id);
                b->resultInt = r.ok ? r.fields : 0;
                SetText(b, r.ok ? "ok" : r.detail);
                return r.ok ? kCmdOK : kCmdFailed;
            }
            if (verb == "region.dump") {
                std::uint32_t id = ArgFormID(b->arg0);
                auto text = RegionWalker::Dump(id);
                if (text.empty()) return kCmdNotFound;
                WriteDumpFile(id, ".region.ini", text);
                SetText(b, text);
                return kCmdOK;
            }
            if (verb == "region.weather") {
                b->resultInt = RegionWalker::SetWeatherChance(ArgFormID(b->arg0), ArgFormID(b->arg1),
                                                             static_cast<std::uint32_t>(std::max(b->argInt, 0)));
                return kCmdOK;
            }
            if (verb == "texture.convert") {
                auto fmt = b->argInt == 1 ? TexCodec::DDSFormat::BC1
                         : b->argInt == 2 ? TexCodec::DDSFormat::BC3 : TexCodec::DDSFormat::RGBA8;
                b->resultInt = TexCodec::Convert(b->arg0, b->arg1, fmt) ? 1 : 0;
                return b->resultInt ? kCmdOK : kCmdFailed;
            }
            if (verb == "texture.scan") {
                auto r = TextureAutoConvert::Get().RunScan(b->argInt != 0);
                b->resultInt = static_cast<std::int32_t>(r.converted);
                char msg[128];
                std::snprintf(msg, sizeof msg, "candidates=%u converted=%u skipped=%u failed=%u",
                              r.candidates, r.converted, r.skipped, r.failed);
                SetText(b, msg);
                return kCmdOK;
            }
            if (verb == "model.convert") {
                b->resultInt = ModelCodec::ConvertToNIF(b->arg0, b->arg1) ? 1 : 0;
                return b->resultInt ? kCmdOK : kCmdFailed;
            }
            return kCmdUnknownVerb;
        }

        // SEH guard kept free of object unwinding (object-using body in its
        // own frame, same split as EnbLightInventoryFix). True = faulted.
        bool DispatchGuarded(SB_CommandBlock* b, std::int32_t& status)
        {
            __try {
                status = Dispatch(b);
                return false;
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                return true;
            }
        }
    }

    void BridgeCommand::Poll()
    {
        auto* b = static_cast<SB_CommandBlock*>(m_block);
        if (!b) return;
        std::uint32_t req = b->requestSeq;
        if (req == m_lastHandled) return;                    // nothing new

        std::int32_t status = kCmdFailed;
        if (DispatchGuarded(b, status)) {
            status = kCmdFailed;
            b->resultInt = 0;
            SetText(b, "dispatch raised an access violation");
            SKSE::log::error("BridgeCommand: dispatch AV on verb '{}'",
                             std::string(b->verb, strnlen(b->verb, sizeof(b->verb))));
        }
        b->status = status;
        m_lastHandled = req;
        ++m_dispatched;
        std::atomic_thread_fence(std::memory_order_release);
        b->responseSeq = req;                                // publish last
        if (m_hEvent) SetEvent(static_cast<HANDLE>(m_hEvent));
    }
}
