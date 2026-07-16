#pragma once
//=============================================================================
//  ENBInterface_v3.h — Convenience wrappers for ENB SDK access
//
//  SkyrimBridge v3.0 - Additional helper functions for Phase 2-4 components
//  that build on the base ENBInterface.
//=============================================================================

#include "ENBInterface.h"
#include "BridgeData.h"
#include <cstring>
#include <string>

namespace ENBInterface
{
    //=========================================================================
    //  Convenience functions for v3 components
    //=========================================================================

    // Set a single float parameter to a specific shader
    inline void SetFloat(const char* shader, const char* group,
                         const char* name, float value)
    {
        if (!SetParameter) return;
        ENBParameter param;
        param.Size = sizeof(float);
        param.Type = ENBParameterType::ENBParam_FLOAT;
        std::memcpy(param.Data, &value, sizeof(float));
        SetParameter(nullptr, shader, name, &param);
    }

    // Get a single float parameter from a specific shader
    inline float GetFloat(const char* shader, const char* group,
                          const char* name)
    {
        float value = 0.0f;
        if (GetParameter) {
            ENBParameter outParam;
            if (GetParameter(nullptr, shader, name, &outParam) && outParam.Size >= 4) {
                std::memcpy(&value, outParam.Data, sizeof(float));
            }
        }
        return value;
    }

    // Set a Float4 parameter to a specific shader
    inline void SetFloat4Shader(const char* shader, const char* group,
                                const char* name, const SB::Float4& value)
    {
        if (!SetParameter) return;
        ENBParameter param;
        param.Size = 16;
        param.Type = ENBParameterType::ENBParam_COLOR4;
        std::memcpy(param.Data, &value, 16);
        SetParameter(nullptr, shader, name, &param);
    }

    // Set a Float4 parameter to all target shaders
    inline void SetFloat4(const char* name, const SB::Float4& value)
    {
        if (!SetParameter) return;
        ENBParameter param;
        param.Size = 16;
        param.Type = ENBParameterType::ENBParam_COLOR4;
        std::memcpy(param.Data, &value, 16);
        for (const auto* shader : SB::kTargetShaders) {
            SetParameter(nullptr, shader, name, &param);
        }
    }

    // Set a float to all target shaders
    inline void SetFloatAll(const char* name, float value)
    {
        if (!SetParameter) return;
        ENBParameter param;
        param.Size = sizeof(float);
        param.Type = ENBParameterType::ENBParam_FLOAT;
        std::memcpy(param.Data, &value, sizeof(float));
        for (const auto* shader : SB::kTargetShaders) {
            SetParameter(nullptr, shader, name, &param);
        }
    }
}
