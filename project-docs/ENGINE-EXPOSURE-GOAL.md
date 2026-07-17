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

Repo: `C:\dev\skyrimbridge\` (50+ core sources, 31 Papyrus natives). Build with
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
- **Fourteen schemas registered (827 fields):** ImageSpace, Volumetric, LightingTemplate,
  **Weather (full 487-field record)**, Climate, Region, Light, Water (full
  DNAM shader block), EffectShader, ImageSpaceModifier, WorldSpace, Grass,
  LandTexture, Tree (OBJ_TREE SpeedTree sway params). Adding a
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
HONEST NULLS: BC4/BC5/BC6H, cubemaps/volumes/arrays, DDS mip-chain read
beyond the top level, and PNG/BMP write are NOT done; sRGB payloads pass
through unconverted. (BC7 + DX10-header DDS landed 2026-07-16 late, lane
B7 remainder; the runtime texture-load hook is built and gated OFF, B8.)

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
   ~~OPEN remainder: BC7 and DX10-header DDS~~ DONE 2026-07-16 late:
   `src/core/TextureBC7.{h,cpp}` decodes all 8 BC7 modes per the D3D11 spec
   (partition/anchor tables machine-extracted from DirectXTex, and the test
   harness re-parses them out of the shipped .cpp so the compiled data is
   what gets verified); encode is mode 6, baseline tier, stated in-source.
   DX10-header DDS read (BC1/BC3/BC7/RGBA8/BGRA8; cubemaps/arrays declined)
   and BC7 DX10 write, wired through ConvertTextureFmt("BC7"),
   `[TextureConvert] Format = BC7`, and the command channel (argInt 3).
   Offline receipt: `tests/validate_bc7_codec.py`, 92/92 (8-mode random-block
   fuzz + 70+ real modlist BC7 files byte-exact vs PIL). Remaining honest
   nulls: BC4/BC5/BC6H, cubemaps/volumes/arrays, sRGB passthrough only.
8. ~~**The runtime texture-substitution hook (the payoff).**~~ BUILT 2026-07-16,
   game-bound validation pending. Two halves, both `[Native]`-gated OFF:
   - `TextureAutoConvert` (`src/core/TextureAutoConvert.{h,cpp}`): background
     scan at kDataLoaded; every `textures\*.png/.tga/.bmp` without a `.dds`
     sibling is transcoded next to it (additive only). Console:
     `TextureScanNow true|false` (dry run / live). Offline dry-run against the
     real modlist found 163 foreign textures, 22 without a `.dds` sibling.
   - `TextureLoadHook` (`src/core/TextureLoadHook.{h,cpp}`): vtable detours on
     `BSResource::LooseFileLocation` (CommonLib VTABLE VariantID 232012/188191)
     over DoCreateStream/DoCreateAsyncStream/DoGetInfo1/DoGetInfo2. On a missing
     `textures\*.dds` with a foreign sibling: transcode once into
     `SkyrimBridge/texcache/` under the location's own prefix, then re-invoke
     the ORIGINAL vfunc on the cache path, so the engine serves its own
     LooseFileStream (sync + async both engine-native; no synthetic engine
     objects, which is decisive on AE where `LooseFileStream::Create` is
     compiled out of CommonLib). Detours act only after the original call
     missed, only under `textures\`, never on cache paths; thunks call through
     the saved original pointer (coexists with other vtable patchers).
     Semantics note: a loose foreign texture overrides a BSA `.dds`, matching
     the loose-over-archive rule. NOT re-derived from the DRM'd exe: design
     corroborated by FileCacheSSE (Location vfuncs 03/05/06/07 hook family) and
     Faster-File-Copy (BSResource vtable patches proven on AE 1.6.1170).
   In-game validation: enable both, drop a PNG at a mesh-referenced
   `textures\` path with no `.dds`, confirm it renders + check
   SkyrimBridge.log for "TextureLoadHook: ... -> texcache/". (game-bound)

**Lane C: the model pipeline.**
9. ~~**glTF / OBJ static-mesh import**~~ the pragmatic 80% DONE 2026-07-16:
   `ModelCodec` (`src/core/ModelCodec.{h,cpp}`) parses OBJ + glTF/GLB (minimal
   zero-dep JSON reader; float POSITION/NORMAL/TEXCOORD + u16/u32 indices;
   external/embedded/base64 buffers) and emits a valid Skyrim SE NIF
   (20.2.0.7 / user12 / bs100): BSFadeNode -> BSTriShape +
   BSLightingShaderProperty + BSShaderTextureSet. The byte layout reproduces a
   real shipping SSE static mesh field-for-field (vertexDesc
   0x0003B00007650408, 32-byte full-precision vertex, particleData trailer), so
   it is engine-loadable by construction. Normals (area-weighted) and tangents
   (Lengyel) are computed when absent. Native `ConvertModel(in,out)`. Validated
   offline by `tests/validate_model_codec.py` (21 checks): the emitted NIF
   re-parses with an independent reader, geometry round-trips full-float exact,
   and our decoder consumes a real modlist BSTriShape byte-for-byte.
   RUNTIME PATH DONE 2026-07-16 late: `SpawnModel` native (32nd) converts a
   foreign mesh into `meshes\SkyrimBridge\spawn\`, creates a dynamic Static
   form via the engine's form factory, and places a reference at the player,
   so the ENGINE's own loader constructs the NiObject graph. Synthetic
   in-memory BSTriShape construction is now an explicit NON-GOAL (same rule
   as B8: no synthetic engine objects; hand the engine files it natively
   loads). Game-bound: docs/VALIDATION-PROTOCOL.md section 7. MUTATES the
   save (dynamic form + placed ref): disposable-save testing only.
   HONEST NULLS remaining: skinned/animated meshes; multi-shape files (first
   primitive/group only); collision (bhk*) generation; Draco/sparse glTF;
   spawned material fidelity is bounded by what the source carried.

**Lane D: expose it as a real utility surface.**
10. ~~Broaden the surface beyond Papyrus~~ DONE 2026-07-16: external command
    channel. A second shared-memory region (`SkyrimBridge_Command`, layout in
    `src/SB_CommandLayout.h`, plugin side `src/core/BridgeCommand.{h,cpp}`,
    header-only client `tools/SkyrimBridgeClient.h`) drives EngineReflect,
    RegionWalker, TextureCodec, and ModelCodec from an external tool. Eleven
    verbs (model.spawn joined 2026-07-16 late for the Blender addon),
    single-slot sequence-gated protocol, one request per frame,
    SEH-isolated dispatch. `[Native] CommandSurface`, ships OFF. Offline-proven
    (`tests/validate_command_protocol.py`, 15 checks: ctypes ABI mirror +
    200-round sequence-gated round-trip). Game-bound: live dispatch through a
    running client (validation protocol has the steps).
11. ~~Documentation spec sheet~~ DONE 2026-07-16: `docs/SPEC-ENGINE-EXPOSURE.md`
    (schemas table with verified field counts, region walker, texture format
    matrix, model codec, all 31 natives with signatures + console forms, config
    grammar, the command channel + verb table, validation receipts). Keep it in
    the same commit as any surface change. **Lane D is COMPLETE.**

**Lane F: creator QoL pipeline (opened 2026-07-16 late, operator-directed).**
The thesis: Blender and the paint tools are the front end, the running game is
the preview window; SkyrimBridge's command channel is the transport. Wedge
order agreed with the operator:
14. ~~Blender push-to-game addon~~ DONE 2026-07-16 late:
    `tools/blender/skyrimbridge_push.py` (single file, install from
    Preferences > Add-ons). One button in the 3D-view sidebar: exports the
    selection as GLB (Z-up, since ModelCodec passes glTF coordinates through
    unchanged into NIF space) and sends `model.spawn` over the mailbox; the
    engine's own loader places the mesh at the player in seconds. The verb
    rides the new shared `ModelSpawn` core (same code as the SpawnModel
    native). Offline-proven: `tests/validate_blender_addon.py`, 12/12
    (protocol layer standalone against a simulated dispatcher on a test-named
    mapping; caught and fixed the int32 FormID bit-pattern crossing for
    0xFFxxxxxx dynamic forms). Game-bound: protocol section 7. MUTATES the
    save per spawn; conversion runs on the game's frame thread (a very large
    mesh hitches for the convert's duration).
15. ~~Alpha-coverage-preserving mipmaps for foliage~~ DONE 2026-07-16 late:
    `ScaleAlphaToCoverage` in TextureCodec (bisected alpha scale per mip,
    erring thick within quantization), wired as `coverageThreshold` through
    EncodeDDS/WriteDDS/Convert. Surfaces: `ConvertTextureFoliage` native
    (33rd), `texture.foliage` verb (12th), `[TextureConvert] CoverageMips` +
    `CoverageThreshold` for the background converters (both configs updated,
    default OFF). Offline-proven: `tests/validate_foliage_mips.py`, 10/10
    (defect demonstrated first: synthetic sparse foliage collapses to 0.000
    coverage under naive mips, real modlist foliage decays measurably; fix
    holds c0 on every mip and survives BC3 quantization). The in-game payoff
    (distant foliage keeps its density) is aesthetic and operator-judged.
16. ~~Tree wind vertex-color auto-painting~~ DONE 2026-07-16 late, the
    verification-first way: the channel mapping was derived empirically from
    real animated trees BEFORE any code (Aspens Ablaze + vanilla Dawnguard
    glade tree, parsed offline): animated shapes carry SLSF2 bit 29
    (Tree_Anim); the sway weight is grayscale R=G=B vertex color, 127 on
    near-rigid geometry rising to 255 at canopy extremities (positive
    correlation with distance from the trunk base, +0.36..+0.54 on leaf
    shapes); vertex alpha constant per shape (vanilla 255, aspens 68,
    semantics unrecovered = honest null); roots are BSLeafAnimNode.
    ModelCodec tree mode reproduces exactly that (weight 0.5 + 0.5*e^1.5 on
    normalized distance from base, alpha 255, Tree_Anim, BSLeafAnimNode).
    Surfaces: ConvertModelTree native (34th), argInt=1 tree mode on
    model.convert / model.spawn, "Push as tree" checkbox in the Blender
    addon (v1.1.0). Offline-proven: tests/validate_tree_wind.py, 22/22.
    Game-bound (protocol section 7): does the pushed tree sway, and does a
    STAT-placed reference animate at all vs needing a TREE form.
17. ~~TESGrass/GRAS schema~~ DONE 2026-07-16 late: twelfth schema, 11 fields
    (model path + the full typed DATA block: density, min/max slope, water
    distance/state, position/height/color ranges, wave period, flags), so
    grass tuning rides the existing dump/apply/verify loop and the command
    channel. Compile-verified against CommonLib's TESGrass layout, like every
    schema; in-game dump/apply/verify is operator-validated. Total 799
    fields. Honest scope note in-schema: placement (which landscape textures
    grow a grass) lives on TESLandTexture, a future one-block add; grass
    LOD/cache stays NGIO/DynDOLOD territory.
19. ~~Convex-hull collision generation~~ DONE 2026-07-16 late (the F18
    recipe implemented): `src/core/ConvexHull.{h,cpp}` (quickhull,
    global-scan variant, doubles internally, degenerate input refused) +
    collision emission in ModelCodec (bhkConvexVerticesShape in Havok
    units, 250-byte donor-templated static bhkRigidBody, bhkCollisionObject,
    BSXFlags 130, root refs). Surfaces: ConvertModelEx native (35th),
    argInt bit 2 on model.convert/model.spawn, "With collision" checkbox in
    the addon (v1.2.0). Offline-proven: tests/validate_collision_gen.py,
    19/19 (hull properties, donor-byte template consistency, container
    round-trip, Havok scale). Game-bound: WALK against a spawned hull
    (protocol section 7); a mis-scaled hull looks exactly like the two
    ported outliers the investigation found. Convex cannot be concave.

18. ~~Convex-hull collision investigation~~ DONE 2026-07-16 late, verdict
    GREEN (`docs/COLLISION-INVESTIGATION-F18.md`). The premise is confirmed
    against real files: 323 of 342 convex-collision NIFs in a 6,000-file
    survey ship bhkConvexVerticesShape with NO MOPP anywhere; the full block
    chain's byte layout is recovered and verified (consumed == blockSize);
    the Havok scale data brackets the canonical ~70 constant; the 250-byte
    bhkRigidBody is constant-size and template-viable from a known-good
    static. Two mis-scaled ported assets found in the wild show exactly what
    in-game validation catches. Implementation (quickhull + template blocks,
    OFF by default, walk-test as acceptance) is now a bounded follow-on
    lane, not R&D. Honest limit: convex cannot be concave; MOPP-accurate
    mesh collision stays out of scope.

**Lane G: community pain points, never-built fixes (opened 2026-07-16 late,
operator-directed).** Assessment of record with sources and the full
category table: `docs/PAIN-POINTS-ASSESSMENT-2026-07.md`. Build order:
20. ~~Cell performance census~~ DONE 2026-07-16 late:
    `src/core/CellReport.{h,cpp}` + `CellReport` native (36th) +
    `cell.report` verb (13th). One command turns the manual CK/xEdit
    shadow-light hunt into a live report: refs by form type, every
    shadow-casting light (SpotShadow/HemiShadow/OmniShadow bits) with the
    placed ref's FormID, winning plugin, and distance nearest-first, and
    per-plugin ref counts (who is crowding this cell). Read-only; full text
    to dumps/cellreport.txt. Compile-verified against CommonLib
    (ForEachReference, sourceFiles, TES_LIGHT_FLAGS); the numbers are
    game-bound (protocol section 8). Honest scope: counts and attribution,
    not per-draw GPU timing; the shadow-light list is the community's own
    proven proxy.
21. ~~Live Papyrus VM monitor~~ DONE 2026-07-17:
    `src/core/ScriptReport.{h,cpp}` + `ScriptReport` native (37th) +
    `script.report` verb (14th). Live answers to "is my game script-lagged
    and who is responsible" (ReSaver is post-mortem only): the VM's own
    overstressed flag, waitingFunctionMessages queue depth (the direct lag
    indicator), running/latent/frozen stack counts, which script classes are
    executing RIGHT NOW (top stack frames under runningStacksLock), and the
    per-class attached-instance census (the save-bloat view, under
    attachedScriptsLock). Read-only. THREADING CONTRACT: Run() takes the
    VM's own locks so it executes on the frame thread only; the Papyrus
    native queues via Request()/Tick() (a native runs inside the VM and must
    not take its locks); the channel verb runs inline (dispatch is already
    frame-thread). Overflow/cleanup array sizes are plain u32 reads, torn
    counts tolerated and stated. Compile-verified against CommonLib's fully
    typed VirtualMachine layout; the numbers are game-bound (protocol
    section 9).
22. ~~Modlist smoke tour~~ DONE 2026-07-17: `tools/sb_smoke_tour.py` +
    two verbs (`game.status` read-only heartbeat returning the current cell
    FormID with 0 during loads; `game.coc` console-exec teleport via
    RE::Script SetCommand/CompileAndRun, MUTATES game state). The driver
    teleports through a cell list, detects landing by cell-id change,
    settles, captures the cell census and VM monitor summaries per stop,
    and renders one of three verdicts: PASS, FAILURES (a stop never landed,
    tour continues), CRASH (channel died, reported at the exact stop).
    Offline-proven: `tests/validate_smoke_tour.py`, 11/11 against a
    simulated game (delayed landings, ignored bad targets exactly like the
    console, mid-tour death). Game-bound: a real tour on a disposable save
    (protocol section 10); coc targets vary by modlist. "Did my modlist
    change break anything" is now a scripted tour with evidence.
    **Lane G fully dispatched (G20-G22).**
23. ~~Utility pass~~ DONE 2026-07-17, three composable gaps closed in one
    sweep: **LandTexture schema** (13th, 14 fields incl. the GNAM grass
    list as bounded slots Grass0..Grass7, existing-entries-only writes;
    closes the grass loop opened by GRAS; 813 fields total).
    **FormChain** native (38th) + `reflect.chain` verb: the plugin override
    chain of ANY form, oldest first, winner last, scriptable ("which mod
    won this record"; the console UI tools only answer for a selected
    reference). **TextureInfo** native (39th) + `texture.info` verb:
    header-only texture inspection (container, format, dims, mips), the
    first question of every texture-pipeline debugging session;
    offline-proven by `tests/validate_texture_info.py`, 6/6. FormChain and
    the schema are engine-bound reads, compile-verified as always.
24. ~~Concave collision via convex decomposition~~ DONE 2026-07-17: closes
    F19's "convex cannot be concave" null the buildable way (MOPP stays
    Havok-SDK-bound; decode evidence in the investigation).
    `ConvexHull.cpp` gains `ConvexDecompose` (greedy volume-reducing binary
    splits with per-axis position search, degenerate-safe), and ModelCodec
    emits a `bhkListShape` of the piece hulls when `collisionPieces >= 2`.
    The bhkListShape layout was recovered byte-exact from real files
    (ms11unholyaltar n=5, daedricwaraxe n=2; 36+8N). Surfaces: ConvertModelEx
    gains a pieces arg; model.convert/model.spawn argInt high byte carries
    the piece count. Offline-proven: `tests/validate_convex_decompose.py`,
    18/18 (union covers every vertex, phantom notch collision 0.44->0.08 on
    an L-solid, container round-trip). The single-hull path stays
    byte-identical (F19 receipt still 19/19). Game-bound: walk a spawned
    decomposed hull (protocol section 11).
25. ~~Exact concave mesh collision (MOPP) investigation~~ DONE 2026-07-17,
    verdict in `docs/MOPP-INVESTIGATION.md`: BUILDABLE in two halves,
    neither of which is "reimplement Havok." Verified against primary
    sources that every open tool (NifMopp, Mopper, PyFFI) generates MOPP by
    wrapping Havok's own `hkpMoppUtility` via the free `mopper.exe`; no open
    builder OR interpreter exists, so a from-scratch reimplementation has no
    reference and a game-bound oracle (not worth it). The buildable path:
    (a) reverse `bhkCompressedMeshShapeData` and emit the mesh geometry
    ourselves (offline-provable; parsed a real 2886-byte block, layout
    recoverable, same discipline as C9/F19), (b) invoke the free `mopper.exe`
    for the one proprietary bytecode step (tool integration), (c) assemble
    the chain. This corrects the "MOPP out of scope" framing to "not yet,
    and here is exactly what it takes." The shipped convex decomposition
    remains the approximation for props; MOPP is for genuinely concave
    walkable statics. Next concrete lane if pursued: the
    bhkCompressedMeshShape builder (offline-provable half).
26. ~~Reverse bhkCompressedMeshShapeData~~ DONE 2026-07-17
    (`docs/CMSD-FORMAT.md`): the offline half of exact mesh collision, the
    geometry the engine narrowphase walks. A field-exact parser round-trips
    the WHOLE corpus byte-for-byte: 29,725 blocks across 28,474 NIFs, 100%
    exact-consume and 100% byte-exact re-serialize. Layout + invariants
    recovered (head 17/18/0x3FFFF/0x1FFFF, error=0.001 = the u16 quant step,
    materialType 1; per-chunk translation + u16 verts + list/strip indices +
    per-chunk SkyrimHavokMaterial). `src/core/CompressedMesh.{h,cpp}` builds
    a single-chunk CMSD from a mesh (u16-quantized, list triangles, chosen
    material, span-limited to 65.535 units/axis). Offline-proven:
    `tests/validate_cmsd.py`, 8/8 (real-file round-trip, builder decodes to
    input within 0.001 on cube/L/real-mesh, span refusal, source
    consistency). NOT yet wired to spawn: a CMSD alone does not collide, the
    chain needs a MOPP tree over it (the free niftools Havok tool, game-bound
    finalize; docs/MOPP-INVESTIGATION.md). Next: assemble the
    bhkCompressedMeshShape + bhkMoppBvTreeShape chain as a file-output option
    with the NifSkope "Update MOPP Code" finalize workflow.
27. ~~Assemble the mesh-collision chain + finalize workflow~~ DONE
    2026-07-17: exact concave mesh collision, file output. CMS (56 B) and
    MOPP (41-B header + buildType byte) layouts recovered byte-exact from
    real files. ModelCodec meshCollision mode emits the full chain: root ->
    collisionObject -> rigidBody -> bhkMoppBvTreeShape (EMPTY MOPP) ->
    bhkCompressedMeshShape -> CMSD, alongside the visual BSTriShape. Surfaces:
    ConvertModelMeshCollision native (41st), model.meshcollision verb.
    File-output ONLY (empty-MOPP chain would crash if live-spawned); the
    modder runs NifSkope "Update MOPP Code" to generate the MOPP over our
    geometry, the one proprietary step. Offline-proven:
    `tests/validate_mesh_collision_chain.py`, 9/9 (chain refs, empty-MOPP
    placeholder, CMSD decodes to input within 0.001). Convex path
    byte-unchanged (all 5 prior collision/model receipts still green). This
    completes exact mesh collision end to end except the game-bound MOPP
    finalize + walk-test (protocol section 13). We now emit the hard geometry
    the community's tools could never generate openly; NifSkope adds MOPP
    one-click.
30. **Skinned meshes bounded (2026-07-17, `docs/SKINNED-MESH-BOUND.md`).**
    Survey of 300 real skinned NIFs: the skin chain is four interlocking
    blocks (NiSkinInstance/BSDismemberSkinInstance, NiSkinData,
    NiSkinPartition) plus a skinned vertex stream. Layout-recoverable, but
    two things make it a real R&D lane unlike the static/texture/collision
    work: a source must be rigged to Skyrim's EXACT skeleton (generic glTF
    rigs do not map; retargeting is its own problem), and the only
    correctness oracle is in-game deformation on an animated actor. Deferred
    with the requirements documented; not built.
29. **Tree animation, two systems (clarified 2026-07-17).** Skyrim has two:
    (a) SpeedTree, the TREE form with OBJ_TREE/CNAM sway params + BaseTreeData
    branch bones, and (b) the Tree_Anim shader (BSLeafAnimNode + vertex-color
    weights), which ModelCodec's tree mode (F16) targets and which sways on a
    STAT via the shader, no form needed. The OBJ_TREE sway params are now a
    reflection schema (Tree, 14 fields) for tuning existing SpeedTrees'
    bend. TREE-form SPAWN of a converted mesh is NOT the fix for shader-sway
    (wrong system) and would need generated BaseTreeData bones (a real R&D
    lane, not built); if a Tree_Anim STAT does not sway in-game the lever is
    the shader/global wind, not the form. Honest bound, no risky feature
    shipped.
28. ~~Multi-chunk CMSD for larger meshes~~ DONE 2026-07-17: the
    bhkCompressedMeshShapeData builder no longer caps at 65.535 units. A mesh
    is split into chunks by triangle centroid on a 48-unit grid, each chunk
    quantizing its own u16 vertices; triangles that would break a chunk's
    span (or exceed it outright) escape to bigVerts/bigTris as exact floats,
    the wild blocks' own mechanism. Verified: bigTris/chunk material fields
    are table indices (0/1/2), not hashes. Terrain-scale statics now get
    exact collision. Offline-proven: `tests/validate_cmsd.py`, 10/10 (a
    200-unit grid -> 25 chunks, giant-triangle -> bigTris escape, EVERY input
    triangle covered, per-chunk span within u16, real-mesh rebuild). The
    mesh-collision chain + all model receipts unchanged.

**Lane E: close the honest nulls already on record.**
12. ~~Reverse `AELAS.dll`'s hooks / sun repositioning~~ DONE 2026-07-16 in two
    parts. The reversal: AELAS is a KiLoader/KiRELibSkyrim plugin (NOT
    CommonLibSSE), no usable Address Library IDs recoverable; its sun hook
    emits a Vector3 incident direction from the standard solar formula (spec:
    `C:\dev\protected\reverse-engineering\kitsuune-plugins-2026-07-16\AELAS-behavioral-spec.md`).
    The implementation: no blind binding; `SkyLighting::Update()` redirects
    `Sky->sun->sunBaseNode` (CommonLib's own typed member) along
    `SkyModel::ComputeSun`'s incident vector, preserving the node's own orbit
    radius, then refreshes via `NiAVObject::Update`. Gated by `[Sky] Enable` +
    `[Orbit] MoveSun`, both ship OFF. Azimuth path upgraded to the exact atan2
    form (the old acos + epsilon guard skewed noon azimuth ~0.2 deg). Math
    offline-proven: `tests/validate_sky_model.py`, 17 checks (closed-form
    sunrise/sunset, noon-altitude identity, azimuth quadrants, polar edges).
    HONEST NULLS (game-bound): the sky-domain radius fallback (5000) if the
    node sits at origin, whether the directional SHADOW follows the disc (vs
    only the visual sun), and the timing fight with vanilla `Sun::Update`
    (may need a post-`Sky::Update` write; that hook is the remaining RE).
13. In-game-validate and then default-enable `EnbLightInventoryFix` and
    `[Sky] Enable` once the operator confirms the look/behavior. (operator-gated)
    The acceptance procedure is delivered: `docs/VALIDATION-PROTOCOL.md` has
    exact toggles, log lines, steps, and pass criteria for all six
    OFF-by-default features, plus `tools/sb_command_client.py` (a zero-compile
    Python client) so the command channel is testable without a C++ toolchain.
    Every default stays OFF until the operator flips it.

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
