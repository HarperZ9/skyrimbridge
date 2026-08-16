# SkyrimBridge in-game validation (gated features)

Every public feature below ships OFF. This is the acceptance procedure for the
features that change engine state: enable one at a time, run the steps, record
the result, and only then decide whether to keep it on. The offline receipts
(the `tests/` harnesses) cover the math and codecs; this covers the live engine
behavior they cannot.

Public release note: the Kitsuune-derived native replacement suite is excluded
from public binaries and public archives. The private-only scenarios for
EnbLightInventoryFix, dynamic night ambient, and orbital sun repositioning are
kept here as provenance/engineering notes, not as public package claims. Public
archives do not ship `Sky.ini` or `WeatherRouting.example.ini`.

**Paths**
- Config: public settings live under `Data/SKSE/Plugins/SkyrimBridge/` in your
  Skyrim `Data` folder or your mod manager's view of it. Private native-suite
  builds may add `Sky.ini`; public packages do not.
- Log: `Documents\My Games\Skyrim Special Edition\SKSE\SkyrimBridge.log`.
- Configs load once at data load: restart the game after every toggle change.

**General loop:** flip ONE toggle, start the game, grep the log for the init
line, run the steps, record PASS/FAIL plus the observations listed, flip the
toggle back OFF unless you are keeping the feature. Test features separately;
two of them (the texture pair) actively mask each other.

| # | Feature | Toggle | Init log line to grep |
|---|---|---|---|
| 1 | EnbLightInventoryFix (private-only) | `[Native] EnbLightInventoryFix` | `EnbLightInventoryFix: installed inventory-3D light hooks` |
| 2 | Dynamic night ambient (private-only) | `Sky.ini [Sky] Enable` | `SkyLighting: loaded Sky.ini (masterEnable=true` |
| 3 | Orbital sun repositioning (private-only) | `[Sky] Enable` + `[Orbit] MoveSun` | `SkyLighting: MoveSun armed` |
| 4 | TextureAutoConvert | `[Native] TextureAutoConvert` | `TextureAutoConvert: scan done` |
| 5 | TextureLoadHook | `[Native] TextureLoadHook` | `TextureLoadHook: LooseFileLocation detours installed` |
| 6 | CommandSurface | `[Native] CommandSurface` | `BridgeCommand: command channel active` |

---

## 1. EnbLightInventoryFix (private native-suite validation only)

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

## 2. Dynamic night ambient (`[Sky] Enable`, private native-suite validation only)

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

