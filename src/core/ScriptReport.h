#pragma once
//=============================================================================
//  ScriptReport.h — live Papyrus VM monitor (lane G21)
//
//  The pain it closes: script-load and save-health diagnosis is post-mortem
//  today (load the save in ReSaver after the damage is done). This reads the
//  running VM live: is it overstressed, how deep is the function-message
//  queue (the direct script-lag indicator), how many stacks are running,
//  latent, or frozen, which script classes are executing RIGHT NOW (top
//  stack frames), and which classes hold the most attached instances (the
//  save-bloat census).
//
//  Read-only. Threading contract: Run() takes the VM's own spin locks
//  briefly, so it must execute on the frame thread, never inside a Papyrus
//  native (the VM may hold its locks around native dispatch). The native
//  therefore calls Request(); Tick(), wired into the frame update, fulfills
//  it. The command channel dispatch already runs on the frame thread and
//  calls Run() directly.
//
//  Full text goes to SkyrimBridge/dumps/scriptreport.txt.
//
//  Author: Zain Dana Harper
//  License: MIT
//=============================================================================

#include <string>

namespace SB::ScriptReport
{
    // Frame-thread only. "" when the VM is unavailable. Never throws.
    std::string Run();

    // Queue a report from any thread (the Papyrus native uses this).
    void Request();

    // Called once per frame update; fulfills a queued Request().
    void Tick();
}
