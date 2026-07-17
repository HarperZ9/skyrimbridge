# SkyrimBridge engine-exposure — session continuation (2026-07-16, part 2)

You are continuing a live project. Read `docs/ENGINE-EXPOSURE-GOAL.md` first
(the authoritative roadmap and the DRM-wall reality), then this file for the
exact current cursor. The mission: turn SkyrimBridge into a safe utility that
exposes the whole engine (read/write/translate/verify any record) and natively
integrates foreign assets. Take it all the way; keep every claim honest.

## How to work here (non-negotiable, from the operator)
- Accountability partner, not a cheerleader. No sycophancy. State the answer.
- Truth over approval: one-word confidence labels (high/moderate/low/unknown) on
  offsets, IDs, versions, file:line. "Unknown" beats a plausible fabrication.
- Honest nulls stay. If it is not done or not validated, say so.
- In-game validation is the acceptance oracle for anything that mutates live
  engine state. You cannot run the game. Make the offline-provable part provable
  (port to Python, cross-check vs ground truth) and hand the operator exact
  in-game checks for the rest. Ship risky live-write features config-gated OFF.
- No dying subagent fan-outs. Implement inline, single-author. Read-only surveys
  via agents are fine (that is how AELAS was reversed this session).
- No em-dashes (rewrite, do not swap the character). No biological metaphors for
  code. Feature-first, no hype.
- The exe is DRM-packed; never statically reverse it. Build on CommonLibSSE-NG
  RE:: types; reverse only specific plugin-DLL gaps as needed.

## Build / install / validate loop
```
cd C:\dev\skyrimbridge
cmake --build build --target SkyrimBridge --config Release
cp build/Release/SkyrimBridge.dll "E:/Modlists/SkyGroundChronicles/mods/SkyrimBridge/SKSE/Plugins/SkyrimBridge.dll"
```
Offline validation is the accepted method: port the codec/protocol to Python,
cross-check against ground truth (PIL / zlib / real modlist files), keep the
receipt in `tests/`. Commit on branch `feat/engine-exposure-native-replacements`
(never `main`); commit messages via `git commit -F <file>` (PowerShell mangles
here-strings). End messages with the Co-Authored-By trailer.

## What is committed (branch feat/engine-exposure-native-replacements)
Through `fb3238b`. Lanes A (all), B5-B8, C9 (the 80%), D11 done. Highlights:
- EngineReflect: 11 schemas (788 fields) + List + VerifyStrict + RegionWalker.
- TextureCodec + Inflate: PNG/TGA/BMP/DDS decode, RGBA8/BC1/BC3 DDS write +
  mips, TGA write. TextureAutoConvert + TextureLoadHook (B8, gated OFF).
- ModelCodec (C9): OBJ + glTF/GLB -> SSE NIF (BSTriShape), offline-proven
  (`tests/validate_model_codec.py`, 21 checks). Native ConvertModel.
- Docs: `docs/SPEC-ENGINE-EXPOSURE.md`. 31 Papyrus natives.

## FIRST ACTION: D10 is written + offline-proven but NOT compiled or committed
Uncommitted on disk right now (do NOT lose these):
- NEW: `src/SB_CommandLayout.h`, `src/core/BridgeCommand.{h,cpp}`,
  `tools/SkyrimBridgeClient.h`, `tests/validate_command_protocol.py`
- MODIFIED: `CMakeLists.txt` (added BridgeCommand.cpp),
  `src/core/main.cpp` (include, NativeConfig.commandSurface, LoadNativeConfig
  entry, Poll() in DoFrameUpdate, Initialize at kDataLoaded, Shutdown on exit)

D10 = an external request/response command mailbox (second shared-memory region
`SkyrimBridge_Command`) so an external editor drives EngineReflect / RegionWalker
/ TextureCodec / ModelCodec, not only the console. The protocol is offline-proven
(`validate_command_protocol.py`, 15/15: ABI layout + sequence-gated round-trip).
The C++ has NOT been compiled yet — the build was interrupted.

