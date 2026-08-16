#include "UIStateTracker.h"
#include <RE/Skyrim.h>

namespace SB::UIStateTracker
{
    UIStateData Update()
    {
        UIStateData data{};

        auto* ui = RE::UI::GetSingleton();
        if (!ui)
            return data;

        // ── Menu state (public scalar components) ─────────────────────
        bool isInMenu = ui->IsShowingMenus();
        bool isInDialogue = ui->IsMenuOpen(RE::DialogueMenu::MENU_NAME);
        bool isInInventory = ui->IsMenuOpen(RE::InventoryMenu::MENU_NAME);
        bool isInMap = ui->IsMenuOpen(RE::MapMenu::MENU_NAME);
        data.Menus.x = isInMenu ? 1.0f : 0.0f;
        data.Menus.y = isInDialogue ? 1.0f : 0.0f;
        data.Menus.z = isInInventory ? 1.0f : 0.0f;
        data.Menus.w = isInMap ? 1.0f : 0.0f;

        // ── HUD state (public scalar components) ──────────────────────
        bool isHUDVisible = ui->IsMenuOpen(RE::HUDMenu::MENU_NAME);
        bool isCrosshairVisible = !ui->IsShowingMenus() && isHUDVisible;

        // Cinematic mode: check for letterbox bars / kill cam
        bool isInCinematicMode = false;
        auto* cam = RE::PlayerCamera::GetSingleton();
        if (cam) {
            isInCinematicMode =
                (cam->currentState == cam->cameraStates[RE::CameraState::kTween]) ||
                (cam->currentState == cam->cameraStates[RE::CameraState::kVATS]) ||
                (cam->currentState == cam->cameraStates[RE::CameraState::kBleedout]);
        }

        bool isLoading = ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME);
        data.HUD.x = isHUDVisible ? 1.0f : 0.0f;
        data.HUD.y = isCrosshairVisible ? 1.0f : 0.0f;
        data.HUD.z = isInCinematicMode ? 1.0f : 0.0f;
        data.HUD.w = isLoading ? 1.0f : 0.0f;

        // ── Detail menu state (public scalar components) ──────────────
        bool isInCrafting = ui->IsMenuOpen(RE::CraftingMenu::MENU_NAME);
        bool isInBook = ui->IsMenuOpen(RE::BookMenu::MENU_NAME);
        bool isInLockpick = ui->IsMenuOpen(RE::LockpickingMenu::MENU_NAME);
        bool isInConsole = ui->IsMenuOpen(RE::Console::MENU_NAME);
        data.Detail.x = isInCrafting ? 1.0f : 0.0f;
        data.Detail.y = isInBook ? 1.0f : 0.0f;
        data.Detail.z = isInLockpick ? 1.0f : 0.0f;
        data.Detail.w = isInConsole ? 1.0f : 0.0f;

        return data;
    }
}
