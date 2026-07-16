#pragma once
//=============================================================================
//  Trackers.h — Aggregated tracker includes and forward declarations
//
//  SkyrimBridge v3.0 - This header provides access to all data trackers
//  for the Phase 2-4 components.
//=============================================================================

#include "BridgeData.h"

// Individual tracker headers from parent project
#include "CelestialTracker.h"
#include "AtmosphereTracker.h"
#include "FogTracker.h"
#include "WeatherTracker.h"
#include "PlayerTracker.h"
#include "CameraTracker.h"
#include "InteriorTracker.h"
#include "ShadowTracker.h"
#include "EffectsTracker.h"
#include "RenderTracker.h"

// v2 expansion trackers (domains 11-17)
#include "ImageSpaceTracker.h"
#include "LightTracker.h"
#include "ActorValueTracker.h"
#include "CrosshairTracker.h"
#include "EquipmentTracker.h"
#include "QuestTracker.h"
#include "UIStateTracker.h"

namespace SB
{
    // Forward declarations for tracker Update functions
    // These are defined in their respective .cpp files

    namespace CelestialTracker  { CelestialData  Update(); }
    namespace AtmosphereTracker { AtmosphereData Update(); }
    namespace FogTracker        { FogData        Update(); }
    namespace WeatherTracker    { WeatherData    Update(float a_deltaTime); }
    namespace PlayerTracker     { PlayerData     Update(); }
    namespace CameraTracker     { CameraData     Update(); }
    namespace InteriorTracker   { InteriorData   Update(); }
    namespace ShadowTracker     { ShadowData     Update(); }
    namespace EffectsTracker    { EffectsData    Update(); }
    namespace RenderTracker     { RenderData     Update(float a_deltaTime); }

    // v2 expansion trackers
    namespace ImageSpaceTracker { ImageSpaceData Update(); }
    namespace LightTracker      { LightData      Update(); }
    namespace ActorValueTracker { ActorValueData Update(); }
    namespace CrosshairTracker  { CrosshairData  Update(); }
    namespace EquipmentTracker  { EquipmentData  Update(); }
    namespace QuestTracker      { QuestData      Update(); }
    namespace UIStateTracker    { UIStateData    Update(); }

    //=========================================================================
    //  AllData accessors for Phase 2-4 components
    //=========================================================================

    // Global data instance for read access
    // Used by ParmLinkCompat and SharedMemoryBridge to read current state
    inline AllData& GetMutableData()
    {
        static AllData s_data;
        return s_data;
    }

    inline const AllData& GetData()
    {
        return GetMutableData();
    }
}
