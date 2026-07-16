cmake_minimum_required(VERSION 3.24)

# Static triplet + static runtime, matching CommonLibSSE-NG plugin practice
set(VCPKG_TARGET_TRIPLET "x64-windows-static" CACHE STRING "")
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" CACHE STRING "")

project(
    SkyrimBridge
    VERSION 3.0.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(CommonLibSSE REQUIRED CONFIG)

# ============================================================================
# SkyrimBridge — live engine state for ENB shaders and external tools
#
#   src/core/  — measurement trackers, ENB SDK interface, Papyrus bridge
#   src/       — weather parameter computer, shared memory, ParmLink compat
# ============================================================================

add_commonlibsse_plugin(
    ${PROJECT_NAME}
    AUTHOR "Zain Dana Harper"
    SOURCES
        src/core/main.cpp
        src/core/BridgeData.cpp
        src/core/ENBInterface.cpp
        src/core/CompatDetect.cpp
        src/core/PapyrusBridge.cpp

        # Core trackers (domains 1-10)
        src/core/CelestialTracker.cpp
        src/core/AtmosphereTracker.cpp
        src/core/FogTracker.cpp
        src/core/WeatherTracker.cpp
        src/core/PlayerTracker.cpp
        src/core/CameraTracker.cpp
        src/core/InteriorTracker.cpp
        src/core/ShadowTracker.cpp
        src/core/EffectsTracker.cpp
        src/core/RenderTracker.cpp

        # v2 expansion trackers (domains 11-17)
        src/core/ImageSpaceTracker.cpp
        src/core/LightTracker.cpp
        src/core/ActorValueTracker.cpp
        src/core/CrosshairTracker.cpp
        src/core/EquipmentTracker.cpp
        src/core/QuestTracker.cpp
        src/core/UIStateTracker.cpp

        # v3 expansion trackers (CPU-side)
        src/core/RegionTracker.cpp
        src/core/AudioTracker.cpp
        src/core/NPCDetectTracker.cpp

        # Phase 2: weather parameter computer
        src/WeatherParameterComputer.cpp
        # Phase 3: shared-memory channel
        src/SharedMemoryBridge.cpp
        # Phase 4: enbParmLink compatibility
        src/ParmLinkCompat.cpp
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    ENABLE_SKYRIM_SE=1
    ENABLE_SKYRIM_AE=1
    SKYRIMBRIDGE_V3=1
    HAS_SKSE=1
)

target_precompile_headers(${PROJECT_NAME} PRIVATE
    src/core/PCH.h
)

# Ship the default weather parameter config next to the plugin
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/config"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/config/WeatherParams.ini"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/config/WeatherParams.ini"
    COMMENT "Copying config files to build directory"
)
