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

## Runtime write-back

Measurement is one direction; `WriteBackConfig.ini` is the other. Each rule
maps a source (a measured `SB_*` field, a fixed value, or a **live ENB shader
parameter**) through scale, offset, clamp, and temporal smoothing onto an
engine target: camera FOV, fog planes, sun and ambient light color, actor
values, timescale, game hour. Author a float in `enbeffect.fx`, name it in a
rule, and ENB's editor becomes a real-time engine control. All targets are
typed engine access, no raw addresses; every rule ships disabled until you
enable it.

## Requirements

- Skyrim SE or AE with SKSE and Address Library.
- ENBSeries, for the shader-facing side.

## Build

A standard CommonLibSSE-NG plugin build through vcpkg:

```
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release
```

The `commonlibsse-ng` dependency resolves through the vcpkg manifest in this
repository. The output is `SkyrimBridge.dll`, an SKSE plugin; install it to
`Data/SKSE/Plugins/` together with `config/WeatherParams.ini` under
`Data/SKSE/Plugins/SkyrimBridge/`.

## Source layout

| Path | Contents |
|---|---|
| `src/core/` | The measurement trackers (20 domains), the ENB SDK interface, compatibility detection, and the Papyrus bridge |
| `src/` | Weather parameter computation, the shared-memory channel (`SkyrimBridge_GameState`), and the enbParmLink compatibility layer |
| `shaders/` | The stable HLSL API for preset authors |
| `docs/` | The parameter contract |

There is no packaged release yet; the first release ships the built plugin.
In-game validation of this standalone build is still ahead of it.

## License

MIT. Copyright 2026 Zain Dana Harper. Use it, ship presets on it, build tools
against the shared-memory channel.
