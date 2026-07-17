#pragma once
//=============================================================================
//  EngineReflect.h — schema-driven reflection over engine records
//
//  The spine of SkyrimBridge's engine-exposure utility. CommonLibSSE-NG gives
//  typed C++ structs for the engine's records; EngineReflect adds a runtime
//  SCHEMA over them (field name / type / accessor) and three generic ops on
//  top: Read (form -> value tree), Write (tree -> form), Translate (tree <->
//  our flat INI), plus a round-trip Verify that WITNESSES losslessness.
//
//  "Lossless" holds over the persistent, schema-defined fields, NOT raw memory:
//  derived/cached state (live pointers, handles, temporaries) is deliberately
//  outside the schema. Writes are bounded: form-type gated, field-typed, and
//  only applied to registered record types.
//
//  Author: Zain Dana Harper
//  License: MIT
//=============================================================================

#include <RE/Skyrim.h>

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace SB::Reflect
{
    // ── A reflected field value (tagged) ─────────────────────────────────
    struct Value
    {
        enum class Kind { Float, Int, Bool, Color3, Color4, FormLink, String };

        Kind          kind = Kind::Float;
        double        num = 0.0;                 // Float / Int / Bool
        float         col[4] = { 0, 0, 0, 1 };   // Color3 / Color4 (normalized)
        std::uint32_t id = 0;                    // FormLink
        std::string   str;                       // String

        static Value F(double v)                          { Value x; x.kind = Kind::Float; x.num = v; return x; }
        static Value I(long long v)                       { Value x; x.kind = Kind::Int;   x.num = static_cast<double>(v); return x; }
        static Value B(bool v)                            { Value x; x.kind = Kind::Bool;  x.num = v ? 1 : 0; return x; }
        static Value C3(float r, float g, float b)        { Value x; x.kind = Kind::Color3; x.col[0]=r; x.col[1]=g; x.col[2]=b; return x; }
        static Value C4(float r, float g, float b, float a){ Value x; x.kind = Kind::Color4; x.col[0]=r; x.col[1]=g; x.col[2]=b; x.col[3]=a; return x; }
        static Value Link(std::uint32_t i)                { Value x; x.kind = Kind::FormLink; x.id = i; return x; }
        static Value S(std::string s)                     { Value x; x.kind = Kind::String; x.str = std::move(s); return x; }

        std::string ToText() const;
        static Value FromText(Kind k, const std::string& t);
        bool ApproxEquals(const Value& o) const;
    };

    using Tree = std::vector<std::pair<std::string, Value>>;   // ordered field name -> value

    // ── A reflected field: name, kind, typed accessors over a form ───────
    struct Field
    {
        std::string  name;
        Value::Kind  kind;
        std::function<Value(const RE::TESForm*)>       get;
        std::function<void(RE::TESForm*, const Value&)> set;
    };

    struct Schema
    {
        std::string        name;         // label, e.g. "ImageSpace"
        RE::FormType       formType;
        std::vector<Field> fields;

        const Field* Find(std::string_view n) const;
    };

    // ── Registry ─────────────────────────────────────────────────────────
    void          RegisterBuiltins();               // idempotent; call once at init
    const Schema* SchemaFor(RE::FormType t);
    const Schema* SchemaByName(std::string_view name);
    std::vector<std::string> RegisteredSchemas();

    // ── Core ops ─────────────────────────────────────────────────────────
    Tree        Read(RE::TESForm* form);                       // schema-driven; empty if unregistered
    int         Write(RE::TESForm* form, const Tree& tree);    // returns fields written
    std::string ToINI(const Schema& s, RE::FormID id, const Tree& t);
    Tree        FromINI(const Schema& s, const std::string& text);

    // ── Discovery ────────────────────────────────────────────────────────
    std::string ListSchemas();                          // one line per schema: name, form type, field count
    std::string DescribeSchema(const Schema& s);        // field name + kind, one per line

    // The form's plugin override chain, oldest first, winner last ("which
    // mod won this record", scriptable, for ANY form). "" if the form does
    // not exist; runtime-created forms are named as such.
    std::string SourceChain(RE::FormID id);

    // ── High level (FormID driven) ───────────────────────────────────────
    std::string Dump(RE::FormID id);                    // "" if no schema/form
    int         Apply(RE::FormID id, const std::string& text);

    struct VerifyResult { bool ok = false; int fields = 0; std::string detail; };
    VerifyResult Verify(RE::FormID id);                 // Read -> INI -> Read == Read (witnessed)

    // Strict mode: Read -> Write back -> Read, compare. MUTATES the form (it
    // rewrites every schema field with its own current value), so it is a
    // separate, explicitly opted-into op and never the default Verify.
    VerifyResult VerifyStrict(RE::FormID id);
}
