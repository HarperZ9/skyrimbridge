# Changelog

## 3.0.0

First public release. One SKSE plugin, built on CommonLibSSE-NG with
version-independent addressing, that runs on Skyrim Special Edition,
Anniversary Edition, and VR.

**Live ENB parameters**
- Over 20 domains of live game state published to ENB every frame, with a
  stable HLSL header for preset authors.
- Live weather workshop: capture the active weather record, edit it in a text
  file, hot-reload while the game runs, and manage per-weather presets.
- Runtime write-back from measured values or live ENB shader parameters onto
  engine targets (camera, fog, light color, actor values, timescale, hour).
- A GPU rendering tier with a physically based atmosphere and volumetric
  clouds, for setups that chain a D3D11 proxy.

**Native ENB-plugin replacements**
- Per-worldspace weather routing, a celestial lighting model with an optional
  orbital sun, an inventory-light fix, and a recovered engine patch. Own flat
  INI files, no external loader.

**Engine record editing**
- EngineReflect: read, edit, and write back 14 record types across 827 named
  fields, with a round-trip verifier. Weathers, image spaces, climates,
  water, effect shaders, grass, land textures, trees, and more.
- RegionWalker for region subrecords; FormChain to see which plugin won a
  record.

**Texture pipeline**
- Decode PNG, TGA, and BMP; read and write DDS in BC1, BC3, BC7, BC4, and BC5,
  plus the DX10 header formats.
- Whole-tree auto-conversion at load, in-flight conversion through the engine
  loader, and alpha-coverage-preserving mipmaps for foliage.

**Model and collision pipeline**
- Convert OBJ, glTF, and GLB static meshes to Skyrim SE NIF.
- Procedural tree wind painting so a converted tree sways.
- Collision generation: convex hull, convex decomposition for concave shapes,
  and exact mesh collision from the reversed compressed-mesh format, with
  selectable Havok materials for correct footstep sounds.

**Tools and diagnostics**
- A command channel over shared memory, with a command-line client, a Blender
  push-to-game add-on, and a modlist smoke-tour tester.
- A cell performance census that names the shadow-casting lights costing you
  frames, and a live Papyrus VM monitor for script-lag diagnosis.

All features that change engine state ship off. The file-format work is
validated offline against real game assets with 17 re-runnable test
harnesses.
