#pragma once
//=============================================================================
//  ParmLinkCompat.h — Drop-in replacement for enbParmLink.dll
//
//  Phase 4: Parses enbParmLink.cfg-compatible expression files and evaluates
//  them using SkyrimBridge data instead of raw process memory reads.
//
//  Supported enbParmLink API:
//    enb.fNightDayFactor          → SkyrimBridge time/weather data
//    enb.fCurrentLocationIndicator → SB_Interior_Flags.x
//    enb.fTimeOfDay               → SB_Time.x (game hour)
//    enb.getFloat(shader, group, name) → ENB parameter read
//    enb.setFloat(shader, group, name, value) → ENB parameter write
//    addr.getAbsFloat(address)    → REDIRECTED to typed SB data
//    addr.getAbsInt(address)      → REDIRECTED to typed SB data
//
//  Key improvement over ParmLink:
//    - No raw memory addresses (no ASLR issues, no version-specific offsets)
//    - All game data comes from SkyrimBridge typed trackers
//    - Expressions are compiled to C++ lambdas on load (not re-parsed per frame)
//    - Hot-reloadable config files
//    - Full backward compatibility with existing .cfg files
//
//  Author: Zain Dana Harper
//  License: MIT
//=============================================================================

#include "BridgeData.h"

#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <filesystem>
#include <mutex>

namespace SB
{
    //=========================================================================
    //  Expression variable binding — maps ParmLink variable names to SB data
    //=========================================================================

    struct ParmLinkVariable
    {
        std::string name;
        std::function<float()> getter;   // Returns current value
        std::string description;         // For debug/ImGui
    };


    //=========================================================================
    //  Compiled expression — a ParmLink assignment compiled to native code
    //=========================================================================

    struct CompiledExpression
    {
        std::string name;        // Left-hand side variable name
        std::string source;      // Original expression text (for debug)

        // The compiled evaluator — returns the computed value
        std::function<float()> evaluate;

        // If this is a setFloat call, these identify the ENB target
        bool isENBPush = false;
        std::string pushShader;
        std::string pushGroup;
        std::string pushParam;

        // Current value (updated per frame)
        float currentValue = 0.0f;
    };


    //=========================================================================
    //  ParmLinkCompat — the compatibility layer
    //=========================================================================

    class ParmLinkCompat
    {
    public:
        static ParmLinkCompat& Get()
        {
            static ParmLinkCompat inst;
            return inst;
        }

        // ─── Lifecycle ──────────────────────────────────────────────────

        // Initialize: register SB variable bindings, load .cfg files
        void Initialize(const std::filesystem::path& gameDir);

        // Per-frame update: evaluate all expressions, push results to ENB
        void Update(float deltaTime);

        // Shutdown
        void Shutdown();

        // ─── Config Management ──────────────────────────────────────────

        // Load a ParmLink-compatible .cfg file
        bool LoadCFG(const std::filesystem::path& cfgPath);

        // Hot reload: check if cfg files changed
        void CheckHotReload();

        // ─── Variable Registry ──────────────────────────────────────────

        // Register a custom variable binding (for extensions beyond ParmLink)
        void RegisterVariable(const std::string& name,
                             std::function<float()> getter,
                             const std::string& description = "");

        // Get current value of a user-defined variable
        float GetVariable(const std::string& name) const;

        // ─── Debug ──────────────────────────────────────────────────────

        const std::vector<CompiledExpression>& GetExpressions() const
        { return m_expressions; }

        const std::vector<ParmLinkVariable>& GetVariables() const
        { return m_variables; }

        size_t GetExpressionCount() const { return m_expressions.size(); }
        size_t GetVariableCount()   const { return m_variables.size(); }

        // Log file output (mirrors enbParmLink.log)
        void SetLogEnabled(bool enabled) { m_logEnabled = enabled; }

    private:
        ParmLinkCompat() = default;

        // ─── Variable Registration ──────────────────────────────────────

        // Register all SkyrimBridge data as ParmLink-compatible variables
        void RegisterSkyrimBridgeVariables();

        // Register ENB built-in state variables (fNightDayFactor, etc.)
        void RegisterENBVariables();

        // Register ParmLink addr.* redirections to SB typed data
        void RegisterAddressRedirections();

        // ─── Expression Compilation ─────────────────────────────────────

        // Parse a single ParmLink expression line and compile it
        CompiledExpression CompileExpression(const std::string& line);

        // Compile a math expression string into a callable lambda
        std::function<float()> CompileMathExpr(const std::string& expr);

        // ─── ENB Interface ──────────────────────────────────────────────

        // Read an ENB shader parameter
        static float ENBGetFloat(const char* shader, const char* group, const char* name);

        // Write an ENB shader parameter
        static void ENBSetFloat(const char* shader, const char* group,
                               const char* name, float value);

        // ─── State ──────────────────────────────────────────────────────

        std::vector<ParmLinkVariable>    m_variables;
        std::unordered_map<std::string, size_t> m_varIndex;  // name → index

        std::vector<CompiledExpression>  m_expressions;
        std::unordered_map<std::string, float> m_userVars;  // User-defined vars

        std::filesystem::path            m_cfgPath;
        std::filesystem::file_time_type  m_cfgLastMod;
        bool                             m_logEnabled = true;

        mutable std::mutex m_mutex;
    };


    //=========================================================================
    //  Address Redirect Table — maps known ParmLink memory addresses to SB data
    //
    //  ParmLink users use addr.getAbsFloat(ADDR) to read game memory.
    //  We intercept known addresses and return SkyrimBridge data instead.
    //  This eliminates ASLR issues and version-specific offsets entirely.
    //=========================================================================

    class AddressRedirectTable
    {
    public:
        static AddressRedirectTable& Get()
        {
            static AddressRedirectTable inst;
            return inst;
        }

        // Register: if someone reads this address chain, return this SB value
        void Register(uint64_t baseAddr, uint32_t offset,
                     std::function<float()> getter,
                     const std::string& description);

        // Look up: given a ParmLink addr.getAbsFloat call, try to find an SB redirect
        // Returns true if found, sets outValue
        bool TryRedirect(uint64_t address, float& outValue) const;

        // Register all known Skyrim memory offsets → SB mappings
        void RegisterDefaults();

    private:
        struct Redirect
        {
            uint64_t address;    // The address ParmLink would read
            uint32_t offset;     // Optional offset from base
            std::function<float()> getter;
            std::string description;
        };

        std::vector<Redirect> m_redirects;
    };

}  // namespace SB
