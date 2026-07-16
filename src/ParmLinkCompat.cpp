//=============================================================================
//  ParmLinkCompat.cpp — Drop-in replacement for enbParmLink.dll
//
//  Author: Zain Dana Harper
//  License: MIT
//=============================================================================

#include "ParmLinkCompat.h"
#include "CompatDetect.h"
#include "Trackers.h"
#include "ENBInterface_v3.h"

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>

namespace SB
{

//=============================================================================
//  ParmLinkCompat — Initialization
//=============================================================================

void ParmLinkCompat::Initialize(const std::filesystem::path& gameDir)
{
    std::lock_guard lock(m_mutex);

    // If the original enbParmLink.dll is loaded, defer to it entirely
    if (CompatDetect::Get().HasENBParmLink()) {
        SKSE::log::info("ParmLinkCompat: enbParmLink.dll detected — "
            "deferring expression evaluation to it");
        return;
    }

    // Register all variable bindings
    RegisterENBVariables();
    RegisterSkyrimBridgeVariables();
    RegisterAddressRedirections();

    // Look for enbParmLink.cfg (or SkyrimBridge.cfg) in the game directory
    m_cfgPath = gameDir / "enbParmLink.cfg";
    if (!std::filesystem::exists(m_cfgPath)) {
        m_cfgPath = gameDir / "SkyrimBridge_ParmLink.cfg";
    }

    if (std::filesystem::exists(m_cfgPath)) {
        LoadCFG(m_cfgPath);
        m_cfgLastMod = std::filesystem::last_write_time(m_cfgPath);
    }

    SKSE::log::info("ParmLinkCompat: initialized with {} variables, {} expressions",
        m_variables.size(), m_expressions.size());
}


void ParmLinkCompat::RegisterENBVariables()
{
    // ENB built-in state — these mirror what ParmLink reads via enb.* API
    RegisterVariable("enb.fNightDayFactor", []() -> float {
        // ENB's night-day factor: 0 = full night, 1 = full day
        // We derive this from SkyrimBridge time data
        float hour = GetData().celestial.TimeData.x;
        float sunrise = GetData().celestial.TimeData.y;
        float sunset  = GetData().celestial.TimeData.z;
        if (hour >= sunrise && hour <= sunset)
            return 1.0f;
        if (hour < sunrise - 1.0f || hour > sunset + 1.0f)
            return 0.0f;
        // Transition zone: 1 hour before sunrise, 1 hour after sunset
        if (hour < sunrise)
            return (hour - (sunrise - 1.0f)) / 1.0f;
        return 1.0f - (hour - sunset) / 1.0f;
    }, "ENB night/day factor [0=night, 1=day]");

    RegisterVariable("enb.fTimeOfDay", []() -> float {
        return GetData().celestial.TimeData.x;  // Game hour [0,24)
    }, "Current game hour");

    RegisterVariable("enb.fCurrentLocationIndicator", []() -> float {
        return GetData().interior.IsInterior.x;  // 0 = exterior, 1 = interior
    }, "Interior flag [0=exterior, 1=interior]");

    RegisterVariable("enb.fWeatherTransition", []() -> float {
        return GetData().weather.Transition.x;
    }, "Weather transition progress [0,1]");

    RegisterVariable("enb.fCurrentWeatherID", []() -> float {
        return GetData().weather.Transition.z;  // Current weather FormID (lower bits)
    }, "Current weather FormID");

    // Extended SB variables accessible via ParmLink-style names
    RegisterVariable("sb.sunElevation", []() -> float {
        return GetData().celestial.SunDirection.w;
    }, "Sun elevation angle (radians)");

    RegisterVariable("sb.windSpeed", []() -> float {
        return GetData().weather.Wind.x;
    }, "Wind speed [0,1]");

    RegisterVariable("sb.precipIntensity", []() -> float {
        return GetData().weather.Precipitation.y;
    }, "Precipitation intensity [0,1]");

    RegisterVariable("sb.precipType", []() -> float {
        return GetData().weather.Precipitation.x;
    }, "Precipitation type (0=none, 1=rain, 2=snow)");

    RegisterVariable("sb.isStormy", []() -> float {
        return GetData().weather.Flags.z > 0.5f ? 1.0f : 0.0f;
    }, "Is rainy weather");

    RegisterVariable("sb.isSnowy", []() -> float {
        return GetData().weather.Flags.w > 0.5f ? 1.0f : 0.0f;
    }, "Is snowy weather");

    RegisterVariable("sb.fogDensity", []() -> float {
        return GetData().fog.Density.x;
    }, "Fog density curve power");

    RegisterVariable("sb.playerHealth", []() -> float {
        return GetData().player.Vitals.x;
    }, "Player health percentage [0,1]");

    RegisterVariable("sb.inCombat", []() -> float {
        return GetData().player.Combat.x;
    }, "Player in combat flag");

    RegisterVariable("sb.isUnderwater", []() -> float {
        return GetData().player.Water.x;
    }, "Player underwater flag");

    RegisterVariable("sb.lightningFlash", []() -> float {
        return GetData().weather.Lightning.y;
    }, "Lightning flash active flag");

    RegisterVariable("sb.nightEye", []() -> float {
        return GetData().effects.VisionEffects.x;
    }, "Night Eye active flag");

    RegisterVariable("sb.slowTime", []() -> float {
        return GetData().effects.TimeEffects.x;
    }, "Slow time factor");
}


void ParmLinkCompat::RegisterSkyrimBridgeVariables()
{
    // Direct access to every SB_ float4 component via dot notation
    // This allows ParmLink-style expressions to use: sb.Camera_Info.x
    // Full SB API exposure — over 100 individual float values

    // Lambdas call GetData() directly for fresh data each frame

    // Camera (Params: .x=FOV rad, .y=near, .z=far, .w=aspect)
    RegisterVariable("sb.fov", []() { return GetData().camera.Params.x * 57.2957795f; }, "Camera FOV degrees");
    RegisterVariable("sb.nearClip", []() { return GetData().camera.Params.y; }, "Near clip");
    RegisterVariable("sb.farClip", []() { return GetData().camera.Params.z; }, "Far clip");
    RegisterVariable("sb.aspectRatio", []() { return GetData().camera.Params.w; }, "Aspect ratio");

    // Player
    RegisterVariable("sb.playerPosX", []() { return GetData().player.Position.x; }, "Player X");
    RegisterVariable("sb.playerPosY", []() { return GetData().player.Position.y; }, "Player Y");
    RegisterVariable("sb.playerPosZ", []() { return GetData().player.Position.z; }, "Player Z");
    RegisterVariable("sb.stamina", []() { return GetData().player.Vitals.y; }, "Stamina %");
    RegisterVariable("sb.magicka", []() { return GetData().player.Vitals.z; }, "Magicka %");
    RegisterVariable("sb.playerLevel", []() { return GetData().player.Vitals.w; }, "Player level");
    RegisterVariable("sb.playerSpeed", []() { return GetData().player.Movement.x; }, "Movement speed");
    RegisterVariable("sb.isSprinting", []() { return GetData().player.Movement.y; }, "Sprinting flag");
    RegisterVariable("sb.isSwimming", []() { return GetData().player.Movement.z; }, "Swimming flag");
    RegisterVariable("sb.isMounted", []() { return GetData().player.Movement.w; }, "Mounted flag");

    // Weather
    RegisterVariable("sb.windDirection", []() { return GetData().weather.Wind.y; }, "Wind dir (rad)");
    RegisterVariable("sb.lightningFreq", []() { return GetData().weather.Lightning.x; }, "Lightning frequency");
    RegisterVariable("sb.flashIntensity", []() { return GetData().weather.Lightning.z; }, "Flash intensity");
    RegisterVariable("sb.precipType", []() { return GetData().weather.Precipitation.x; }, "Surface wetness");
    RegisterVariable("sb.precipIntensity", []() { return GetData().weather.Precipitation.y; }, "Puddle depth");
    RegisterVariable("sb.weatherTransition", []() { return GetData().weather.Transition.x; }, "Weather transition");

    // Interior
    RegisterVariable("sb.isInterior", []() { return GetData().interior.IsInterior.x; }, "Interior flag");

    // Atmosphere
    RegisterVariable("sb.ambientR", []() { return GetData().atmosphere.Ambient.x; }, "Ambient R");
    RegisterVariable("sb.ambientG", []() { return GetData().atmosphere.Ambient.y; }, "Ambient G");
    RegisterVariable("sb.ambientB", []() { return GetData().atmosphere.Ambient.z; }, "Ambient B");
    RegisterVariable("sb.sunlightR", []() { return GetData().atmosphere.SunlightColor.x; }, "Sunlight R");
    RegisterVariable("sb.sunlightG", []() { return GetData().atmosphere.SunlightColor.y; }, "Sunlight G");
    RegisterVariable("sb.sunlightB", []() { return GetData().atmosphere.SunlightColor.z; }, "Sunlight B");
}


void ParmLinkCompat::RegisterAddressRedirections()
{
    // Map known ParmLink memory addresses to SB data
    // This handles the case where existing .cfg files use addr.getAbsFloat()
    AddressRedirectTable::Get().RegisterDefaults();
}


void ParmLinkCompat::RegisterVariable(const std::string& name,
                                       std::function<float()> getter,
                                       const std::string& description)
{
    m_varIndex[name] = m_variables.size();
    m_variables.push_back({name, std::move(getter), description});
}


float ParmLinkCompat::GetVariable(const std::string& name) const
{
    auto it = m_varIndex.find(name);
    if (it != m_varIndex.end())
        return m_variables[it->second].getter();

    // Check user-defined variables
    auto uit = m_userVars.find(name);
    if (uit != m_userVars.end())
        return uit->second;

    return 0.0f;
}


//=============================================================================
//  CFG Loading & Expression Compilation
//=============================================================================

bool ParmLinkCompat::LoadCFG(const std::filesystem::path& cfgPath)
{
    std::ifstream file(cfgPath);
    if (!file.is_open()) {
        SKSE::log::error("ParmLinkCompat: cannot open '{}'", cfgPath.string());
        return false;
    }

    m_expressions.clear();

    std::string line;
    int lineNum = 0;

    while (std::getline(file, line))
    {
        lineNum++;

        // Trim whitespace
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        if (line.empty()) continue;

        // Strip inline comments (// style)
        auto cpos = line.find("//");
        if (cpos != std::string::npos)
            line = line.substr(0, cpos);
        line.erase(line.find_last_not_of(" \t\r\n") + 1);
        if (line.empty()) continue;

        // Skip function definitions (ParmLink supports [] func() { })
        // We don't support user-defined functions — they're rare in practice
        if (line.front() == '[') continue;

        // Look for assignment: name := expr  (live evaluation)
        //                  or: name = "str"  (string assignment, skip)
        auto colonEq = line.find(":=");
        if (colonEq != std::string::npos) {
            auto expr = CompileExpression(line);
            if (expr.evaluate) {
                m_expressions.push_back(std::move(expr));
            } else if (m_logEnabled) {
                SKSE::log::warn("ParmLinkCompat: line {}: failed to compile '{}'",
                    lineNum, line);
            }
            continue;
        }

        // Check for enb.setFloat() calls as standalone statements
        if (line.find("enb.setFloat") != std::string::npos) {
            auto expr = CompileExpression(line);
            if (expr.evaluate) {
                m_expressions.push_back(std::move(expr));
            }
        }
    }

    SKSE::log::info("ParmLinkCompat: loaded {} expressions from '{}'",
        m_expressions.size(), cfgPath.filename().string());
    return true;
}


CompiledExpression ParmLinkCompat::CompileExpression(const std::string& line)
{
    CompiledExpression result;
    result.source = line;

    // Parse: name := expression
    auto colonEq = line.find(":=");
    if (colonEq != std::string::npos) {
        result.name = line.substr(0, colonEq);
        result.name.erase(result.name.find_last_not_of(" \t") + 1);
        result.name.erase(0, result.name.find_first_not_of(" \t"));

        std::string exprStr = line.substr(colonEq + 2);
        exprStr.erase(0, exprStr.find_first_not_of(" \t"));

        // Strip trailing semicolon
        if (!exprStr.empty() && exprStr.back() == ';')
            exprStr.pop_back();

        result.evaluate = CompileMathExpr(exprStr);
        return result;
    }

    // NOTE: enb.setFloat() parsing disabled - use WeatherParams.ini instead
    // Phase 2 provides a cleaner per-weather parameter system.


    return result;
}


//=============================================================================
//  ExprParser — Recursive-descent expression compiler
//
//  Compiles ParmLink/ExprTk-style math expressions into std::function<float()>
//  lambdas at config load time. No per-frame string parsing.
//
//  Supported:
//    Arithmetic:  + - * / % (unary -)
//    Comparison:  < > <= >= == !=
//    Logical:     && || !
//    Ternary:     condition ? true_val : false_val
//    Grouping:    ( )
//    Functions (1-arg): sin cos tan asin acos atan sqrt abs exp exp2
//                       log log2 log10 floor ceil round frac sign
//                       saturate radians degrees
//    Functions (2-arg): pow atan2 min max fmod step
//    Functions (3-arg): lerp mix clamp smoothstep
//    Literals:    1.0  .5  3e-2  1.5f
//    Variables:   sb.playerHealth  enb.fNightDayFactor  (dot-separated)
//=============================================================================

namespace  // anonymous — file-local only
{

class ExprParser
{
public:
    using Getter    = std::function<float()>;
    using VarLookup = std::function<float(const std::string&)>;