Do this, in order:
1. `cmake --build ...` and FIX any compile errors in BridgeCommand.cpp /
   main.cpp. Likely watch-items: the `__try/__except` in `Poll()` must have no
   C++ unwinding locals in that function (Dispatch is a separate function, which
   is correct); `<algorithm>`/`<atomic>` already included; `strnlen` from
   `<cstring>`. Confirm `SB_CommandBlock` size static_assert (5184) holds.
2. Add the `[Native] CommandSurface = false` toggle (and a short comment) to
   BOTH `config/SkyrimBridge.ini` (repo) and the installed
   `E:\Modlists\...\mods\SkyrimBridge\SKSE\Plugins\SkyrimBridge\SkyrimBridge.ini`.
3. Copy the DLL to the modlist path (above).
4. Update `docs/SPEC-ENGINE-EXPOSURE.md` (add the command-channel section + the
   verb table already documented in `SB_CommandLayout.h`) and bump the native
   count line in `ENGINE-EXPOSURE-GOAL.md` / mark lane D10 done there.
5. Commit D10.
Then update the memory file `[[skyrimbridge-engine-exposure-vision-2026-07-16]]`.

## E12 — AELAS reversal DONE; SkyLighting sun-reposition NOT yet implemented
- The reversal is complete and WRITTEN to
  `C:\dev\protected\reverse-engineering\kitsuune-plugins-2026-07-16\AELAS-behavioral-spec.md`.
  Read it. Decisive: AELAS is a KiLoader/KiRELibSkyrim plugin, NOT CommonLibSSE;
  NO usable Address Library IDs were recoverable. Do not bind blind.
- The legitimate completion (no reversed ID): CommonLib's own `RE::Sun`
  (`build/vcpkg_installed/.../include/RE/S/Sun.h`) exposes `sunBaseNode`
  (NiBillboardNode @0x10) and `light`/`cloudLight` (NiDirectionalLight
  @0x38/0x40). SkyLighting already has `SkyModel::ComputeSun` (the orbital math).
- IMPLEMENT in `SkyLighting::Update()` (`src/core/SkyLighting.cpp`), behind the
  EXISTING gates `m_cfg.masterEnable && m_cfg.moveSun` (both ship OFF): compute
  the incident vector, set `sun->sunBaseNode->local.translation = dir * radius`,
  force refresh via `RE::NiAVObject::Update` (RELOCATION_ID(68900, 70251),
  confirmed reusable). Currently `Update()` logs "orbital sun repositioning
  needs the sky-update hook (pending) — ignored this build" — replace that with
  the real gated write.
- Honest nulls to keep: the sky-domain radius (needs in-game tuning), whether
  the directional SHADOW follows (vs only the sun disc), and the timing fight
  with vanilla `Sun::Update` (may need a post-update write or a `Sky::Update`
  hook — the remaining RE). Offline-provable: the math (validate ComputeSun vs
  known sunrise/sunset). Game-bound: the writes.

## E13 — prepare gated-feature validation, do NOT flip any defaults
Operator-gated by construction. Do NOT enable anything. Deliver:
- A concise validation protocol doc (e.g. `docs/VALIDATION-PROTOCOL.md`) with
  exact in-game acceptance steps + expected log lines for each OFF-by-default
  feature: EnbLightInventoryFix, `[Sky] Enable` (+ new MoveSun), Texture
  AutoConvert, TextureLoadHook, CommandSurface, and `[Sky] Enable`'s ambient.
- Optional: a one-line self-check log at init for each gated component (what it
  hooked / what it will write) so the operator can confirm wiring without
  guessing. Additive logging only.
- Leave every default OFF. The operator flips them after confirming in-game.

## Remaining after these
BC7 / DX10-header DDS; the runtime NiObject-graph construction (C9's ambitious
20%); deeper AELAS shadow-light coupling + the vanilla-timing hook (E12 tail);
whatever the operator surfaces next. Finish D10, land E12's gated write, deliver
E13, keep the nulls honest.
