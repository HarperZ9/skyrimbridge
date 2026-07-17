#pragma once
//=============================================================================
//  SBConfig.h — SkyrimBridge's own configuration substrate
//
//  One flat, readable INI dialect shared by every native component, in the
//  same house style as the plugin's existing configs (WeatherParams.ini,
//  GPU.ini): `[Section]` headers, `Key = Value` pairs, `;`/`#` comments,
//  `R, G, B, A` tuples, `0x..` or decimal FormIDs.
//
//  Deliberately NOT the third-party plugins' formats: no nested
//  `Key = { ... }` blocks, no `.kfg`/`.cfg` extensions, no enumerated
//  `SECTIONnn` scanning. Per-form overrides use a `[Base:FormID]` section
//  suffix — our scheme, not `[ { ID=.. }, .. ]` arrays.
//
//  Header-only, pure (no engine/game access), so parsing is unit-testable.
//
//  Author: Zain Dana Harper
//  License: MIT
//=============================================================================

#include <array>
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace SB::Cfg
{
    // ── Scalar helpers ───────────────────────────────────────────────────
    inline std::string Trim(std::string_view s)
    {
        auto a = s.find_first_not_of(" \t\r\n");
        if (a == std::string_view::npos) return {};
        auto b = s.find_last_not_of(" \t\r\n");
        return std::string(s.substr(a, b - a + 1));
    }

    inline bool AsBool(std::string_view v, bool def = false)
    {
        auto t = Trim(v);
        if (t == "1" || t == "true" || t == "True" || t == "TRUE" || t == "on" || t == "yes")
            return true;
        if (t == "0" || t == "false" || t == "False" || t == "FALSE" || t == "off" || t == "no")
            return false;
        return def;
    }

    inline float AsFloat(std::string_view v, float def = 0.0f)
    {
        auto t = Trim(v);
        if (t.empty()) return def;
        try { return std::stof(t); } catch (...) { return def; }
    }

    inline int AsInt(std::string_view v, int def = 0)
    {
        auto t = Trim(v);
        if (t.empty()) return def;
        try { return std::stoi(t, nullptr, 0); } catch (...) { return def; }
    }

    // Hex (0x..) or decimal → 32-bit FormID. Returns 0 on garbage.
    inline std::uint32_t AsFormID(std::string_view v)
    {
        std::string t = Trim(v);
        if (t.empty()) return 0;
        int base = 10;
        std::size_t off = 0;
        if (t.size() > 2 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X')) { base = 16; off = 2; }
        std::uint32_t id = 0;
        std::from_chars(t.data() + off, t.data() + t.size(), id, base);
        return id;
    }

    // "r, g, b, a" → 4 floats; missing components keep the default.
    inline std::array<float, 4> AsColor4(std::string_view v, std::array<float, 4> def = {0, 0, 0, 1})
    {
        std::array<float, 4> out = def;
        std::istringstream ss{ std::string(v) };
        std::string tok;
        for (int i = 0; i < 4; ++i) {
            if (!std::getline(ss, tok, ',')) break;
            out[i] = AsFloat(tok, out[i]);
        }
        return out;
    }

    // "3C, 0x800, 12" → comma-separated hex/decimal ID list.
    inline std::vector<std::uint32_t> AsIDList(std::string_view v)
    {
        std::vector<std::uint32_t> ids;
        std::istringstream ss{ std::string(v) };
        std::string tok;
        while (std::getline(ss, tok, ',')) {
            auto t = Trim(tok);
            if (!t.empty()) ids.push_back(AsFormID(t));
        }
        return ids;
    }

    // ── Document model ───────────────────────────────────────────────────
    struct Section
    {
        std::string name;   // full header text: "Orbit", or "Weather:0x010C1F"
        std::vector<std::pair<std::string, std::string>> entries;

        // Base name before any ':' suffix ("Weather:0x1" → "Weather").
        std::string Base() const
        {
            auto c = name.find(':');
            return c == std::string::npos ? name : name.substr(0, c);
        }
        // Suffix after ':' as a FormID (0 when absent).
        std::uint32_t SuffixID() const
        {
            auto c = name.find(':');
            return c == std::string::npos ? 0u : AsFormID(name.substr(c + 1));
        }

        const std::string* Find(std::string_view key) const
        {
            for (auto& [k, val] : entries)
                if (k == key) return &val;
            return nullptr;
        }
        std::string Get(std::string_view key, std::string_view def = {}) const
        {
            auto* p = Find(key);
            return p ? *p : std::string(def);
        }
        float Float(std::string_view key, float def) const
        {
            auto* p = Find(key); return p ? AsFloat(*p, def) : def;
        }
        bool Bool(std::string_view key, bool def) const
        {
            auto* p = Find(key); return p ? AsBool(*p, def) : def;
        }
        int Int(std::string_view key, int def) const
        {
            auto* p = Find(key); return p ? AsInt(*p, def) : def;
        }
    };

    struct Document
    {
        std::vector<Section> sections;

        const Section* Find(std::string_view name) const
        {
            for (auto& s : sections)
                if (s.name == name) return &s;
            return nullptr;
        }
        // Every section whose base name matches (the global "Base" plus all
        // "Base:FormID" overrides), preserving file order.
        std::vector<const Section*> AllWithBase(std::string_view base) const
        {
            std::vector<const Section*> out;
            for (auto& s : sections)
                if (s.Base() == base) out.push_back(&s);
            return out;
        }
    };

    inline Document Parse(const std::string& text)
    {
        Document doc;
        Section* cur = nullptr;
        std::istringstream stream(text);
        std::string line;
        while (std::getline(stream, line)) {
            auto t = Trim(line);
            if (t.empty() || t[0] == ';' || t[0] == '#') continue;
            if (t[0] == '[') {
                auto close = t.find(']');
                std::string name = (close != std::string::npos) ? t.substr(1, close - 1) : t.substr(1);
                doc.sections.push_back(Section{ Trim(name), {} });
                cur = &doc.sections.back();
                continue;
            }
            auto eq = t.find('=');
            if (eq == std::string::npos) continue;
            std::string key = Trim(t.substr(0, eq));
            std::string val = Trim(t.substr(eq + 1));
            if (!cur) {  // keys before any header land in an implicit "" section
                doc.sections.push_back(Section{ "", {} });
                cur = &doc.sections.back();
            }
            cur->entries.emplace_back(std::move(key), std::move(val));
        }
        return doc;
    }

    inline Document ParseFile(const std::filesystem::path& path, bool* found = nullptr)
    {
        std::ifstream in(path);
        if (!in) { if (found) *found = false; return {}; }
        std::stringstream buf; buf << in.rdbuf();
        if (found) *found = true;
        return Parse(buf.str());
    }
}