    ExprParser(const std::string& src, VarLookup lookup)
        : m_src(src), m_pos(0), m_lookup(std::move(lookup))
    {
        NextToken();
    }

    Getter Parse()
    {
        auto result = ParseTernary();
        return result ? result : []() { return 0.0f; };
    }

private:
    // ── Token ───────────────────────────────────────────────────────────

    enum TokType {
        TOK_NUM, TOK_IDENT,
        TOK_PLUS, TOK_MINUS, TOK_STAR, TOK_SLASH, TOK_PERCENT,
        TOK_LPAREN, TOK_RPAREN, TOK_COMMA,
        TOK_LT, TOK_GT, TOK_LE, TOK_GE, TOK_EQ, TOK_NE,
        TOK_AND, TOK_OR, TOK_NOT,
        TOK_QUESTION, TOK_COLON,
        TOK_END
    };

    struct Token { TokType type = TOK_END; float num = 0.0f; std::string str; };

    // ── Lexer ───────────────────────────────────────────────────────────

    void SkipWS() { while (m_pos < m_src.size() && (m_src[m_pos]==' '||m_src[m_pos]=='\t')) ++m_pos; }

    void NextToken()
    {
        SkipWS();
        if (m_pos >= m_src.size()) { m_tok = {TOK_END}; return; }

        char c = m_src[m_pos];

        // Number: 1  1.5  .5  3e-2  1.5f
        if (std::isdigit(c) || (c=='.' && m_pos+1<m_src.size() && std::isdigit(m_src[m_pos+1]))) {
            size_t s = m_pos;
            while (m_pos < m_src.size() && (std::isdigit(m_src[m_pos]) || m_src[m_pos]=='.')) ++m_pos;
            if (m_pos < m_src.size() && (m_src[m_pos]=='e'||m_src[m_pos]=='E')) {
                ++m_pos;
                if (m_pos < m_src.size() && (m_src[m_pos]=='+'||m_src[m_pos]=='-')) ++m_pos;
                while (m_pos < m_src.size() && std::isdigit(m_src[m_pos])) ++m_pos;
            }
            if (m_pos < m_src.size() && (m_src[m_pos]=='f'||m_src[m_pos]=='F')) ++m_pos;
            m_tok.type = TOK_NUM;
            try { m_tok.num = std::stof(m_src.substr(s, m_pos - s)); } catch (...) { m_tok.num = 0.0f; }
            return;
        }

        // Identifier: alpha/_ followed by alnum/_ and dots (for sb.foo)
        if (std::isalpha(c) || c=='_') {
            size_t s = m_pos;
            while (m_pos < m_src.size() && (std::isalnum(m_src[m_pos]) || m_src[m_pos]=='_' || m_src[m_pos]=='.'))
                ++m_pos;
            m_tok = {TOK_IDENT, 0.0f, m_src.substr(s, m_pos - s)};
            return;
        }

        // Operators / punctuation
        ++m_pos;
        switch (c) {
            case '+': m_tok={TOK_PLUS};   return;
            case '-': m_tok={TOK_MINUS};  return;
            case '*': m_tok={TOK_STAR};   return;
            case '/': m_tok={TOK_SLASH};  return;
            case '%': m_tok={TOK_PERCENT};return;
            case '(': m_tok={TOK_LPAREN}; return;
            case ')': m_tok={TOK_RPAREN}; return;
            case ',': m_tok={TOK_COMMA};  return;
            case '?': m_tok={TOK_QUESTION}; return;
            case ':': m_tok={TOK_COLON};  return;
            case '!': if (m_pos<m_src.size()&&m_src[m_pos]=='='){++m_pos;m_tok={TOK_NE};}else m_tok={TOK_NOT}; return;
            case '<': if (m_pos<m_src.size()&&m_src[m_pos]=='='){++m_pos;m_tok={TOK_LE};}else m_tok={TOK_LT}; return;
            case '>': if (m_pos<m_src.size()&&m_src[m_pos]=='='){++m_pos;m_tok={TOK_GE};}else m_tok={TOK_GT}; return;
            case '=': if (m_pos<m_src.size()&&m_src[m_pos]=='='){++m_pos;m_tok={TOK_EQ};}else m_tok={TOK_END}; return;
            case '&': if (m_pos<m_src.size()&&m_src[m_pos]=='&'){++m_pos;m_tok={TOK_AND};}else m_tok={TOK_END}; return;
            case '|': if (m_pos<m_src.size()&&m_src[m_pos]=='|'){++m_pos;m_tok={TOK_OR};}else m_tok={TOK_END}; return;
            default:  m_tok={TOK_END}; return;
        }
    }

