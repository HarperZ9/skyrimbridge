# SkyrimBridge: the engine-exposure mission (handoff for a fresh session)

You are picking up a live, real project. This document is your full context so
you can continue with momentum and no re-derivation. Read it once, top to
bottom, then act. The goal is big and worth finishing. Take it all the way.

---

## 0. The one-line mission

Turn **SkyrimBridge** (an SKSE plugin for Skyrim SE/AE) into a deeply useful,
safe utility that **exposes the whole game engine**: read, write, translate, and
verify any engine record losslessly, and natively integrate foreign
(non-.nif/.dds) assets. It is also the operator's ENB toolchain: it already
natively replaces the third-party plugin stack their weather/lighting presets
depended on. Both threads live in one plugin.

You are not starting from zero. A large, coherent foundation is built, compiled,
and installed. Your job is to extend it to completion along the sequenced
roadmap in section 6, keeping every claim honest and every write bounded.

---

## 1. How to work here (the operator's standard, non-negotiable)

- **Accountability partner, not a cheerleader.** No sycophancy, no "great
  question," no flattery. State the answer. Disagree plainly when you disagree;
  agree plainly when you agree. Do not capitulate under pushback without new
  evidence.
- **Truth over approval.** Put a one-word confidence label (high / moderate /
  low / unknown) on non-trivial factual claims (offsets, IDs, versions,
  file:line). "Unknown" beats a plausible fabrication. When you state a fact
  that could be wrong, verify it (grep, read, run) or label it.
- **Honest nulls stay.** If something is not done, say so. Do not fake a stage
  to look complete. The codebase already has explicit "not faked" markers; keep
  that discipline.
- **In-game validation is the acceptance oracle.** You cannot run the game.
  Anything that mutates live engine state is validated by the operator in-game.
  So: make the offline-provable part provable (unit-check parsers/codecs against
  real files), and mark the game-bound part clearly for their validation. Ship
  risky live-write features config-gated and OFF by default.
- **No dying subagent fan-outs.** Implement inline. Read-only surveys via agents
  are fine; write/build work is inline and single-author. (This tempers any
  "use a workflow" default. A one-module build or one document is inline work.)
- **Voice:** no em-dashes (rewrite, do not swap the character). Feature-first,
  words with weight, no hype, human. No biological metaphors for code (use
  component/module/service).
- **Reverse anything you need. It is second nature.** But see section 2 first,
  because the main executable is a wall and you must not waste effort on it.

---

## 2. The decisive reality: the exe is DRM-packed (read this before any RE)

`SkyrimSE.exe` (AE **1.6.1170**, at
`E:\Modlists\SkyGroundChronicles\Stock Game\SkyrimSE.exe`, ImageBase
0x140000000, ~120,080 `.pdata` functions) is **SteamStub DRM-packed**. It has a
`.bind` section and the main `.text` is **encrypted on disk**, decrypting only
in the running process. Reading `.text` prologue bytes off disk yields garbage
(verified: `Sky::GetSingleton` @ RVA 0x1C2640 reads `1E 6C 1C 1F...`).

**Consequence (high confidence, binary-verified):** static disassembly, RTTI
recovery, or decompilation of the whole executable **from the file is
impossible**. Only `.data`/`.rdata` are statically readable. Do **not** attempt
a static full-exe reversal; it is a wall, not a to-do.

