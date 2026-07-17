#pragma once
//=============================================================================
//  CellReport.h — one-command cell performance census (lane G20)
//
//  The pain it closes: hunting a heavy cell today means Creation Kit or
//  xEdit archaeology guided by forum lore, with shadow-casting lights (the
//  classic FPS killer; the engine renders only a handful at once) found one
//  by one. This walks the player's current cell live and reports:
//    - reference counts by form type, and disabled-flag counts
//    - every shadow-casting light (SpotShadow/HemiShadow/OmniShadow), with
//      the placed ref's FormID, its winning plugin, and distance from the
//      player, sorted nearest first
//    - reference counts per winning plugin (who is crowding this cell)
//
//  Read-only: no engine state is written. Full text goes to
//  SkyrimBridge/dumps/cellreport.txt; callers get the text back (the
//  command channel truncates to its 4 KiB window).
//
//  Author: Zain Dana Harper
//  License: MIT
//=============================================================================

#include <string>

namespace SB::CellReport
{
    // "" when there is no player cell (main menu). Never throws.
    std::string Run();
}
