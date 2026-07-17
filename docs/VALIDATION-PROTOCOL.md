# SkyrimBridge in-game validation protocol (gated features)

Every feature below ships OFF. This document is the acceptance procedure: the
operator enables one feature at a time, runs the steps, records the result,
and only then decides whether the default flips. Nothing here is validated
until the operator says so; offline receipts (the `tests/` harnesses) cover
the math and codecs, not live engine behavior.

**Paths**
- Installed config: `E:\Modlists\SkyGroundChronicles\mods\SkyrimBridge\SKSE\Plugins\SkyrimBridge\SkyrimBridge.ini`
  (and `Sky.ini` next to it). In-game these resolve under
  `Data/SKSE/Plugins/SkyrimBridge/`.
- Log: `Documents\My Games\Skyrim Special Edition\SKSE\SkyrimBridge.log`.
- Configs load once at data-load: restart the game after every toggle change.

**General loop:** flip ONE toggle, start the game, grep the log for the init
line, run the steps, record PASS/FAIL plus the observations listed, flip the
toggle back OFF unless you are keeping the feature. Test features separately;
two of them (the texture pair) actively mask each other.

| # | Feature | Toggle | Init log line to grep |
|---|---|---|---|
| 1 | EnbLightInventoryFix | `[Native] EnbLightInventoryFix` | `EnbLightInventoryFix: installed inventory-3D light hooks` |
| 2 | Dynamic night ambient | `Sky.ini [Sky] Enable` | `SkyLighting: loaded Sky.ini (masterEnable=true` |
| 3 | Orbital sun repositioning | `[Sky] Enable` + `[Orbit] MoveSun` | `SkyLighting: MoveSun armed` |
| 4 | TextureAutoConvert | `[Native] TextureAutoConvert` | `TextureAutoConvert: scan done` |
| 5 | TextureLoadHook | `[Native] TextureLoadHook` | `TextureLoadHook: LooseFileLocation detours installed` |
| 6 | CommandSurface | `[Native] CommandSurface` | `BridgeCommand: command channel active` |

---

## 1. EnbLightInventoryFix

Replaces ELIF.dll (inventory-3D previews leaking ENB particle light into the
world). AE-only; trampoline detours on AE IDs 51769/51770.

**Setup:** `[Native] EnbLightInventoryFix = true`, and disable the real
`ELIF.dll` for the test (rename it or disable its mod): both active means two
detours on the same functions, an untested interaction.

**Init log:** `EnbLightInventoryFix: installed inventory-3D light hooks (ok)`.
`(partial)` means one of the two hooks failed: record it, treat as FAIL.
`EnbLightInventoryFix: install failed (...); disabled` is a hard FAIL.

**Steps:** load an exterior night save with the ENB preset active. Take an
item whose model carries an ENB particle light (a torch or a glowing weapon).
Open the inventory, select the item so the 3D preview renders, rotate it.
Close and reopen ten times.

**Pass:** no light flash or glow leaks into the world scene behind the menu
while the preview is up; the preview itself still renders lit; no crash.

**Record:** ok vs partial; any visual difference from how real ELIF behaved.

## 2. Dynamic night ambient (`[Sky] Enable`)

**Setup:** `Sky.ini`: `[Sky] Enable = true`, `[Orbit] MoveSun = false`
(default), `[Ambient] Enable = true` (default).

**Init log:** `SkyLighting: loaded Sky.ini (masterEnable=true, 0 overrides)`
(override count as configured).

**Steps:** exterior, clear weather. `set gamehour to 12`: daytime must look
identical to a `[Sky] Enable = false` run (the write only happens after
twilight). `set gamehour to 0`: compare the same scene against the disabled
run. Walk the hour through 19, 20, 21, 22 to cross twilight.

**Pass:** night ambient visibly shifts toward the configured `[NightSky]`
color and intensity; the transition across twilight is smooth (no stepping or
flicker); day is untouched.

**Record:** whether the strength suits the preset (tune `[Ambient] TiltScale`
and `[NightSky]` values before judging the mechanism).

## 3. Orbital sun repositioning (`[Orbit] MoveSun`)

The AELAS-replacement behavior. The reversal proved AELAS's own IDs are not
bindable; this implementation redirects `Sky->sun->sunBaseNode` through
CommonLib's typed members instead, along `SkyModel::ComputeSun`'s incident
vector (math offline-proven, 17/17, `tests/validate_sky_model.py`).

**Setup:** `Sky.ini`: `[Sky] Enable = true` AND `[Orbit] MoveSun = true`.

**Init log:** `SkyLighting: MoveSun armed (will redirect
Sky->sun->sunBaseNode along the orbital incident vector each frame,
preserving the node's own orbit radius)`.
On the first frame write: `SkyLighting: sun repositioned (alt=... deg,
az=... deg, radius=...)`.

**Steps:** exterior, clear weather. `set gamehour to 8`, screenshot the sun
disc; repeat at 12 and 16. Run the same hours with `MoveSun = false` and
compare positions. With the default `Latitude = 35` the disc should ride a
southern arc matching the logged altitude/azimuth.

**Pass:** the disc moves along the model arc, position is stable (no
per-frame jitter), and the repositioned log line appears exactly once.

**Record (these are the open engineering questions, each answer is data):**
- Does the disc move at all? No visible change means vanilla `Sun::Update`
  rewrites the node after us: the next step is a post-`Sky::Update` write,
  the one remaining piece of RE.
