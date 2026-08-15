# Nexus mod page content

Everything needed to fill the Nexus upload form. The description below is
BBCode, ready to paste into the mod page description field.

## Form fields

- **Name**: SkyrimBridge
- **Summary** (one line): Opens the Skyrim engine to shaders, tools, and
  creators: live game state for ENB, live record editing, foreign texture and
  model import, collision generation, and a tool automation channel.
- **Category**: Modders Resources and Tutorials (or Utilities / Patches)
- **Version**: 3.0.0

## Requirements (add these on the mod page)

- SKSE64 (or SKSEVR)
- Address Library for SKSE Plugins
- ENBSeries (for the shader-facing features only)
- ConsoleUtilSSE (optional, for the console functions)

## Description (BBCode)

[size=5]SkyrimBridge[/size]

An SKSE plugin that opens the Skyrim engine to shaders, tools, and creators.
It publishes live game state to ENB every frame, edits engine records while
the game runs, imports foreign texture and model files into formats the game
loads natively, generates collision, and exposes a diagnostics and automation
channel that external tools can drive.

Built on CommonLibSSE-NG with version-independent addressing, so one build
runs on Special Edition, Anniversary Edition, and VR. Every write to the
running engine is typed access through the game's own structures, never a raw
memory poke, and the features that change engine state ship turned off until
you enable them.

[size=4]What it does[/size]

SkyrimBridge is several tools in one plugin. Use only the parts you need.

[b]For ENB preset authors[/b]
[list]
[*]Publishes over 20 domains of live game state as ENB parameters every frame:
weather colors, sun and moon, fog, camera, combat intensity, equipment, actor
values and skills, damage and vision effects, interior state, and nearby
lights. A stable HLSL header is the contract preset authors read.
[*]A live weather workshop captures the active weather record, applies edits
back to the running game, and hot-reloads per-weather presets while you play.
No editor, no restart.
[*]Runtime write-back maps a measured value, a fixed number, or a live ENB
shader parameter onto an engine target, so a slider in the ENB editor can
drive the engine in real time.
[*]A GPU tier with a physically based atmosphere renderer and volumetric
clouds, for setups that chain a D3D11 proxy.
[/list]

[b]For record and worldspace editors[/b]
[list]
[*]Read any of 14 record types to a plain text file, edit it, and write it
back with a round-trip check: image spaces, weathers (the full 487-field
record), climates, lighting templates, water, effect shaders, lights,
regions, worldspaces, grass, land textures, and trees. 827 named fields.
[*]A recovered engine patch for the AE spin-lock, validated against the live
executable before each write.
[/list]

[size=4]Works alongside the plugins you already run[/size]

SkyrimBridge detects KreatE, ELIF, EVLaS and AELAS, Native EditorID Fix, and
ENB Worldspace Weatherlists at startup. When one is loaded, SkyrimBridge stands
down from that feature and logs that it deferred, so nothing here competes with
them and you are never asked to choose.

Those plugins are by [b]Kitsuune (LonelyKitsuune)[/b]. SkyrimBridge does contain
its own implementations of that functionality, developed by reverse-engineering
their compiled binaries, and those are [b]not included in this download[/b].
Redistributing them needs Kitsuune's permission, which has not been granted, so
the released build is compiled with that suite absent from the binary rather
than merely switched off. The full attribution and provenance record ships in
the archive as [font=Courier New]Docs/CREDITS.md[/font].

What that means in practice: install Kitsuune's plugins for those features. They
are the originals, they are maintained, and SkyrimBridge is built to sit beside
them.

[b]For texture and model creators[/b]
[list]
[*]Decode PNG, TGA, and BMP, and read and write DDS in BC1, BC3, BC7, BC4, and
BC5. Transcode a whole texture tree at load, or convert a missing DDS in
flight and serve it through the engine's own file stream. Alpha-coverage
mipmaps keep foliage from thinning out at distance.
[*]Convert OBJ, glTF, and GLB static meshes to Skyrim SE NIF, place them in
the running game, and paint procedural tree wind so a converted tree sways.
[*]Generate collision: a convex hull, a convex decomposition for concave
shapes, and exact mesh collision, with selectable Havok materials so a snow
prop sounds like snow underfoot.
[*]A Blender add-on that exports the selected mesh and drops it into the
running game with one click.
[/list]

[b]For power users and troubleshooters[/b]
[list]
[*]A cell performance census names the shadow-casting lights in your current
cell, ranked by distance, with the plugin that placed each one. The manual
hunt for a frame-rate sink becomes one command.
[*]A live Papyrus virtual machine monitor reports script load while the game
runs: the queue depth, running and frozen stacks, and which scripts are
executing right now.
[*]A command channel over shared memory lets an external tool drive the engine
surface without the console.
[/list]

[size=4]Requirements[/size]
[list]
[*]SKSE64 (or SKSEVR)
[*]Address Library for SKSE Plugins
[*]ENBSeries, for the shader-facing features only
[*]ConsoleUtilSSE, optional, for calling the console functions
[/list]

The record editing, texture, model, collision, and diagnostics features do
not require ENB.

[size=4]Installation[/size]

Install it like any SKSE plugin. A mod manager is recommended.
[list=1]
[*]Download and install the main file in your mod manager, and enable it.
[*]Make sure SKSE, Address Library, and (for the shader features) ENBSeries
are installed.
[*]Launch through SKSE.
[/list]

After a launch, check for SkyrimBridge.log in Documents/My Games/Skyrim
Special Edition/SKSE/ to confirm it loaded.

Everything that writes to the running engine ships disabled. You turn on each
feature in its config file, under Data/SKSE/Plugins/SkyrimBridge/.

[size=4]Documentation[/size]

The full feature reference, with every console command, config key, and
command-channel verb, is in the docs folder and on the source repository. The
source, build instructions, and the offline test harnesses are on GitHub.

[size=4]Scope, stated plainly[/size]

The offline gates are thorough: 18 validation harnesses cover the texture and
model codecs, collision generation and material mapping, compressed mesh, tree
wind, and deterministic packaging. The release archive is byte-reproducible and
ships a content-hash manifest with a SHA-256 sidecar.

What those gates do not cover is your load order. Broad in-game validation
across SE, AE, and ENB 0.504 is ahead of this release, not behind it. The
plugin is written to fail quietly rather than loudly, so a tracker that throws
is disabled and retried instead of taking the frame down, and every published
value defaults to zero when the plugin is idle. Report anything that misbehaves
and include your log.

[size=4]Source and license[/size]

MIT licensed. Source: https://github.com/HarperZ9/skyrimbridge

## Permissions (set these on the mod page to open it up)

- Users can modify this file: yes
- Users can convert this file to work with other games: yes
- Users can use assets from this file without permission as long as they
  credit you: yes
- Others can use assets in this file with credit, without permission: yes
- You are allowed to upload this file to other sites: yes (with credit)

This mirrors the MIT license the source ships under. In the "Permissions and
credits" description, state: this mod is MIT licensed; use it, modify it,
convert it, and ship presets and tools on it, with credit.
