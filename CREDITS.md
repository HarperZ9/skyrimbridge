# Credits and provenance

SkyrimBridge is MIT licensed (`LICENSE`). Vendored third-party components are
listed in `THIRD_PARTY_NOTICES.md`. This file covers a different obligation:
work by other authors that SkyrimBridge reverse-engineered and reimplemented.
Read it before any public release.

## Kitsuune (LonelyKitsuune) — original author of the reversed plugins

SkyrimBridge's native ENB-plugin replacement suite reimplements the functionality
of plugins authored by **Kitsuune (LonelyKitsuune)**. Each unit below exists
because it reproduces the behavior of one of their plugins:

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
`kPostLoad` and again at `kDataLoaded`, and when the original is loaded the
matching SkyrimBridge unit stands down and logs that it deferred. A user who
already runs KreatE, ELIF, EVLaS/AELAS, Native EditorID Fix, or ENB Worldspace
Weatherlists keeps them, and SkyrimBridge fills only what is absent.

The replacements exist so a load order does not *need* these plugins, not to
compete with them. That is a design constraint, not a courtesy: do not add a
code path that overrides a detected original.

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

What ships is original SkyrimBridge code reproducing observed behavior. No
Kitsuune source, binary, decompiled output, or `.kfg`/`.cfg` config is
redistributed. The RE notes and binaries are held privately outside this
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

The **source** is public. It sits on this branch in a public repository and has
since roughly 2026-07, before this file existed. It is not on `main`. Standing
decision, 2026-08-14: leave it there, now that it carries attribution and defers
to the originals at runtime.

The suite is **not distributed as a mod**. It is excluded from public release
packages and is not uploaded to Nexus or anywhere else. SkyrimBridge's other
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
