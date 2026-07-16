# SkyrimBridge

Live game state for ENB shaders. SkyrimBridge publishes the engine's real-time
data as ENB parameters every frame, so a preset can react not only to weather
and time of day, but to what the player is actually doing.

Weather colors, sun and moon, fog, and camera are what other bridges give you.
SkyrimBridge adds the rest: combat intensity, equipment, actor values and
skills, damage and vision effects, interior state, and nearby lights. If you can
name a piece of game state, a shader can read it.

## What you can build with it

- Weather-reactive grading that matches the sky's real colors, interpolated
  across the day the same way the engine does it.
- Combat-reactive post: a vignette that tightens as a fight escalates, screen
  response tied to damage taken.
- Equipment- and skill-aware looks: effects that shift with what is worn or
  which skills are in play.
- Interior-aware exposure that knows it is indoors without guessing from depth.
- External-app access to the same live data through a shared-memory channel.

## Three ways in

1. **The plugin** publishes the data. Drop it in as an SKSE plugin.
2. **The HLSL API** (`shaders/SkyrimBridge.fxh`) is the stable contract for
   shader authors. Include it after ENB's built-in parameter block, read the
   `SB_*` values, and call `SB_Retain(uv)` once so nothing dead-strips.
3. **The shared-memory bridge** exposes the same frame data to external tools.
   A drop-in `enbParmLink` compatibility layer is included for existing setups.

See [docs/parameters.md](docs/parameters.md) for the full parameter contract.

## What is in this repository today

This repository is the home of the public contract: the HLSL API headers in
`shaders/` and the parameter documentation in `docs/`. Preset authors can build
against these now; the `SB_*` names and packing are the stable surface.

The plugin's C++ source is being extracted from the larger codebase it grew up
in and will land here as a standalone CommonLibSSE-NG build. There is no
release from this repository yet; the first release will ship the built plugin
alongside the extracted source. The API contract does not change when the
source arrives.

## Requirements

- Skyrim SE or AE with SKSE and Address Library.
- ENBSeries, for the shader-facing side.

## License

MIT. Copyright 2026 Zain Dana Harper. Use it, ship presets on it, build tools
against the shared-memory channel.