**The engine reversal already exists and is what you build on:**
- **CommonLibSSE-NG** RE:: types = the community's decompiled engine (thousands
  of typed structs with member offsets). SkyrimBridge links it. Headers are at
  `C:\dev\skyrimbridge\build\vcpkg_installed\x64-windows-static\include\RE\`.
- **Address Library** `versionlib-1-6-1170-0.bin` (428,461 entries) = ID -> RVA.
- The operator's recovered offset spec:
  `C:\dev\protected\migration-ledger\enb-recovery-2026-07-14\OFFSET-SPEC-camera-sky-weather-2026-07-15.md`
  (Sky singleton, currentWeather +0x048, currentWeatherPct +0x360,
  PlayerCamera.worldFOV +0x13C, the ~7 key singleton AE IDs).
- Prior reversal corpus (already productized into SkyrimBridge's trackers):
  `C:\dev\protected\reverse-engineering\SKSE\Playground\`.

**Boundary:** the DRM affects only the main exe. Plugin DLLs are NOT packed, so
static disassembly of them is valid. Runtime patching of the decrypted process
via the Address Library works (see `EngineFixes`). If you ever truly need raw
code, runtime memory dumping of the live process is the only path, and it is
almost never necessary because CommonLib covers it.

---

## 3. What is built, compiled, and installed (the current state)

Repo: `C:\dev\skyrimbridge\` (50+ core sources, 29 Papyrus natives). Build with
VS18 + CommonLibSSE-NG 3.7.0 (static x64 triplet). Installed DLL target:
`E:\Modlists\SkyGroundChronicles\mods\SkyrimBridge\SKSE\Plugins\SkyrimBridge.dll`.

### 3a. Native replacements for the third-party ENB plugin stack
The operator's KreatE presets used to depend on Kitsuune plugins. All are now
replaced natively so the presets run with no external loader, using
SkyrimBridge's OWN flat-INI config (never the third-party `.kfg`/`.cfg`).
- **`EditorIDCache`** replaces NativeEditorID Fix (vtable 0x33 hook on 85+ form
  types + `GetFormEditorID` export). Always on.
- **`WorldspaceWeatherlist`** replaces ENBWorldspaceWeatherlists + KiLoader.
  Reads `enbseries\WeatherRouting.ini` (our `[Routes]` grammar), one-time
  imports a legacy `_worldspaceweatherlist.ini`. Polls player worldspace,
  rewrites `_weatherlist.ini`, calls ENB `DirtyHack(3)` reload. On.
- **`KreateProfile` + `KreateRecords`** apply all FIVE KreatE record types
  (ImageSpaces, Weathers, LightingTemplates, VolumetricLighting,
  ShaderParticleGeometries) through typed RE:: members. The Weather applier is
  field-complete (verified against all 21,114 real weather files). On.
- **`SkyLighting`** replaces AELAS (+EVLaS). Reads `Sky.ini` (our flat sections,
  not the German `.kfg`). Orbital math (axial tilt/latitude/solstice -> sun
  altitude/azimuth) + moon-phase night ambient are complete. `[Sky] Enable`
  ships OFF: the live ambient write is a look to tune in-game. HONEST NULL: the
  deep AELAS behavior (reposition the sun so hard shadows follow the model)
  needs the AELAS.dll hook reversal, not yet done (binary preserved in
  `C:\dev\protected\reverse-engineering\kitsuune-plugins-2026-07-16\binaries\`).
- **`EnbLightInventoryFix`** replaces ELIF. Reproduces the reversed detour
  (inventory-3D preview: zero light emittance +0x11c, NiAVObject::Update,
  original, restore) via trampoline hooks on AE IDs 51769/51770. AE-only,
  SEH-guarded, ships OFF (operator keeps real ELIF.dll until validated).
- **`EngineFixes`** = recovered binary patch (BSSpinLock threshold 10000->1000,
  AE `REL::ID(68233)`, validated before write). Proof that runtime patching
  works despite DRM. `[Native] EngineFixes` default on.

Toggles live in `config/SkyrimBridge.ini` `[Native]`. RE specs for the reversed
plugins: `C:\dev\protected\reverse-engineering\kitsuune-plugins-2026-07-16\`.

### 3b. EngineReflect (the engine-exposure SPINE)
`src/core/EngineReflect.{h,cpp}`. A runtime schema over engine records +
generic Read / Write / Translate (to our flat INI) / **Verify** (a round-trip
losslessness witness). Built on CommonLib RE:: layouts + the OFFSET-SPEC.
Nothing re-derived from the DRM'd exe.
- `Value` (Float / Int / Bool / Color3 / Color4 / FormLink / String), `Field`
  (name + kind + std::function get/set), `Schema` (per FormType), registry.
- **Eleven schemas registered:** ImageSpace, Volumetric, LightingTemplate,
  **Weather (full 487-field record)**, Climate, Region, Light, Water (full
  DNAM shader block), EffectShader, ImageSpaceModifier, WorldSpace. Adding a
  record type is one schema block; the macros (RF_F/RF_INT/RF_B/RF_FLAGS/RF_S/
  RF_LINK/RF_C3F/RF_C3B) and the Weather-style programmatic builder handle
  scalars, links, strings, and indexed arrays. Honest exclusions are named in
  comments at each schema (e.g. EffectShader blend modes: the D3DBLEND enums
  are only forward-declared in CommonLib; ImageSpaceModifier's uint32 DNAM
  fields are exposed as raw Ints, not lossy-decoded).
- Papyrus surface: `EngineReflectDump 0x<id>` (writes
  `SkyrimBridge/dumps/<id>.ini`), edit it, `EngineReflectApply 0x<id>`,
  `EngineReflectVerify 0x<id>` (witnesses the lossless round-trip),
  `EngineReflectVerifyStrict 0x<id>` (opt-in write-back idempotence witness;
  mutates), `EngineReflectList 0|0x<id>` (schema/field discovery, in-game).

### 3c. TextureCodec (foreign-asset integration)
`src/core/TextureCodec.{h,cpp}` + `src/core/Inflate.{h,cpp}`. Pure, zero-dep.
- Decode: **PNG** (from-scratch DEFLATE/zlib inflate with verified Adler-32 +
  per-chunk CRC-32; all five color types; 1/2/4/8/16-bit, 16-bit narrows via
  high byte; all five filters; Adam7 interlace; tRNS palette alpha + colorkey),
  TGA (uncompressed + RLE), BMP (24/32-bit), **DDS** (uncompressed 32-bit
  masked, DXT1/BC1, DXT5/BC3; top mip).
- Encode: DDS as uncompressed RGBA8 **or BC1/BC3 block-compressed** (baseline
  encoder tier, stated in-source: most-distant-pair endpoints, nearest index),
  optional clamp-edge box-filter mipmap chain down to 1x1; 32-bit TGA write
  (the DDS -> editable-format lane).
- Papyrus natives `ConvertTexture(in,out)` (dispatches on output extension)
  and `ConvertTextureFmt(in,out,"BC1"|"BC3"|"RGBA8")`.
- VALIDATED offline (102 checks, `tests/validate_png_codec.py` +
  `tests/validate_bcn_codec.py`, both in-repo and re-runnable): inflate
  byte-exact vs zlib on the real IDAT streams (incl. a 33.7 MB stream); PNG
  pixel-exact vs PIL on 40 real ENB PNGs + synthetic modes + hand-built
  Adam7/16-bit cases; BC decode exact vs PIL on hand-built blocks (every mode,
  incl. DXT5's always-4-color rule) and 8 real modlist DXT1/DXT5 textures; BC
  encode PIL-agreed with 39-41 dB PSNR on real content and lossless on
  exactly-representable blocks; DDS structure + mip math; TGA write via PIL
  read-back. The BC interpolation arithmetic was locked empirically against
  PIL first (truncating /3, /2, /7, /5).
HONEST NULLS: BC7, DX10-header DDS, cubemaps/volumes, DDS mip-chain read
beyond the top level, PNG/BMP write, and the runtime texture-load
substitution hook are NOT done.

### 3d. Config architecture (ours, not theirs)
`src/core/SBConfig.h` = one flat-INI dialect (`[Section]`, `Key = Value`, `;`
comments, `R,G,B,A` tuples, `[Base:FormID]` override suffix). Every native
component uses it. Configs ship via CMake POST_BUILD: `SkyrimBridge.ini`,
`Sky.ini`, `WeatherRouting.example.ini`, plus the existing GPU/WeatherParams/
WriteBack configs.

---

## 4. Build / install / validate loop

```
cd C:\dev\skyrimbridge
cmake --build build --target SkyrimBridge --config Release
cp build/Release/SkyrimBridge.dll \
   "E:/Modlists/SkyGroundChronicles/mods/SkyrimBridge/SKSE/Plugins/SkyrimBridge.dll"
