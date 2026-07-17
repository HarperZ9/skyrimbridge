#pragma once
//=============================================================================
//  EnbLightInventoryFix.h — native "ENB Light Inventory Fix" replacement
//
//  Reimplements, clean-room, the third-party inventory-3D ENB-light fix:
//  when the menu renders an item's rotating 3D preview, particle-lights baked
//  into that mesh must not leak into the world scene. The original hooks two
//  inventory-3D passes and, for the previewed item's node, temporarily zeroes
//  each light's emittance field, forces a transform update, runs the original,
//  then restores. This reproduces that behavior from disassembly evidence.
//
//  AE-targeted (the reversed Address Library IDs are AE 1.6.x; the third-party
//  binary itself only supports AE). Config-gated and OFF by default — enable
//  in SkyrimBridge.ini [Native] EnbLightInventoryFix and validate in-game.
//  Installs are SEH-guarded so a mismatch degrades to a no-op, never a crash.
//
//  Author: Zain Dana Harper
//  License: MIT
//=============================================================================

namespace SB
{
    class EnbLightInventoryFix
    {
    public:
        static EnbLightInventoryFix& Get();

        // Install the two full-function hooks. No-op on SE/VR, when already
        // installed, or when the Address Library IDs do not resolve.
        void Install();

        bool IsInstalled() const { return m_installed; }

    private:
        EnbLightInventoryFix() = default;
        bool m_installed = false;
    };
}