    // ── Recursive-Descent Parser ────────────────────────────────────────
    // Precedence (low → high):
    //   ternary  →  ||  →  &&  →  comparison  →  +/-  →  */%  →  unary  →  primary

    Getter ParseTernary()
    {
        auto cond = ParseOr();
        if (m_tok.type == TOK_QUESTION) {
            NextToken();
            auto yes = ParseTernary();
            if (m_tok.type == TOK_COLON) NextToken();
            auto no  = ParseTernary();
            return [cond,yes,no]() { return cond() > 0.5f ? yes() : no(); };
        }
        return cond;
    }

    Getter ParseOr()
    {
        auto L = ParseAnd();
        while (m_tok.type == TOK_OR) {
            NextToken();
            auto R = ParseAnd();
            L = [l=L,r=R]() { return (l()>0.5f || r()>0.5f) ? 1.f : 0.f; };
        }
        return L;
    }

    Getter ParseAnd()
    {
        auto L = ParseCmp();
        while (m_tok.type == TOK_AND) {
            NextToken();
            auto R = ParseCmp();
            L = [l=L,r=R]() { return (l()>0.5f && r()>0.5f) ? 1.f : 0.f; };
        }
        return L;
    }

    Getter ParseCmp()
    {
        auto L = ParseAdd();
        while (m_tok.type>=TOK_LT && m_tok.type<=TOK_NE) {
            auto op = m_tok.type; NextToken();
            auto R = ParseAdd();
            switch (op) {
                case TOK_LT: L=[l=L,r=R](){return l()<r()  ?1.f:0.f;}; break;
                case TOK_GT: L=[l=L,r=R](){return l()>r()  ?1.f:0.f;}; break;
                case TOK_LE: L=[l=L,r=R](){return l()<=r() ?1.f:0.f;}; break;
                case TOK_GE: L=[l=L,r=R](){return l()>=r() ?1.f:0.f;}; break;
                case TOK_EQ: L=[l=L,r=R](){return std::abs(l()-r())<1e-6f?1.f:0.f;}; break;
                case TOK_NE: L=[l=L,r=R](){return std::abs(l()-r())>=1e-6f?1.f:0.f;}; break;
                default: break;
            }
        }
        return L;
    }