```
- Offline validation you CAN do: port a parser/codec to Python, run it against
  the real files (KreatE presets, ENB textures) and cross-check vs ground truth
  (PIL for images). This is the accepted method here (no standalone C++ compiler
  is on PATH). Real inputs: KreatE presets at
  `E:\Modlists\SkyGroundChronicles\mods\Elder ENB\KreatE\Presets\`; ENB textures
  under the various `*ENB*/ROOT/enbseries/` folders.
- In-game validation: the operator runs it. Give them the exact console command
  (`cgf "SkyrimBridge.<Native>" ...`) and what to look for. Log to
  `Documents\My Games\Skyrim Special Edition\SKSE\SkyrimBridge.log`.

---

## 5. Architecture principles to preserve

- **Reflection, not re-derivation.** Build on CommonLib RE:: types. Reverse only
  the specific gaps CommonLib does not name (a plugin DLL, an undocumented
  offset), never the whole exe.
- **"Entropy-free" = witnessed, and honest about scope.** Losslessness holds
  over the persistent, schema-defined fields, NOT raw memory (derived/cached
  pointers/handles are excluded). Never claim entropy-free over whole structs.
  The `Verify` round-trip is the witness; keep it truthful (raw-byte fields as
  Int, not lossy-normalized, where it matters).
- **"Safe" is bounds, not a word.** Form-type gating (writable records only,
  never code/vtables), field validation, opt-in per op, dry-run + verifier,
  SEH/exception isolation. Live-write features ship gated.
- **One config dialect (SBConfig), our nomenclature.** No third-party formats
  in the runtime path; provide importers if backward compat is needed.

---

## 6. The roadmap: take it all the way (dependency order)

Do these roughly in order. Each is a real, bounded increment. Difficulty and
confidence are labeled honestly.

**Lane A: finish EngineReflect coverage + power.**
1. ~~More schemas~~ DONE 2026-07-16: Light, Water, EffectShader,
   ImageSpaceModifier, WorldSpace registered (compile-verified against
   CommonLib member layouts; in-game dump/apply/verify is operator-validated).
   Further types (BGSMaterialObject, TESObjectLAND, ...) remain one-block adds.
2. ~~`EngineReflectList` native~~ DONE 2026-07-16 (0 = all schemas, 0x<id> =
   that form's fields; writes `dumps/schema-<name>.txt` + log).
3. ~~Stricter Verify mode~~ DONE 2026-07-16: `EngineReflectVerifyStrict`,
   separate opt-in native since it mutates.
4. ~~Structured/variant records~~ DONE 2026-07-16: `RegionWalker`
   (`src/core/RegionWalker.{h,cpp}`) dumps every CommonLib-typed TESRegionData
   subrecord (Weather lists with chances/globals, Sound lists, Map name, Land
   icon; Objects/Grass/Imposter listed by type+priority, honestly not decoded:
   no CommonLib layout). One bounded write: `RegionSetWeatherChance` /
   `RegionApply` edit chances on EXISTING entries only, no list surgery.
   Natives: RegionDump / RegionSetWeatherChance / RegionApply. Game-bound
   validation (live-form data source). **Lane A is COMPLETE** except new
   schema types as needs arise.

**Lane B: complete the texture pipeline (the operator's explicit asset goal).**
5. ~~**PNG decode**~~ DONE 2026-07-16: from-scratch inflate (`Inflate.{h,cpp}`)
   + full PNG decoder, validated pixel-exact vs PIL on 40 real ENB PNGs and
   byte-exact vs zlib on their IDAT streams (77-check harness).
6. ~~**Mipmap generation**~~ DONE 2026-07-16: clamp-edge box filter to 1x1,
   default-on in `ConvertToDDS`, single-mip layout preserved otherwise.
7. ~~**BC1/BC3 block compression**~~ DONE 2026-07-16 (encode + decode, PIL-exact
   decode model, baseline encoder tier; DDS read + TGA write landed with it).
   OPEN remainder: BC7 (genuinely hard to do well) and DX10-header DDS.
8. **The runtime texture-substitution hook (the payoff).** Hook the game's
   texture-load path so a dropped `.png`/`.tga`/`.bmp` is decoded by TextureCodec
   and served where the engine expects `.dds`, transparently. SkyrimBridge
   already owns a D3D11 hook/proxy (`D3D11Hook`, `src/d3d11_proxy/`). This is
   game-bound: identify the load function (via CommonLib / the community), wrap
   TextureCodec, create the ID3D11Texture2D + SRV. Ship gated, validate in-game.
   (hard, game-bound)

**Lane C: the model pipeline (R&D, the ambitious 20%).**
9. **glTF / OBJ static-mesh import to runtime NiObject graphs.** Construct
   BSTriShape/BSGeometry from foreign meshes (vertices, normals, tangents, UVs)
   + BSLightingShaderProperty materials. Start static meshes only; defer skinned
   and animated. The pragmatic 80% alternative is an offline glTF->nif converter
   (the nif runtime format is community-reversed: nifly/nifxml). (hard/R&D)

**Lane D: expose it as a real utility surface.**
10. Broaden the surface beyond Papyrus: the existing shared-memory channel
    (`SharedMemoryBridge`) for external tools, and optionally a small local IPC
    endpoint, so EngineReflect + TextureCodec are usable by an external editor,
    not only console. (moderate)
11. ~~Documentation spec sheet~~ DONE 2026-07-16: `docs/SPEC-ENGINE-EXPOSURE.md`
    (schemas table with verified field counts, region walker, texture format
    matrix, all 29 natives with signatures + console forms, config grammar,
    validation receipts). Keep it in the same commit as any surface change.

**Lane E: close the honest nulls already on record.**
12. Reverse `AELAS.dll`'s hooks (a plugin DLL, NOT DRM-packed, so static disasm
    is valid) to finish `SkyLighting`'s sun-repositioning / shadow-direction
    behavior. Binary is preserved in the protected binaries dir. (moderate RE)
13. In-game-validate and then default-enable `EnbLightInventoryFix` and
    `[Sky] Enable` once the operator confirms the look/behavior. (operator-gated)

---

## 7. North star (what "all the way" looks like)

A single SKSE plugin that lets anyone **read, edit, verify, and translate any
engine record** through a clean schema, **drop in modern asset formats** (PNG,
glTF) and have the engine use them natively, and **patch/fix** the running engine
safely via the Address Library, all on the community's reversal (CommonLib) with
the specific gaps reversed as needed, all bounded and witnessed. It is the
operator's ENB toolchain and, more broadly, a genuinely useful engine-exposure
utility that did not exist before.

The foundation is real and proven where it can be. Extend it honestly, validate
what you can offline, hand the operator exact in-game checks for the rest, and
keep the nulls honest. Finish it.

---

## 8. Key paths (quick reference)

| What | Where |
|---|---|
| SkyrimBridge repo | `C:\dev\skyrimbridge\` (src/core, config, docs) |
| Installed DLL | `E:\Modlists\SkyGroundChronicles\mods\SkyrimBridge\SKSE\Plugins\SkyrimBridge.dll` |
| CommonLib RE:: headers | `...\skyrimbridge\build\vcpkg_installed\x64-windows-static\include\RE\` |
| The exe (DRM-packed) | `E:\Modlists\SkyGroundChronicles\Stock Game\SkyrimSE.exe` (AE 1.6.1170) |
| Address Library bin | `...\Address Library for SKSE Plugins\SKSE\Plugins\versionlib-1-6-1170-0.bin` |
| Offset spec | `C:\dev\protected\migration-ledger\enb-recovery-2026-07-14\OFFSET-SPEC-camera-sky-weather-2026-07-15.md` |
| Kitsuune plugin RE specs | `C:\dev\protected\reverse-engineering\kitsuune-plugins-2026-07-16\` |
| Prior reversal corpus | `C:\dev\protected\reverse-engineering\SKSE\Playground\` |
| KreatE presets (test data) | `E:\Modlists\SkyGroundChronicles\mods\Elder ENB\KreatE\Presets\` |
| Memory (orientation) | `[[skyrimbridge-engine-exposure-vision-2026-07-16]]`, `[[skyrimbridge-native-plugin-replacements-2026-07-16]]`, `[[skyrimse-exe-drm-reversal-reality-2026-07-16]]` |
