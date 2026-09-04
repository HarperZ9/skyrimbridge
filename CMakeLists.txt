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

#  The native replacement suite reimplements plugins by Kitsuune, derived from
#  reverse-engineering their compiled binaries. Distributing it needs the
#  author's permission, which has not been granted; see CREDITS.md. Nothing in
#  the build used to separate it from the rest of the plugin, so every compiled
#  SkyrimBridge contained it and no release could be cut without it.
#
#  OFF is the public/default build and keeps the suite absent from the binary
#  entirely, not merely disabled at runtime: an inert copy is still a copy.
#  Private operator builds must opt in explicitly with ON.
option(SKYRIMBRIDGE_NATIVE_REPLACEMENTS
       "Build the Kitsuune-derived native ENB plugin replacement suite" OFF)

set(SKYRIMBRIDGE_NATIVE_SOURCES "")
if(SKYRIMBRIDGE_NATIVE_REPLACEMENTS)
    set(SKYRIMBRIDGE_NATIVE_SOURCES
        src/core/EditorIDCache.cpp
        src/core/KreateProfile.cpp
        src/core/KreateRecords.cpp
        src/core/WorldspaceWeatherlist.cpp
        src/core/SkyLighting.cpp
        src/core/EnbLightInventoryFix.cpp
    )
    message(STATUS "SkyrimBridge: native replacement suite INCLUDED (private explicit opt-in)")
else()
    message(STATUS "SkyrimBridge: native replacement suite EXCLUDED (public/default build)")
endif()

add_commonlibsse_plugin(
    ${PROJECT_NAME}
    AUTHOR "Zain Dana Harper"
    SOURCES
        src/core/main.cpp
        src/core/BridgeApi.cpp
        src/core/BridgeData.cpp
        src/core/ENBInterface.cpp
        src/core/CompatDetect.cpp
        src/core/PapyrusBridge.cpp
        src/core/WriteBackProcessor.cpp
        src/core/WeatherEditor.cpp

        ${SKYRIMBRIDGE_NATIVE_SOURCES}

        # Engine-exposure spine + recovered binary patches
        src/core/EngineReflect.cpp
        src/core/RegionWalker.cpp
        src/core/EngineFixes.cpp

        # Native foreign-format texture codec (non-.dds asset integration)
        src/core/TextureCodec.cpp
        src/core/Inflate.cpp
        src/core/TextureAutoConvert.cpp
        src/core/TextureBC7.cpp
        src/core/TextureLoadHook.cpp
        src/core/ConvexHull.cpp
        src/core/CollisionMaterial.cpp
        src/core/CompressedMesh.cpp
        src/core/ModelCodec.cpp
        src/core/ModelSpawn.cpp
        src/core/BridgeCommand.cpp
        src/core/CellReport.cpp
        src/core/ScriptReport.cpp

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

        # GPU tier: D3D11 integration, compute infrastructure, renderers
        src/core/D3D11Hook.cpp
        src/core/ComputeManager.cpp
        src/core/SRVInjector.cpp
        src/core/RenderPassManager.cpp
        src/core/RenderPipeline.cpp
        src/core/HiZPyramid.cpp
        src/core/PhaseDispatcher.cpp
        src/core/SceneData.cpp
        src/core/AtmosphereRenderer.cpp
        src/core/VolumetricClouds.cpp
        src/core/BootDiagnostics.cpp
        src/core/GPUProfiler.cpp
        src/core/FrameCapture.cpp
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    d3d11
    dxgi
    d3dcompiler
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    ENABLE_SKYRIM_SE=1
    ENABLE_SKYRIM_AE=1
    SKYRIMBRIDGE_V3=1
    SKYRIMBRIDGE_BUILDING_DLL=1
    HAS_SKSE=1
    SKYRIMBRIDGE_NATIVE_REPLACEMENTS=$<BOOL:${SKYRIMBRIDGE_NATIVE_REPLACEMENTS}>
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
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/config/WriteBackConfig.ini"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/config/WriteBackConfig.ini"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/config/GPU.ini"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/config/GPU.ini"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/config/SkyrimBridge.ini"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/config/SkyrimBridge.ini"
    COMMENT "Copying public config files to build directory"
)

if(SKYRIMBRIDGE_NATIVE_REPLACEMENTS)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/config"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/config/Sky.ini"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/config/Sky.ini"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/config/WeatherRouting.example.ini"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/config/WeatherRouting.example.ini"
        COMMENT "Copying private native-suite config files to build directory"
    )
endif()

# ============================================================================
# d3d11 proxy — full D3D11 API wrap for mid-frame injection
#
# Standalone DLL with no game/SKSE/vcpkg dependencies. Load it directly as
# the game's d3d11.dll (no ENB), or chain it through ENB's [PROXY]
# ProxyLibrary setting. The SKSE plugin auto-detects it at runtime.
# ============================================================================

enable_language(ASM_MASM)

add_library(d3d11_proxy SHARED
    src/d3d11_proxy/proxy_main.cpp
    src/d3d11_proxy/WrappedDevice.cpp
    src/d3d11_proxy/WrappedContext.cpp
    src/d3d11_proxy/WrappedSwapChain.cpp
    src/d3d11_proxy/ShaderManager.cpp
    src/d3d11_proxy/RenderPhaseDetector.cpp
    src/d3d11_proxy/CBDirtyTracker.cpp
    src/d3d11_proxy/MaterialPipeline.cpp
    src/d3d11_proxy/AlbedoExtractor.cpp
    src/d3d11_proxy/OcclusionCuller.cpp
    src/d3d11_proxy/d3d11_trampolines.asm
    src/d3d11_proxy/d3d11_proxy.def
)

set_target_properties(d3d11_proxy PROPERTIES
    OUTPUT_NAME "d3d11"
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)

target_include_directories(d3d11_proxy PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/d3d11_proxy
)

target_compile_definitions(d3d11_proxy PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

# Do NOT link d3d11.lib (we ARE d3d11.dll) or dxgi.lib (importing dxgi
# corrupts graphics-stack init order; factories load lazily instead).

# ============================================================================
# Offline release validation
# ============================================================================

include(CTest)
if(BUILD_TESTING)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)

    add_executable(SkyrimBridgeConfigTest
        tests/sbconfig_inline_comment_test.cpp
    )
    target_include_directories(SkyrimBridgeConfigTest PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/core
    )
    target_compile_features(SkyrimBridgeConfigTest PRIVATE cxx_std_23)
    add_test(
        NAME SkyrimBridge.ConfigInlineComments
        COMMAND SkyrimBridgeConfigTest
    )

    add_test(
        NAME SkyrimBridge.PortableValidation
        COMMAND
            "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/run_validation.py"
            --mode portable
            --receipt-dir "${CMAKE_CURRENT_BINARY_DIR}/validation"
    )
    set_tests_properties(
        SkyrimBridge.PortableValidation
        PROPERTIES
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            TIMEOUT 600
    )

    add_test(
        NAME SkyrimBridge.RepoFacts
        COMMAND
            "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/check_repo_facts.py"
    )
    add_test(
        NAME SkyrimBridge.RepoArt
        COMMAND
            "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/check_repo_art.py"
    )
    set_tests_properties(
        SkyrimBridge.RepoFacts
        SkyrimBridge.RepoArt
        PROPERTIES
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            LABELS "documentation;artwork"
    )
endif()
