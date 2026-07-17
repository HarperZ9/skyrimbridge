# The GPU rendering tier

Beyond measurement and write-back, SkyrimBridge carries a GPU rendering tier:
a compute infrastructure (shader compilation, dispatch, render-pass
orchestration, and a Hi-Z depth pyramid), a physically based atmosphere
renderer with Rayleigh and Mie scattering lookup tables, and raymarched
volumetric clouds. It is configured in `GPU.ini` and is off by default.
Volumetric clouds compile their shaders in the background after data load.

## Two modes

The tier detects its environment and runs in one of two modes.

**Proxy mode** requires the bundled `d3d11.dll` proxy. Load it either directly
as the game's `d3d11.dll` (place it next to `SkyrimSE.exe`), or chain it
through ENB by setting `ProxyLibrary` in the `[PROXY]` section of
`enblocal.ini`. In proxy mode the plugin gets mid-frame injection: depth and
G-buffer access, the Hi-Z pyramid builds, and the clouds render into the
scene.

**Legacy mode** is plain ENB with no proxy. The tier initializes and its
compute work runs at present time, but the mid-frame passes (clouds, Hi-Z)
stay dormant, because the injection points require the proxy. This mode is
useful for profiling and frame capture. It is not yet a visual path.

## The proxy

The proxy is a standalone D3D11 wrapper with no dependency on the game, SKSE,
or any package manager. It wraps the full D3D11 API so the plugin can inject
work mid-frame. The SKSE plugin detects it automatically at runtime; you do
not configure the link between them.

If you already run ENB (which is itself a `d3d11.dll` proxy), do not replace
ENB's proxy. Chain SkyrimBridge's proxy through ENB's `ProxyLibrary` setting
instead, so both load.

## Isolation hotkeys

While the game runs, these keys isolate parts of the tier for testing:

| Key | Toggles |
|---|---|
| F7 | Mid-frame dispatch |
| F8 | Compute and trackers |
| F9 | Render passes |
| F10 | Frame capture (600 frames) |
| F11 | GPU profiler |

## Status

The compute infrastructure, the atmosphere lookup tables, and the cloud
raymarch are built and compile in-game. The visual result in proxy mode is
validated in-game per [VALIDATION-PROTOCOL.md](VALIDATION-PROTOCOL.md). The
tier is off by default; enable it in `GPU.ini` only after reading what each
setting does.
