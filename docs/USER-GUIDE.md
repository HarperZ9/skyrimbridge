# SkyrimBridge user guide

This guide covers every feature, console command, config key, and command
channel verb. It is task oriented: find the thing you want to do, and follow
the steps. The technical specification, with exact field tables and byte
layouts, is in [SPEC-ENGINE-EXPOSURE.md](SPEC-ENGINE-EXPOSURE.md).

Everything that changes engine state ships turned off. You enable each
feature deliberately, in its config file or with a console command.

## Contents

1. [Running console commands](#running-console-commands)
2. [Live ENB parameters](#live-enb-parameters)
3. [The weather workshop](#the-weather-workshop)
4. [Runtime write-back](#runtime-write-back)
5. [Native ENB-plugin replacements](#native-enb-plugin-replacements)
6. [Editing engine records](#editing-engine-records)
7. [The texture pipeline](#the-texture-pipeline)
8. [The model and collision pipeline](#the-model-and-collision-pipeline)
9. [The command channel and external tools](#the-command-channel-and-external-tools)
10. [Diagnostics](#diagnostics)
11. [Console command index](#console-command-index)

## Running console commands

SkyrimBridge registers its functions as Papyrus natives. The simplest way to
call one is the console, using ConsoleUtilSSE's `cgf` (call global function):

```
cgf "SkyrimBridge.CellReport"
cgf "SkyrimBridge.EngineReflectDump" 0x10A232
```

Open the console with the tilde key, type the command, and press enter.
Numbers can be decimal or hexadecimal (`0x` prefix). You can also call the
same functions from your own Papyrus scripts.

Large text output (record dumps, reports) is written to files under
`Data/SKSE/Plugins/SkyrimBridge/dumps/`, and a short summary goes to the log
at `Documents/My Games/Skyrim Special Edition/SKSE/SkyrimBridge.log`.

## Live ENB parameters

The plugin publishes live game state to ENB every frame. Preset authors read
these values through the HLSL header `shaders/SkyrimBridge.fxh`. Include it in
your effect file after ENB's own parameter block, read the `SB_*` values, and
call `SB_Retain(uv)` once so the compiler does not strip them.

The full list of parameters and their meaning is in
[parameters.md](parameters.md). Configuration of which parameters compute and
how they map is in `WeatherParams.ini`.

## The weather workshop

The workshop edits the active weather record live and manages per-weather
presets. Presets live at
`Data/SKSE/Plugins/SkyrimBridge/WeatherPresets/<EditorID>.ini`.

**The loop**
1. Stand in the weather you want to tune.
2. `cgf "SkyrimBridge.CaptureWeather"` snapshots the active record: all 17
   color types across four times of day, fog, wind, sun glare, cloud layers,
   directional ambient, and the linked image space and volumetric lighting.
3. `cgf "SkyrimBridge.SaveWeatherPreset" "MyPreset"` writes it to a preset
   file you can open in any text editor.
4. Edit the preset file while the game runs. The change lands within a second.
   A preset overlays the live record, so a preset that carries only a
   `[Colors]` section leaves everything else untouched.
5. `cgf "SkyrimBridge.SetWeatherCompare" true` shows the original for an A/B
   comparison; `false` shows your edit.

**Automatic application**
When the game transitions to a weather that has a preset file, the preset
applies automatically. No console command is needed after the file exists.

**Other commands**
- `ApplyWeather` and `RevertWeather` push and undo the captured edit.
- `LoadWeatherPreset "name"` loads a named preset.
- `ForceWeatherByID <formid>` forces a weather; `ClearForcedWeather` releases.

## Runtime write-back

Write-back is the reverse of measurement. Each rule in `WriteBackConfig.ini`
maps a source onto an engine target through scale, offset, clamp, and temporal
smoothing. The source can be a measured game value, a fixed number, or a live
ENB shader parameter, so a float you author in `enbeffect.fx` becomes a
real-time engine control.

Targets include camera field of view, fog planes, sun and ambient light
color, actor values, timescale, and game hour. Every target is typed engine
access, never a raw address. Every rule ships disabled; you enable the ones
you want in the config file.

## Native ENB-plugin replacements

These reimplement the third-party ENB plugins that presets used to depend on,
using the plugin's own flat INI files. Toggle them in the `[Native]` section
of `SkyrimBridge.ini`. All the ones that change the look ship off.

- **WeatherRouting**: per-worldspace ENB weather-list switching. Reads
  `WeatherRouting.ini` (an example is shipped).
- **Sky**: a celestial lighting model with an orbital sun and moon and dynamic
  ambient. The model loads from `Sky.ini`. The live ambient write is gated by
  `[Sky] Enable`, and the orbital sun by `[Orbit] MoveSun`, both off by
  default.
- **EnbLightInventoryFix**: stops ENB particle lights on inventory 3D item
  previews from leaking into the world. AE only. Ships off.
- **EngineFixes**: a recovered engine patch, validated before it writes.

The editor-ID cache that names forms for the weather workshop is always on.

## Editing engine records

EngineReflect reads a game record to a plain text file, lets you edit it, and
writes it back. It covers 14 record types and 827 named fields.

**The loop**
1. `cgf "SkyrimBridge.EngineReflectList" 0` lists the registered schemas.
   Pass a form ID instead of `0` to list that record's fields.
2. `cgf "SkyrimBridge.EngineReflectDump" 0x10A232` writes the record to
   `dumps/<formid>.ini`.
3. Edit the file.
4. `cgf "SkyrimBridge.EngineReflectApply" 0x10A232` reads your edited file
   back into the running record.
5. `cgf "SkyrimBridge.EngineReflectVerify" 0x10A232` reads the record, writes
   it out, parses it back, and confirms the round trip. It returns the field
   count on success and 0 on failure.

The covered record types are image space, volumetric lighting, lighting
template, weather (the full 487-field record), climate, region, light, water,
effect shader, image-space modifier, worldspace, grass, land texture, and
tree. The field tables are in [SPEC-ENGINE-EXPOSURE.md](SPEC-ENGINE-EXPOSURE.md).

**Regions**: `RegionDump <regionid>` writes a region's subrecords (weather,
sound, map, land). `RegionSetWeatherChance <region> <weather> <chance>` edits
a weather-list entry; it changes existing entries only.

**Which mod won a record**: `FormChain 0x<formid>` prints the plugin override
chain for any form, oldest first, winner last. This works on records that have
no world reference, like weathers and image spaces.

## The texture pipeline

SkyrimBridge decodes PNG, TGA, and BMP, and reads and writes DDS in BC1, BC3,
BC7, BC4, and BC5. It also reads the DX10 header formats.

**Inspect a file**
```
cgf "SkyrimBridge.TextureInfo" "textures/foo.dds"
```
Prints the format, dimensions, and mip count without a full decode.

**Convert one file**
```
cgf "SkyrimBridge.ConvertTexture" "in.png" "out.dds"
cgf "SkyrimBridge.ConvertTextureFmt" "in.png" "out.dds" "BC7"
```
The format can be RGBA8, BC1, BC3, BC7, BC4, or BC5. BC1 is opaque color, BC3
carries alpha, BC7 is high quality color plus alpha, BC4 is a single channel
(masks and heightmaps), and BC5 is two channels (normal maps).

**Foliage mipmaps**
```
cgf "SkyrimBridge.ConvertTextureFoliage" "leaf.png" "leaf.dds" "BC3" 128
```
Rescales each mipmap's alpha so the fraction of texels passing the alpha test
stays constant. This stops alpha-tested foliage from thinning out and
vanishing at distance. The last number is the alpha-test threshold (128 is the
usual cutoff).

**Automatic integration** (both in `SkyrimBridge.ini`, both off)
- `TextureAutoConvert`: at load, transcodes every `textures\*.png/.tga/.bmp`
  without a DDS sibling to a DDS next to it. Additive; it never overwrites.
- `TextureLoadHook`: serves a missing `textures\*.dds` from a foreign sibling
  by transcoding it once and streaming it through the engine's own loader.

`MaterialHash "snow"` resolves a Havok material name to its hash, for the
collision features below.

## The model and collision pipeline

Convert OBJ, glTF, and GLB static meshes to Skyrim SE NIF, and generate
collision for them.

**Convert a static mesh**
```
cgf "SkyrimBridge.ConvertModel" "in.obj" "out.nif"
```
Emits a NIF the engine loads natively. Static single-shape meshes; the first
group or primitive is used.

**Trees**
```
cgf "SkyrimBridge.ConvertModelTree" "in.obj" "out.nif"
```
Paints procedural wind-sway weights into the vertex colors (stiff at the
trunk base, rising toward the canopy) and sets the tree animation shader, so a
converted tree sways in wind.

**Full options, including collision**
```
cgf "SkyrimBridge.ConvertModelEx" "in.obj" "out.nif" false true 8 "stone"
```
The arguments are: tree mode (true or false), collision (true or false),
collision piece count, and Havok material name. A piece count of 1 is a single
convex hull; 2 or more runs a convex decomposition that approximates concave
shapes with a list of convex pieces. The material sets footstep sounds and
impact effects. Names include snow, stone, wood, ice, dirt, grass, gravel,
sand, and metal.

Convex collision cannot be truly concave (an archway's opening fills in). For
exact concave collision, the plugin builds the game's compressed-mesh geometry
and you finalize the bounding-volume tree in NifSkope; see
[SPEC-ENGINE-EXPOSURE.md](SPEC-ENGINE-EXPOSURE.md).

**Place a mesh in the running game**
```
cgf "SkyrimBridge.SpawnModel" "Data/test.obj"
```
Converts the mesh and places it at the player through the engine's own model
loader. This changes the save (it creates a form and a reference), so use a
save you can discard while testing. Remove the reference with the console
(`markfordelete`).

## The command channel and external tools

The command channel lets an external tool on the same computer drive the
engine surface over shared memory, without the console. It is off by default.
Turn it on with `[Native] CommandSurface = true` in `SkyrimBridge.ini`, then
launch and load a save.

**The command-line client**
```
python tools/sb_command_client.py ping
python tools/sb_command_client.py reflect.dump 0x10A232
python tools/sb_command_client.py texture.convert in.png out.dds --int 2
```
Run it with no arguments to see the verb list.

**The Blender add-on**
`tools/blender/skyrimbridge_push.py` adds a "Push to Game" button to Blender.
It exports the selected mesh and spawns it in the running game in one click.
Install it through Blender's add-on preferences. The game must be running with
the command channel on. Checkboxes select tree mode and collision.

**The modlist smoke tester**
`tools/sb_smoke_tour.py` teleports the game through a list of cells and
collects a performance and script report at each stop, so you can compare a
modlist before and after a change. It reports a pass, a per-cell failure, or a
crash at the exact cell. This changes the save (it teleports), so use a spare
save.

## Diagnostics

**Cell performance census**
```
cgf "SkyrimBridge.CellReport"
```
Reports the current cell: reference counts by type, and every shadow-casting
light ranked by distance with the plugin that placed it. The engine renders
only a few shadow lights at once and each is expensive, so this list is the
usual first stop when a cell drops frames. Full text goes to
`dumps/cellreport.txt`.

**Papyrus VM monitor**
```
cgf "SkyrimBridge.ScriptReport"
```
Reports the live script engine: whether it is overstressed, the
function-message queue depth (the direct script-lag signal), running and
frozen stack counts, which script classes are running right now, and the
per-script instance census. It runs on the next frame and writes
`dumps/scriptreport.txt`. This is a live equivalent of loading a save in a
save cleaner, without the save.

## Console command index

State readers and the weather workshop:

| Command | Effect |
|---|---|
| `IsActive`, `GetFloat`, `GetGameHour`, `GetWeatherFormID`, `IsInterior` | State reads |
| `CaptureWeather`, `ApplyWeather`, `RevertWeather` | Capture and edit the active weather |
| `SaveWeatherPreset`, `LoadWeatherPreset` | Preset files |
| `SetWeatherCompare`, `ForceWeatherByID`, `ClearForcedWeather` | Compare and force |

Record editing:

| Command | Effect |
|---|---|
| `EngineReflectList` | List schemas or a record's fields |
| `EngineReflectDump`, `EngineReflectApply`, `EngineReflectVerify` | Dump, apply, verify a record |
| `RegionDump`, `RegionSetWeatherChance`, `RegionApply` | Region subrecords |
| `FormChain` | The plugin override chain of any form |

Textures:

| Command | Effect |
|---|---|
| `TextureInfo` | Header-only format and dimensions |
| `ConvertTexture`, `ConvertTextureFmt`, `ConvertTextureFoliage` | Convert a texture |
| `TextureScanNow` | Run the texture tree scan |
| `MaterialHash` | Resolve a Havok material name |

Models and collision:

| Command | Effect |
|---|---|
| `ConvertModel`, `ConvertModelTree`, `ConvertModelEx` | Convert a mesh to NIF |
| `SpawnModel` | Convert and place at the player (changes the save) |

Diagnostics:

| Command | Effect |
|---|---|
| `CellReport` | Cell performance census |
| `ScriptReport` | Live Papyrus VM monitor |

The full argument signatures are in
[SPEC-ENGINE-EXPOSURE.md](SPEC-ENGINE-EXPOSURE.md).