- Jitter or flicker: both writers fighting within a frame.
- Do terrain and object hard shadows follow the disc, or stay on the vanilla
  sun path? Shadow-follow is the deep AELAS behavior; if only the disc moves,
  the directional light (`Sun::light`) needs its own write.
- Sun glare and lens flare alignment: only `sunBaseNode` is moved,
  `sunGlareNode` is not; misalignment is plausible and worth recording.
- The logged radius: thousands means the node's own orbit radius was
  preserved; exactly 5000 means the node sat at origin and the fallback fired.

## 4. TextureAutoConvert

**Setup:** `[Native] TextureAutoConvert = true`. Keep TextureLoadHook OFF
(it never fires once the sibling `.dds` exists, so testing both at once
proves neither).

**Init behavior:** a background scan starts at data-load and logs when done:
`TextureAutoConvert: scan done — N foreign, M converted, K already had .dds,
F failed`.

**Steps:** first run: note the counts. The 2026-07-16 offline dry-run against
this modlist found 163 foreign textures, 22 without a `.dds` sibling, so
expect converted near 22 on the first pass (the in-game number can differ
under the MO2 VFS view; record the actual). Restart: the second run must
convert 0 (idempotence). Dry-run from the console any time:
`cgf "SkyrimBridge.TextureScanNow" true` (counts only, writes nothing).

**Pass:** first run converts > 0 with failed = 0 (or each failure explained),
`.dds` files appear next to their sources, second run converts 0.

## 5. TextureLoadHook

**Setup:** `[Native] TextureLoadHook = true`, TextureAutoConvert OFF.

**Init log:** `TextureLoadHook: LooseFileLocation detours installed
(stream/async/info x2), cache at Data/SKSE\Plugins\SkyrimBridge\texcache`.

**Steps:** pick one loose texture you can see in-game (a clutter object's
diffuse). Convert it to PNG (any tool, or the `ConvertTexture` console
native), move the original `.dds` out of the tree, leave the PNG in its
place. Load the game near the object.

**Pass:** the object renders with its texture (not purple or missing); the
log shows `TextureLoadHook: <path> -> texcache/<name>`; the transcoded file
exists under `texcache/`; a second launch renders the same with no new
transcode line (cache hit). Restore the original `.dds` afterward.

## 6. CommandSurface

**Setup:** `[Native] CommandSurface = true`.

**Init logs (both):** `SkyrimBridge: external command channel active` and
`BridgeCommand: command channel active ('SkyrimBridge_Command', 5184 bytes)`.

**Steps:** with the game running, unpaused, in a loaded save, from any
terminal on the same machine:

```
python C:\dev\skyrimbridge\tools\sb_command_client.py ping
python C:\dev\skyrimbridge\tools\sb_command_client.py reflect.list 0
python C:\dev\skyrimbridge\tools\sb_command_client.py reflect.dump 0x10A241
python C:\dev\skyrimbridge\tools\sb_command_client.py bogus.verb
```

**Pass:** `ping` returns status 0, resultInt 1, `pong`. `reflect.list 0`
returns the same 11-schema listing as the in-game `EngineReflectList 0`.
`reflect.dump` prints the record INI and writes `dumps/<id>.ini`. The bogus
verb returns status -1 and the game keeps running. Requests sent while a menu
is open complete after unpausing (dispatch is one per frame).

**Record:** any dispatch AV line (`BridgeCommand: dispatch AV on verb ...`):
that is the SEH guard doing its job, but each one is a bug report.

## 7. Runtime model spawn (per-op, no toggle)

`SpawnModel` is opt-in per invocation (a console call is the gate), but it
MUTATES the save: it creates a dynamic Static form and places a persistent
reference. Test on a disposable save only.

**Steps:** put a small foreign mesh somewhere reachable, e.g.
`Data\test.obj` (any static single-shape OBJ or glTF/GLB). In-game:

```
cgf "SkyrimBridge.SpawnModel" "Data/test.obj"
```

**Expected log:** `SpawnModel: Data/test.obj -> meshes\SkyrimBridge\spawn\test.nif : placed 0x...`
(failure paths log `SpawnModel: ...` warnings naming the stage).

**Pass:** the mesh appears at the player's position, loaded by the engine's
own model loader. Geometry is the claim under test; the material is only as
good as the source carried, so an untextured OBJ rendering flat or purple is
NOT a failure. Click the ref in the console and `markfordelete` to clean up,
then discard the save.

**Record:** whether the shape appears at all (loader accepted our NIF), scale
and orientation sanity, and any crash on `markfordelete` or cell reload.

**Blender addon path (same feature, the QoL front end):** install
`tools/blender/skyrimbridge_push.py` (Edit > Preferences > Add-ons >
Install...), enable it, open the View3D sidebar (N) > SkyrimBridge. With the
game running (CommandSurface on, save loaded, unpaused) select a mesh and
click "Push to Game". Expected: the panel reports `<name>: placed 0x...`, the
same `ModelSpawn:` log line appears, and the mesh stands at the player.
Orientation is part of the claim: the addon exports Z-up specifically so the
model arrives upright; a mesh lying on its side is a FAIL (record it). Same
save rules as above: disposable save, `markfordelete` to clean up.

---

## After a PASS

Defaults stay OFF until the operator flips them. When one flips, change BOTH
copies of the config (the installed INI and `config/SkyrimBridge.ini` or
`config/Sky.ini` in the repo) in the same commit, and note the validation
date in the commit message. A FAIL goes back to OFF and the observations go
into the lane's honest nulls.