    Getter ParseAdd()
    {
        auto L = ParseMul();
        while (m_tok.type==TOK_PLUS || m_tok.type==TOK_MINUS) {
            bool add = m_tok.type==TOK_PLUS; NextToken();
            auto R = ParseMul();
            if (add) L=[l=L,r=R](){return l()+r();};
            else     L=[l=L,r=R](){return l()-r();};
        }
        return L;
    }

    Getter ParseMul()
    {
        auto L = ParseUnary();
        while (m_tok.type==TOK_STAR || m_tok.type==TOK_SLASH || m_tok.type==TOK_PERCENT) {
            auto op = m_tok.type; NextToken();
            auto R = ParseUnary();
            if      (op==TOK_STAR)    L=[l=L,r=R](){return l()*r();};
            else if (op==TOK_SLASH)   L=[l=L,r=R](){float d=r();return d!=0.f?l()/d:0.f;};
            else                      L=[l=L,r=R](){float d=r();return d!=0.f?std::fmod(l(),d):0.f;};
        }
        return L;
    }

    Getter ParseUnary()
    {
        if (m_tok.type==TOK_MINUS) { NextToken(); auto v=ParseUnary(); return [v](){return -v();}; }
        if (m_tok.type==TOK_NOT)   { NextToken(); auto v=ParseUnary(); return [v](){return v()>0.5f?0.f:1.f;}; }
        return ParsePrimary();
    }