## 3. Orbital sun repositioning (`[Orbit] MoveSun`, private native-suite validation only)

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
`TextureAutoConvert: scan done: N foreign, M converted, K already had .dds,
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
python tools\sb_command_client.py ping
python tools\sb_command_client.py reflect.list 0
python tools\sb_command_client.py reflect.dump 0x10A241
python tools\sb_command_client.py bogus.verb
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

**Tree mode (F16):** repeat the spawn with tree conversion, either
`cgf "SkyrimBridge.ConvertModelTree" "Data/test.obj" "Data/meshes/SkyrimBridge/spawn/test.nif"`
then `SpawnModel` on that .nif, or tick "Push as tree" in the Blender addon.
The decisive observation, genuinely unknown until you look: does the
STAT-placed mesh sway in the wind? The shader carries Tree_Anim and the
vertex colors carry the weights (trunk stiff, canopy loose), exactly as real
animated trees do, but real trees are placed as TREE forms. Record: sway or
static; if it sways, whether the base stays planted (weight ramp working);
any shading oddity (the grayscale weights also feed vertex-color shading).
A static result is DATA, not failure: it tells us TREE-form placement is the
missing piece, which becomes its own lane.

**Collision (F18 follow-on):** spawn with collision, either
`cgf "SkyrimBridge.ConvertModelEx" "Data/test.obj" "Data/meshes/SkyrimBridge/spawn/test.nif" false true`
then `SpawnModel` on the .nif, or tick "With collision" in the addon. The
acceptance test is physical: WALK into the spawned mesh. Pass: you collide
with a hull that hugs the visible shape at the right size (arrows and
dropped items also land on it). FAIL modes worth recording separately: no
collision at all (chain rejected by the engine), collision far too large or
small (scale wrong; the investigation found real ported assets broken
exactly this way), or collision offset from the visible mesh. Remember a
hull is convex: walking "into" a concave opening of the model stopping you
early is expected behavior, not a failure.

**Decomposed (concave-approximating) collision:** spawn a concave prop
(an archway, a chair, an L-bracket) with a piece count, e.g.
`cgf "SkyrimBridge.ConvertModelEx" "Data/arch.obj" "Data/meshes/SkyrimBridge/spawn/arch.nif" false true 8`
then `SpawnModel` on the .nif (or model.spawn with argInt = 2 | (8<<8) over
the channel). Walk THROUGH the concave opening: with a single hull it is
blocked (the notch is filled); with decomposition you should pass through
while still colliding with the solid parts. Record: does the opening clear,
do the solid parts still stop you, any seam you can slip through (a gap
between pieces). More pieces = tighter concavity, more physics cost.

**Blender addon path (same feature, the QoL front end):** install
`tools/blender/skyrimbridge_push.py` (Edit > Preferences > Add-ons >
Install...), enable it, open the View3D sidebar (N) > SkyrimBridge. With the
game running (CommandSurface on, save loaded, unpaused) select a mesh and
click "Push to Game". Expected: the panel reports `<name>: placed 0x...`, the
same `ModelSpawn:` log line appears, and the mesh stands at the player.
Orientation is part of the claim: the addon exports Z-up specifically so the
model arrives upright; a mesh lying on its side is a FAIL (record it). Same
save rules as above: disposable save, `markfordelete` to clean up.

## 8. Cell performance census (per-op, read-only)

No toggle and no writes; safe to run anywhere in-game.

**Steps:** stand in a cell you know is heavy (a cluttered ELFX interior is
ideal). Run `cgf "SkyrimBridge.CellReport"` (or `cell.report` over the
channel). Open `SkyrimBridge/dumps/cellreport.txt`.

**Pass:** the counts are plausible against the console's own knowledge
(click a listed shadow-light ref: `prid <formid>` then `getpos x` places
it; the winning plugin matches what More Informative Console shows for the
same ref); an exterior and an interior both produce reports; the
shadow-light list is nearest-first.

**The payoff to record:** disable the nearest listed shadow lights
(`prid <formid>`, `disable`) and note the FPS change. If the report's
nearest shadow lights are the frame cost, the census just replaced the
manual hunt.

## 9. Papyrus VM monitor (per-op, read-only)

No toggle, no writes. Two surfaces with different timing: the console
native queues the report for the NEXT frame (`cgf "SkyrimBridge.ScriptReport"`,
then read `dumps/scriptreport.txt`); the channel verb answers synchronously
(`python tools\sb_command_client.py script.report`).

**Steps:** run it on a fresh save in an empty interior, then on your oldest
long-played save in a busy exterior. Compare.

**Pass:** plausible numbers in both (a fresh save shows few running stacks
and a small instance census; the old save shows more), the top instance
classes look like your installed script mods, and repeated runs a few
seconds apart move (runningStacks fluctuates, the census stays near-stable).
No hitch, stutter, or deadlock when invoked repeatedly, including during
combat and scripted scenes (the lock-hold windows are short; this is the
stress case worth trying deliberately).

**The payoff to record:** waitingFunctionMessages under load. Near zero on
a healthy setup; sustained growth or a set overstressed flag while stutter
is felt is the live confirmation of script lag, and the executing-class
list names the suspects while it happens.

## 10. Modlist smoke tour (per-op; teleports, disposable save only)

The driver MUTATES game state (it teleports the player); everything it
reads is read-only. Use a save you will discard.

**Steps:** with the game running (CommandSurface on, unpaused, disposable
save), pick 4-6 `coc` targets you know work in your modlist (test each in
the console once), then:

```
python tools\sb_smoke_tour.py --cells Riverwood WhiterunBanneredMare Solitude
```

**Pass:** every stop lands (cell id changes), the per-stop census and VM
summaries in `skyrimbridge-tour.txt` look plausible, load times are sane,
and exit code is 0. Deliberately add a bogus cell name and confirm the stop
reports FAILED while the tour continues (exit 2). If you have a known-crash
cell, run it last and confirm the CRASH verdict names it (exit 3).

**The payoff to record:** run the same tour before and after a modlist
change and diff the two reports. Differences in shadow-light counts, ref
counts, VM queue depth, and load times per stop are the regression signal
this tool exists to surface.

## 12. Collision material (snow footsteps), per-op

Depends on section 7/11 (a spawned mesh with collision). Convert or spawn
with a material and confirm the engine reacts to it.

**Steps:** spawn a walkable mesh with collision and a snow material. Over
the channel: `python tools\sb_command_client.py model.spawn Data/test.obj --int 131330`
(argInt = collision bit 2 | 1 piece <<8 = 0x102 | snow index 2 <<16 =
0x20000, i.e. 131330). Or in-game after converting a file:
`cgf "SkyrimBridge.ConvertModelEx" "Data/x.obj" "Data/x.nif" false true 4 "snow"`.
Sanity-check the hash first: `cgf "SkyrimBridge.MaterialHash" "snow"` should
return 398949039 (0x17C77AAF).

**Pass:** walking on the spawned mesh produces SNOW footstep sounds (not the
wood default), and weapon/arrow hits show snow impact effects. Compare
against a wood-default spawn to hear the difference.

**Honest null to remember:** this changes the sound and impact the engine
reacts to. Visual footprint depressions in snow are a separate system (the
snow shader / a footprints mod), not the collision material, so their
absence is not a failure of this feature.

## 13. Exact mesh collision (finalize workflow), file output

This produces a NIF that is NOT game-ready until you finalize the MOPP. Do
not spawn it live before finalizing (it would crash).

**Steps:** convert a mesh with exact collision and a material. In-game:
`cgf "SkyrimBridge.ConvertModelMeshCollision" "Data/x.obj" "Data/x.nif" "stone"`,
or over the channel:
`python tools\sb_command_client.py model.meshcollision Data/x.obj Data/x.nif`
(add `--int 65536` for stone = material index 1 << 16). Then open `x.nif`
in NifSkope, run Spells -> Havok -> "Update MOPP Code", and save.

**Pass (offline first):** NifSkope loads `x.nif` without error, shows the
`bhkCompressedMeshShape` chain, and "Update MOPP Code" completes and fills a
non-empty MOPP (the block grows). **Pass (in-game):** place the finalized
NIF as a static and walk on it. Unlike the convex/decomposed collision, a
concave shape's true openings are walkable and the solid faces stop you
(the exact-geometry payoff a hull list only approximates). Footstep sounds
match the chosen material.

**Honest bound:** if NifSkope's "Update MOPP Code" errors on the emitted
chain, that is the finding to record (the wrapper layout or an unknown field
needs a fix); the geometry block itself is corpus-proven byte-exact.

---

## After a PASS

Defaults stay OFF until you flip them. When a public feature flips, change both
copies of the config (the installed INI and the matching public config in this
repository) in the same commit, and note the validation date in the commit
message. Private native-suite builds have their own excluded config files and
release gate. A FAIL goes back to OFF and the observations go into the lane's
honest nulls.
