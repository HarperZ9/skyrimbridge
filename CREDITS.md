# Credits and provenance

SkyrimBridge is MIT licensed (`LICENSE`). Vendored third-party components are
listed in `THIRD_PARTY_NOTICES.md`. This file covers a different obligation:
work by other authors that SkyrimBridge reverse-engineered and reimplemented.
Read it before any public release.

## Kitsuune (LonelyKitsuune) — original author of the reversed plugins

SkyrimBridge has a private, explicitly opted-in native ENB-plugin replacement
suite that reimplements functionality of plugins authored by
**Kitsuune (LonelyKitsuune)**. Public packages exclude that suite from the
compiled binary; this section records the provenance of the private
implementation and the interoperability behavior that remains in the public
build. Each private unit exists because it reproduces the behavior of one of
their plugins:

| Kitsuune plugin | where | SkyrimBridge unit |
| --- | --- | --- |
| KreatE (`.kfg` profile loader: ImageSpaces, Weathers, LightingTemplates, VolumetricLighting, ShaderParticleGeometries) | Nexus 83757 | `KreateProfile`, `KreateRecords` |
| AELAS (celestial / orbital lighting, successor to EVLaS) | kitsuune.pages.dev | `SkyLighting` |
| EVLaS (Enhanced Volumetric Lighting and Shadows) | Nexus 63725 | `SkyLighting` |
| ELIF (ENB Light Inventory Fix) | Nexus 66411 | `EnbLightInventoryFix` |
| Native EditorID Fix | Nexus 85260 | `EditorIDCache` |
| ENB Worldspace Weatherlists (hosted by KiLoader, Nexus 99404) | Nexus 101697 | `WorldspaceWeatherlist` |

## Interoperability: the original always wins

SkyrimBridge does not displace these plugins. `CompatDetect` probes for each at
`kPostLoad` and again at `kDataLoaded`. In private opt-in builds, when the
original is loaded the matching SkyrimBridge unit stands down and logs that it
deferred. In public builds, the private replacement units are absent, so users
who want KreatE, ELIF, EVLaS/AELAS, Native EditorID Fix, or ENB Worldspace
Weatherlists behavior should install the original plugins.

The private replacements exist for operator-only builds and not to compete with
the originals. That is a design constraint, not a courtesy: do not add a code
path that overrides a detected original.

Kitsuune is credited separately, as LonelyKitsuune / Skratzer, for legacy "ENB of
the Elders" preset shader techniques: Dynamic Gaussian Bloom 2.2, ADOF, sunsprite,
BokehMax, FNENB, KiSharp, and the skin-mask convention. Those credits live in the
preset shader headers and must stay there.

## This is reverse-engineering, not clean-room

Be accurate about this. The replacements were derived by **reverse-engineering
Kitsuune's compiled plugin binaries**: disassembly plus a behavioral spec. The
same author reversed and reimplemented, so the strict clean-room definition does
not apply and must not be claimed anywhere in this repository, its docs, or any
public surface.

What exists in the private opt-in suite is original SkyrimBridge code
reproducing observed behavior. Public packages do not ship those compiled
objects. No Kitsuune source, binary, decompiled output, or `.kfg`/`.cfg` config
is redistributed. The RE notes and binaries are held privately outside this
repository and are excluded from every package.

"Clean-room" is a specific provenance claim. This is not it.

## Their stated terms, checked 2026-08-14

All five Nexus mods above carry the identical permission block, verbatim:

- Other user's assets: all assets in the file belong to the author, or are from
  free-to-use modder's resources.
- Upload permission: you are **not allowed** to upload the file to other sites
  under any circumstances.
- Modification permission: you **must get permission** before modifying their
  files to improve them.
- Conversion permission: you are **not allowed** to convert the file to work on
  other games.
- Asset use permission: you **must get permission** before using any of the
  assets in the file.
- Asset use in mods that are sold, or that earn Donation Points: not allowed.

AELAS is distributed off-Nexus and carries only
`Copyright © 2024-2026 Kitsuune All rights reserved`, with no licence named and
no source published.

Read those terms precisely. Every clause governs **their files and their
assets**: uploading them, modifying them, converting them, using assets out of
them. SkyrimBridge does none of those. It ships no Kitsuune file, asset, config,
binary, or source, so no clause above is engaged by what we distribute.

That is a statement about their permission block, not a clearance. What remains
is the part no licence text settles: reimplementing another author's plugin from
their compiled binary is a matter between authors, and Kitsuune is active
(KreatE was updated 2026-08-09).

## Release posture

**Permission has not been requested, and has not been granted.**

Be precise about what is and is not public, because these are different things.

The **source** is public, and as of 2026-08-14 it is on `main`. It had been
readable on a feature branch since roughly 2026-07, before this file existed.
Standing decision: leave it public, now that it carries attribution and defers
to the originals at runtime.

The suite is **not distributed as a mod**. It is excluded from public release
packages and is not uploaded to Nexus or anywhere else. The release pipeline is
testing, then a GitHub release as staging, then Nexus as the official release,
and the exclusion applies at every stage. A built binary is a stronger act than
readable source.

### How to build without it

```
cmake -S . -B build -DSKYRIMBRIDGE_NATIVE_REPLACEMENTS=OFF
```

`OFF` is the default and is the only public-package configuration. It omits the
six units from the target sources entirely, so the suite is absent from the
binary rather than disabled at runtime: an inert copy is still a copy. Private
operator builds must opt in explicitly with
`-DSKYRIMBRIDGE_NATIVE_REPLACEMENTS=ON`.

The distributable build still detects Kitsuune's plugins and defers to them,
because that detection lives in `CompatDetect` and is not part of the suite.
What changes is that SkyrimBridge no longer offers its own implementation when
they are absent. The weather workshop keeps working; without the native
EditorID cache it takes names from whichever provider the user already has, and
falls back to naming weathers by FormID.

`tests/validate_release_package.py` and `scripts/package.py` enforce this
against the shipped bytes rather than the build configuration. A marker for the
private suite is always a hard failure on the public package path; there is no
warning-only or private-success archive mode.

SkyrimBridge's other
capabilities ship without it: live parameters, the texture / model / collision
pipelines, diagnostics, and the command channel. None of those touch anyone
else's work.

Readable source is not a licence and not a release. Do not read the first fact
as permission for the second.

This posture changes only if Kitsuune grants explicit permission. Nothing about
the code, its quality, or the fact that it is a reimplementation rather than a
copy changes the obligation. It is owed to the author whose work was reversed.

## Everyone else

The full technique and format lineage is recorded in the handoff brief
(`shader-handoff/HANDOFF.md`, section 5) and in the shader headers: Boris
Vorontsov / ENBSeries, kingeric1992, Adyss, TreyM, l00ping,
TheSandvichMaker / ReforgedUI, po3, Havok, niftools, CommonLibSSE-NG, and
DirectXTex. Reliance on those is by technique, format, or citation, not copied
source. Preserve every credit in headers, docs, and any public surface.