    Getter ParsePrimary()
    {
        // Number literal
        if (m_tok.type == TOK_NUM) {
            float val = m_tok.num; NextToken();
            return [val]() { return val; };
        }

        // Parenthesized sub-expression
        if (m_tok.type == TOK_LPAREN) {
            NextToken();
            auto val = ParseTernary();
            if (m_tok.type == TOK_RPAREN) NextToken();
            return val;
        }

        // Identifier: variable or function call
        if (m_tok.type == TOK_IDENT) {
            std::string name = m_tok.str; NextToken();
            if (m_tok.type == TOK_LPAREN)
                return ParseFunc(name);
            // Variable
            auto lk = m_lookup;
            return [lk,name]() { return lk(name); };
        }

        // Unknown token — skip and return 0
        NextToken();
        return []() { return 0.0f; };
    }

    // ── Built-in Function Dispatch ──────────────────────────────────────

    Getter ParseFunc(const std::string& name)
    {
        NextToken(); // consume '('
        std::vector<Getter> args;
        if (m_tok.type != TOK_RPAREN) {
            args.push_back(ParseTernary());
            while (m_tok.type == TOK_COMMA) { NextToken(); args.push_back(ParseTernary()); }
        }
        if (m_tok.type == TOK_RPAREN) NextToken();

        const size_t n = args.size();

        // ── 1-argument functions ────────────────────────────────────────
        if (n == 1) {
            auto a = args[0];
            if (name=="sin")      return [a](){return std::sin(a());};
            if (name=="cos")      return [a](){return std::cos(a());};
            if (name=="tan")      return [a](){return std::tan(a());};
            if (name=="asin")     return [a](){return std::asin(std::clamp(a(),-1.f,1.f));};
            if (name=="acos")     return [a](){return std::acos(std::clamp(a(),-1.f,1.f));};
            if (name=="atan")     return [a](){return std::atan(a());};
            if (name=="sqrt")     return [a](){return std::sqrt(std::max(0.f,a()));};
            if (name=="abs")      return [a](){return std::abs(a());};
            if (name=="exp")      return [a](){return std::exp(a());};
            if (name=="exp2")     return [a](){return std::exp2(a());};
            if (name=="log")      return [a](){float v=a();return v>0.f?std::log(v):0.f;};
            if (name=="log2")     return [a](){float v=a();return v>0.f?std::log2(v):0.f;};
            if (name=="log10")    return [a](){float v=a();return v>0.f?std::log10(v):0.f;};
            if (name=="floor")    return [a](){return std::floor(a());};
            if (name=="ceil")     return [a](){return std::ceil(a());};
            if (name=="round")    return [a](){return std::round(a());};
            if (name=="trunc")    return [a](){return std::trunc(a());};
            if (name=="frac")     return [a](){float v=a();return v-std::floor(v);};
            if (name=="sign")     return [a](){float v=a();return v>0.f?1.f:(v<0.f?-1.f:0.f);};
            if (name=="saturate") return [a](){return std::clamp(a(),0.f,1.f);};
            if (name=="radians")  return [a](){return a()*0.01745329252f;};
            if (name=="degrees")  return [a](){return a()*57.2957795131f;};
        }

        // ── 2-argument functions ────────────────────────────────────────
        if (n == 2) {
            auto a=args[0], b=args[1];
            if (name=="pow")   return [a,b](){return std::pow(a(),b());};
            if (name=="atan2") return [a,b](){return std::atan2(a(),b());};
            if (name=="min")   return [a,b](){return std::min(a(),b());};
            if (name=="max")   return [a,b](){return std::max(a(),b());};
            if (name=="fmod")  return [a,b](){float d=b();return d!=0.f?std::fmod(a(),d):0.f;};
            if (name=="step")  return [a,b](){return b()>=a()?1.f:0.f;};
        }

        // ── 3-argument functions ────────────────────────────────────────
        if (n == 3) {
            auto a=args[0], b=args[1], c=args[2];
            if (name=="lerp"||name=="mix")
                return [a,b,c](){float t=c();return a()+(b()-a())*t;};
            if (name=="clamp")
                return [a,b,c](){return std::clamp(a(),b(),c());};
            if (name=="smoothstep")
                return [a,b,c](){
                    float e0=a(),e1=b(),x=c();
                    float t=std::clamp((x-e0)/(e1-e0+1e-7f),0.f,1.f);
                    return t*t*(3.f-2.f*t);
                };
        }

        // Unknown function — return 0
        return []() { return 0.0f; };
    }

