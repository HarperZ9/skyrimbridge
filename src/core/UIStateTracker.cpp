#include "UIStateTracker.h"
#include <RE/Skyrim.h>
#include <bit>

namespace SB::UIStateTracker
{
    UIStateData Update()
    {
        UIStateData data{};

        auto* ui = RE::UI::GetSingleton();
        if (!ui)
            return data;

        // ── Menu state (packed uint bitfield) ────────────────────────
        uint32_t menuBits = 0;
        if (ui->IsShowingMenus())                                menuBits |= (1u << 0);
        if (ui->IsMenuOpen(RE::DialogueMenu::MENU_NAME))        menuBits |= (1u << 1);
        if (ui->IsMenuOpen(RE::InventoryMenu::MENU_NAME))       menuBits |= (1u << 2);
        if (ui->IsMenuOpen(RE::MapMenu::MENU_NAME))             menuBits |= (1u << 3);
        data.Menus.x = std::bit_cast<float>(menuBits);
        data.Menus.y = 0.0f;
        data.Menus.z = 0.0f;
        data.Menus.w = 0.0f;

        // ── HUD state (packed uint bitfield) ─────────────────────────
        uint32_t hudBits = 0;
        bool hudOpen = ui->IsMenuOpen(RE::HUDMenu::MENU_NAME);
        if (hudOpen)                                             hudBits |= (1u << 0);
        if (!ui->IsShowingMenus() && hudOpen)                    hudBits |= (1u << 1);

        // Cinematic mode: check for letterbox bars / kill cam
        auto* cam = RE::PlayerCamera::GetSingleton();
        if (cam) {
            bool isThirdPersonCinematic =
                (cam->currentState == cam->cameraStates[RE::CameraState::kTween]) ||
                (cam->currentState == cam->cameraStates[RE::CameraState::kVATS]) ||
                (cam->currentState == cam->cameraStates[RE::CameraState::kBleedout]);
            if (isThirdPersonCinematic)                          hudBits |= (1u << 2);
        }

        if (ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME))         hudBits |= (1u << 3);
        data.HUD.x = std::bit_cast<float>(hudBits);
        data.HUD.y = 0.0f;
        data.HUD.z = 0.0f;
        data.HUD.w = 0.0f;

        // ── Detail menu state (packed uint bitfield) ─────────────────
        uint32_t detailBits = 0;
        if (ui->IsMenuOpen(RE::CraftingMenu::MENU_NAME))        detailBits |= (1u << 0);
        if (ui->IsMenuOpen(RE::BookMenu::MENU_NAME))            detailBits |= (1u << 1);
        if (ui->IsMenuOpen(RE::LockpickingMenu::MENU_NAME))     detailBits |= (1u << 2);
        if (ui->IsMenuOpen(RE::Console::MENU_NAME))             detailBits |= (1u << 3);
        data.Detail.x = std::bit_cast<float>(detailBits);
        data.Detail.y = 0.0f;
        data.Detail.z = 0.0f;
        data.Detail.w = 0.0f;

        return data;
    }
}