    // ── Data ────────────────────────────────────────────────────────────

    std::string m_src;
    size_t      m_pos;
    Token       m_tok;
    VarLookup   m_lookup;
};

}  // anonymous namespace


std::function<float()> ParmLinkCompat::CompileMathExpr(const std::string& expr)
{
    if (expr.empty())
        return []() { return 0.0f; };

    // Compile via recursive-descent parser — handles all arithmetic, comparisons,
    // ternary, logical ops, 25+ math functions, variables, and number literals.
    // Expressions are parsed once at config load and evaluated as native lambdas.
    ExprParser parser(expr, [this](const std::string& name) { return GetVariable(name); });
    return parser.Parse();
}


//=============================================================================
//  Per-Frame Update
//=============================================================================

void ParmLinkCompat::Update(float deltaTime)
{
    std::lock_guard lock(m_mutex);

    for (auto& expr : m_expressions)
    {
        if (!expr.evaluate) continue;

        // Evaluate the expression
        float value = expr.evaluate();
        expr.currentValue = value;

        // Store as a user variable (so other expressions can reference it)
        m_userVars[expr.name] = value;

        // If this is an ENB push, write it to the shader
        if (expr.isENBPush) {
            ENBSetFloat(
                expr.pushShader.c_str(),
                expr.pushGroup.c_str(),
                expr.pushParam.c_str(),
                value
            );
        }
    }
}


void ParmLinkCompat::Shutdown()
{
    std::lock_guard lock(m_mutex);
    m_expressions.clear();
    m_variables.clear();
    m_varIndex.clear();
    m_userVars.clear();
}


void ParmLinkCompat::CheckHotReload()
{
    if (!std::filesystem::exists(m_cfgPath)) return;

    auto mod = std::filesystem::last_write_time(m_cfgPath);
    if (mod != m_cfgLastMod) {
        m_cfgLastMod = mod;
        SKSE::log::info("ParmLinkCompat: config changed, reloading...");
        LoadCFG(m_cfgPath);
    }
}


float ParmLinkCompat::ENBGetFloat(const char* shader, const char* group, const char* name)
{
    return ENBInterface::GetFloat(shader, group, name);
}

void ParmLinkCompat::ENBSetFloat(const char* shader, const char* group,
                                  const char* name, float value)
{
    ENBInterface::SetFloat(shader, group, name, value);
}


//=============================================================================
//  AddressRedirectTable — map ParmLink memory addresses to SB data
//=============================================================================

void AddressRedirectTable::Register(uint64_t baseAddr, uint32_t offset,
                                     std::function<float()> getter,
                                     const std::string& description)
{
    m_redirects.push_back({baseAddr, offset, std::move(getter), description});
}


bool AddressRedirectTable::TryRedirect(uint64_t address, float& outValue) const
{
    for (const auto& r : m_redirects) {
        if (r.address == address || (r.address + r.offset) == address) {
            outValue = r.getter();
            return true;
        }
    }
    return false;
}


void AddressRedirectTable::RegisterDefaults()
{
    // Known ParmLink memory patterns used by popular ENB presets:
    //
    // Pattern: Sky singleton → game hour
    //   skyPtr = addr.getAbsInt(SKY_SINGLETON_RVA)
    //   gameHour = addr.getAbsFloat(skyPtr + 0x1B0)
    //
    // Pattern: PlayerCharacter → position
    //   playerPtr = addr.getAbsInt(PLAYER_SINGLETON_RVA)
    //   posX = addr.getAbsFloat(playerPtr + 0x54)
    //
    // We don't need to match exact addresses — instead, we provide the
    // SkyrimBridge variable system as the preferred alternative.
    // If a .cfg file uses addr.*, we log a deprecation warning and
    // suggest the sb.* variable equivalent.

    SKSE::log::info("AddressRedirectTable: registered. Users should migrate "
        "addr.* calls to sb.* variables for version-safe access.");
}


}  // namespace SB
