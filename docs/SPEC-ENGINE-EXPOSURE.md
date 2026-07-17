# SkyrimBridge engine-exposure specification

The reference sheet for the engine-exposure surface: the record-reflection
layer, the region walker, the texture codec, and every console-callable
native. Counts and behaviors on this page are verified against the source at
the commit that ships them; the validation harnesses named here are in-repo
and re-runnable.

Scope note. "Lossless" always means: over the persistent, schema-defined
fields, witnessed by a round-trip verifier. Derived and cached engine state
(live pointers, handles, temporaries) is deliberately outside every schema.

---

## 1. EngineReflect: schema-driven record reflection

`src/core/EngineReflect.{h,cpp}`

A runtime schema (field name, type, get/set accessors) over CommonLibSSE-NG's
typed engine structs, with four generic operations:

| Op | Meaning |
|---|---|
| Read | form -> ordered (name, value) tree |
| Write | tree -> form, only registered fields, type-checked |
| Translate | tree <-> flat INI (the SBConfig dialect) |
| Verify | Read -> INI -> parse -> compare: witnesses the serialization round-trip |

Value kinds: `float`, `int`, `bool`, `color3`, `color4`, `formlink`, `string`.
Form links resolve through `TESForm::LookupByID` on write; an unresolvable ID
is a no-op, never a wild pointer.

### Registered schemas (799 fields total)

| Schema | Form type | Fields | Covers / notable exclusions |
|---|---|---|---|
| ImageSpace | IMGS | 17 | HDR, cinematic, tint, DoF |
| Volumetric | VOLI | 12 | intensity, color, density, phase, sampling |
| LightingTemplate | LGTM | 14 | ambient/directional/fog colors + distances |
| Weather | WTHR | 487 | full record: 17x4 colors, fog, 32 cloud layers, 4x directional ambient cubes, DATA bytes, form links |
| Climate | CLMT | 8 | sunrise/sunset timing, volatility, moon phase, sun textures |
| Region | REGN | 3 | worldspace/current-weather links, emittance (subrecords: see RegionWalker) |
| Light | LIGH | 14 | DATA block, fade, emittance, sound/lens-flare links |
| Water | WATR | 66 | full DNAM shader block, noise, velocities, textures, links; unnamed (`unkXX`) members excluded, not guessed |
| EffectShader | EFSH | 95 | fill/edge/particle/color keys/holes/addon + textures + links; membrane and particle blend modes excluded (D3DBLEND is only forward-declared in CommonLib) |
| ImageSpaceModifier | IMAD | 41 | duration + HDR/cinematic mult-add pairs first-class; fields CommonLib types as raw `uint32` (tint, blurs, DoF) exposed as raw Ints, suffixed `Raw`, never lossy-decoded |
| WorldSpace | WRLD | 31 | climate/water/lighting/music links, map framing, land/water heights, textures; runtime containers (cell maps) excluded by design |
| Grass | GRAS | 11 | model path + full DATA block: density, slopes, water distance/state, position/height/color ranges, wave period, flags. Placement (which land textures grow it) lives on TESLandTexture, not GRAS |

Adding a record type is one schema block using the field macros
(`RF_F`, `RF_INT`, `RF_B`, `RF_FLAGS`, `RF_S`, `RF_LINK`, `RF_C3F`, `RF_C3B`)
or the Weather-style programmatic builder for indexed arrays.

### The console loop

```
cgf "SkyrimBridge.EngineReflectList" 0          ; all schemas -> dumps/schema-schemas.txt
cgf "SkyrimBridge.EngineReflectList" 0x10A232   ; that form's fields
cgf "SkyrimBridge.EngineReflectDump" 0x10A232   ; -> dumps/0010A232.ini
;   ... edit the INI ...
cgf "SkyrimBridge.EngineReflectApply" 0x10A232  ; write back, returns fields written
cgf "SkyrimBridge.EngineReflectVerify" 0x10A232 ; round-trip witness (non-mutating)
cgf "SkyrimBridge.EngineReflectVerifyStrict" 0x10A232 ; write-back idempotence witness (MUTATES: opt-in)
```

Dumps live under `Data/SKSE/Plugins/SkyrimBridge/dumps/`.

---

## 2. RegionWalker: structured region subrecords

`src/core/RegionWalker.{h,cpp}`

Regions keep their interesting state in polymorphic `TESRegionData`
subrecords, which a flat schema cannot express. The walker dumps every
subrecord CommonLib types:

- **Weather** — the region weather list: `WeatherChance<i> = <weather>, <chance>, <global>`
- **Sound** — music link + `Sound<i> = <sound>, <flags>, <chance>`
- **Map** — map name
- **Land** — icon texture
- Objects, Grass, Imposter are listed by type and priority only: CommonLib has
  no layout for them, and this spec does not pretend otherwise.

Writes are bounded to one operation: setting the chance of an existing
weather entry. No list add/remove, no pointer surgery.

```
cgf "SkyrimBridge.RegionDump" 0x<region>                     ; -> dumps/<id>.region.ini
cgf "SkyrimBridge.RegionSetWeatherChance" 0x<region> 0x<weather> 40
cgf "SkyrimBridge.RegionApply" 0x<region>                    ; chance edits from the dump file
```

---

## 3. TextureCodec: foreign-format texture pipeline

`src/core/TextureCodec.{h,cpp}` + `src/core/Inflate.{h,cpp}` (pure, zero-dep)

### Format matrix

| Format | Read | Write | Notes |
|---|---|---|---|
| PNG | yes | no | all 5 color types; 1/2/4/8/16-bit (16 narrows via high byte); all 5 filters; Adam7; tRNS. From-scratch DEFLATE with verified Adler-32 + per-chunk CRC-32 |
| TGA | yes | yes | read: uncompressed + RLE, 24/32-bit; write: 32-bit uncompressed top-origin |
| BMP | yes | no | 24/32-bit, top-down and bottom-up |
| DDS RGBA8 | yes | yes | uncompressed 32-bit byte-aligned masks (RGBA/BGRA) |
| DDS BC1/DXT1 | yes | yes | decode: both 3- and 4-color modes; encode: opaque. Legacy or DX10 header |
| DDS BC3/DXT5 | yes | yes | decode honors the always-4-color rule; encode carries alpha. Legacy or DX10 header |
| DDS BC7 (DX10) | yes | yes | decode: all 8 modes per the D3D11 spec (tables machine-extracted from DirectXTex); encode: mode 6 baseline, DX10 header |
| DDS DX10 RGBA8/BGRA8 | yes | no | byte-order formats (dxgi 28/29, 87/91) |
| DDS BC4 / BC5 / BC6H / cubemaps / volumes / arrays | no | no | honest nulls; sRGB payloads pass through unconverted |

DDS read decodes the top mip. DDS write appends an optional clamp-edge 2x2
box-filter mip chain down to 1x1 (default on in the conversion natives).

BC encoder tier, stated plainly: baseline. Endpoints are the block's two most
distant colors, indices nearest-palette. Good for utility conversion at
39-41 dB PSNR on real content; it is not a cluster-fit art-pipeline encoder.

The BC decode arithmetic was locked empirically against an independent
decoder (Pillow's native BCn) before implementation: truncating `(2a+b)/3`
and `(a+b)/2` color interpolation, truncating `/7` and `/5` alpha ramps.

### Conversion natives

```
cgf "SkyrimBridge.ConvertTexture" "in.png" "out.dds"        ; by output extension
cgf "SkyrimBridge.ConvertTexture" "in.dds" "out.tga"        ; DDS -> editable lane
cgf "SkyrimBridge.ConvertTextureFmt" "in.png" "out.dds" "BC3"  ; BC1 | BC3 | BC7 | RGBA8
cgf "SkyrimBridge.ConvertTextureFoliage" "in.png" "out.dds" "BC3" 128
;   coverage-preserving mips for alpha-tested foliage: each mip's alpha is
;   rescaled so the fraction of texels passing the alpha test stays at the
;   top level's coverage (box-filter mips otherwise thin leaves/grass to
;   nothing at distance). threshold <= 0 -> 128. Also available to the
;   background converters: [TextureConvert] CoverageMips / CoverageThreshold.
cgf "SkyrimBridge.TextureScanNow" true                      ; dry-run the tree scan
```

### Automatic integration (both `[Native]`-gated, ship OFF)

- **TextureAutoConvert** — background scan at data-load: every
  `textures\*.png/.tga/.bmp` without a `.dds` sibling is transcoded next to it
  (additive; an existing `.dds` is never overwritten unless
  `[TextureConvert] Refresh` and the source is newer).
- **TextureLoadHook** — in-flight substitution. Vtable detours over
  `BSResource::LooseFileLocation`'s stream/async/info functions: a missing
  `textures\*.dds` whose foreign sibling exists is transcoded once into
  `SkyrimBridge/texcache/` and the original engine function is re-invoked on
  the cache path, so sync and async I/O stay engine-native. The detours act
  only after the original call already failed, and any internal failure
  returns the original error unchanged. A loose foreign texture overrides a
  BSA `.dds`, same as a loose `.dds` would.

### Validation receipts (in-repo, re-runnable)

- `tests/validate_png_codec.py` — 77 checks. Inflate byte-exact vs zlib on
  real ENB IDAT streams (incl. a 33.7 MB stream) and synthetic
  stored/fixed/dynamic blocks; PNG pixel-exact vs PIL on 40 real ENB PNGs plus
  synthetic and hand-built Adam7/16-bit/colorkey cases; DDS mip structure and
  box-filter math; checksums vs binascii.
- `tests/validate_bcn_codec.py` — 25 checks. BC decode exact vs PIL on
  hand-built blocks (every mode) and real modlist DXT1/DXT5 textures; encode
  output decodes identically under PIL and our model; PSNR floor; lossless
  round-trip on exactly-representable blocks; TGA layout via PIL read-back.
- `tests/validate_tree_wind.py` — 22 checks. The empirical mapping asserted
  on real animated trees (grayscale weights in [127,255], rising with
  distance from base, Tree_Anim bit 29, constant per-shape alpha,
  BSLeafAnimNode roots, vanilla's all-255 minimal config), plus the
  generator port (monotone, 128..255, deterministic) and source-consistency
  checks that the shipped ModelCodec.cpp carries exactly the derived values.
- `tests/validate_foliage_mips.py` — 10 checks. Demonstrates the defect first
  (box-filter mips collapse a sparse synthetic foliage's alpha-test coverage
  to 0.000, and real modlist foliage decays measurably), then proves the fix:
  every mip holds the top level's coverage within quantization tolerance,
  never thinner, deterministic, opaque/transparent edge cases pass through,
  and the property survives BC3 alpha quantization (the shipping format).
- `tests/validate_bc7_codec.py` — 92 checks. The partition/anchor tables are
  parsed out of the shipped TextureBC7.cpp at run time, so the compiled data
  is what gets verified. Per-mode random-block fuzz (all 8 modes) byte-exact
  vs PIL; 70+ real modlist BC7 textures byte-exact vs PIL; DX10-rewrapped
  BC1/BC3 and RGBA8 vs PIL (BGRA8 vs by-construction swizzle: PIL lacks
  dxgi 87); cubemap/array refusal; reserved mode -> transparent black;
  mode-6 encoder lossless on exactly-representable blocks, anchor-swap
  exact, PIL-agreed output, 54 dB PSNR measured on the sampled content.

Both harnesses are faithful Python ports of the C++ and run offline against
the real files in the modlist.

---

## 4. ModelCodec: foreign-model pipeline (OBJ / glTF -> NIF)

`src/core/ModelCodec.{h,cpp}`. Parses OBJ and glTF/GLB (zero-dep JSON reader;
float POSITION/NORMAL/TEXCOORD, u16/u32 indices; external, embedded, and
base64 buffers) and emits a Skyrim SE NIF (20.2.0.7 / user 12 / stream 100):
`BSFadeNode -> BSTriShape + BSLightingShaderProperty + BSShaderTextureSet`.
The byte layout reproduces a real shipping SSE static mesh field-for-field
(vertexDesc `0x0003B00007650408`, 32-byte full-precision vertex, particleData
trailer). Normals (area-weighted) and tangents (Lengyel) are computed when the
source lacks them.

Validation receipt: `tests/validate_model_codec.py` (21 checks) re-parses the
emitted NIF with an independent reader, round-trips geometry full-float exact,
and consumes a real modlist BSTriShape byte-for-byte.

**Runtime path:** `SpawnModel` materializes a foreign mesh (or copies a .nif)
under `meshes\SkyrimBridge\spawn\`, creates a dynamic Static form pointing at
it, and places one reference at the player, so the engine's own model loader
constructs the NiObject graph. Building BSTriShape graphs synthetically in
memory is a deliberate non-goal, the same architecture rule as the
texture-load hook: the engine constructs its own objects, we hand it files it
natively loads. The dynamic form and reference persist in the save (test on a
disposable save; remove via console `markfordelete`).

**Tree mode** (`ConvertModelTree`, or `argInt = 1` on the model verbs): the
converter paints procedural wind-sway weights into the vertex colors, sets
`SLSF2_Tree_Anim`, and emits a `BSLeafAnimNode` root. The mapping is derived
empirically from real animated tree assets and asserted by the receipt:
grayscale R=G=B weight, 127/128 at the trunk base rising to 255 at canopy
extremities (weight = `0.5 + 0.5 * e^1.5`, e = normalized distance from the
trunk base), vertex alpha 255 (the vanilla-accepted constant; the aspens
ship a constant 68 whose semantics are an unrecovered honest null).

Honest nulls: no skinned/animated meshes, first primitive/group only, no
collision (bhk*) generation, no Draco/sparse glTF; a spawned mesh's material
is only as good as what the source carried (an untextured OBJ renders flat);
whether a STAT-placed reference sways (vs needing a TREE form) is game-bound.

---

## 5. Native API reference (34 natives, script name `SkyrimBridge`)

Console form: `cgf "SkyrimBridge.<name>" <args...>`

**State reads** (from the tracker pipeline)
| Native | Signature |
|---|---|
| IsActive | `bool ()` |
| GetFloat | `float (string param, int component)` |
| GetGameHour | `float ()` |
| GetQualityScale | `float ()` |
| GetWeatherFormID | `int ()` |
| IsInterior | `bool ()` |
| GetParamCount | `int ()` |

**Weather workshop**
| Native | Signature |
|---|---|
| CaptureWeather / ApplyWeather / RevertWeather | `void ()` |
| SaveWeatherPreset / LoadWeatherPreset | `bool (string name)` |
| SetWeatherCompare | `void (bool original)` |
| ForceWeatherByID | `void (int formID)` |
| ClearForcedWeather | `void ()` |

**KreatE profiles**
| Native | Signature |
|---|---|
| LoadKreateProfile | `int (string name)` |
| KreateProfileCount | `int ()` |
| KreateProfileNameAt | `string (int index)` |
| ActiveKreateProfile | `string ()` |

**EngineReflect**
| Native | Signature |
|---|---|
| EngineReflectDump | `string (int formID)` — dump path, `""` on failure |
| EngineReflectApply | `int (int formID)` — fields written |
| EngineReflectVerify | `int (int formID)` — fields on success, 0 on failure |
| EngineReflectVerifyStrict | `int (int formID)` — MUTATES (write-back witness) |
| EngineReflectList | `int (int formID)` — 0 = schemas; else that form's fields |

**RegionWalker**
| Native | Signature |
|---|---|
| RegionDump | `string (int regionID)` |
| RegionSetWeatherChance | `int (int regionID, int weatherID, int chance)` |
| RegionApply | `int (int regionID)` |

**TextureCodec**
| Native | Signature |
|---|---|
| ConvertTexture | `bool (string in, string out)` |
| ConvertTextureFmt | `bool (string in, string out, string fmt)` |
| ConvertTextureFoliage | `bool (string in, string out, string fmt, int threshold)` — coverage-preserving mips (BC3/BC7/RGBA8) |
| TextureScanNow | `int (bool dryRun)` — converted (or would-convert) count |

**ModelCodec**
| Native | Signature |
|---|---|
| ConvertModel | `bool (string in, string out)` — OBJ/glTF/GLB in, NIF out |
| ConvertModelTree | `bool (string in, string out)` — tree mode: wind vertex colors + Tree_Anim + BSLeafAnimNode root |
| SpawnModel | `int (string in)` — convert + place at the player via the engine loader; returns the ref's FormID (MUTATES the save) |

---

## 6. Config dialect (SBConfig)

One flat-INI grammar everywhere: `[Section]`, `Key = Value`, `;` comments,
`R,G,B[,A]` tuples, hex form IDs (`0x10A232`), `[Base:Qualifier]` override
suffix. Parser: `src/core/SBConfig.h`. No third-party config formats in the
runtime path; importers translate legacy files one-time where compatibility
is needed.

---

## 7. External command channel (`[Native] CommandSurface`, ships OFF)

`src/SB_CommandLayout.h` (the ABI contract, dependency-free; include it from
any client) + `src/core/BridgeCommand.{h,cpp}` (plugin side). Clients:
`tools/SkyrimBridgeClient.h` (header-only C++),
`tools/sb_command_client.py` (zero-compile CLI), and
`tools/blender/skyrimbridge_push.py` (Blender push-to-game addon: export the
selection, `model.spawn`, see it at the player). A second shared-memory
region, `SkyrimBridge_Command`, alongside the one-way game-state region, so an
external tool drives the engine surface without the in-game console.

**Protocol** (single-slot, sequence-gated, one in-flight request):
1. Client fills `verb` + `arg0` + `arg1` + `argInt`, then publishes
   `requestSeq` last.
2. The plugin dispatches at most one pending request per frame on the game
   thread (SEH-isolated, same fault budget as the frame update), writes
   `status` + `resultInt` + `resultText`, then publishes
   `responseSeq = requestSeq` last and signals the `SkyrimBridge_CommandReady`
   event.
3. Client waits until `responseSeq` equals its `requestSeq`, then reads.

The block is 5184 bytes, `#pragma pack(1)`, guarded by a `static_assert`;
`magic` = `'SBC1'`, `version` = 1. Status codes: `0` ok, `-1` unknown verb,
`-2` bad argument, `-3` not found, `-4` failed. Large payloads (a full Weather
dump) travel through the dumps directory exactly as the console natives do;
the mailbox carries the trigger and a bounded 4 KiB text result.

**Verb table**
| Verb | Request | Response |
|---|---|---|
| `ping` | — | `resultInt` = 1, `"pong"` |
| `reflect.list` | `arg0` = `"0"` or `"0x<formid>"` | schema or field listing |
| `reflect.dump` | `arg0` = `"0x<formid>"` | INI text (also written to `dumps/<id>.ini`), `resultInt` = lines |
| `reflect.apply` | `arg0` = `"0x<formid>"`; client edits `dumps/<id>.ini` first | `resultInt` = fields written |
| `reflect.verify` | `arg0` = `"0x<formid>"`, `argInt` = 1 for strict (MUTATES) | `resultInt` = fields, 0 on failure |
| `region.dump` | `arg0` = `"0x<region>"` | subrecord dump (also `dumps/<id>.region.ini`) |
| `region.weather` | `arg0` = region, `arg1` = weather, `argInt` = chance | `resultInt` = entries edited |
| `texture.convert` | `arg0` = in, `arg1` = out, `argInt` = 0/1/2/3 (RGBA8/BC1/BC3/BC7) | `resultInt` = ok |
| `texture.foliage` | `arg0` = in, `arg1` = out, `argInt` = fmt \| (threshold << 8), threshold 0 -> 128 | coverage-preserving mips; `resultInt` = ok |
| `texture.scan` | `argInt` = 1 dry / 0 live | `resultInt` = converted, counts in text |
| `model.convert` | `arg0` = in, `arg1` = out, `argInt` = 1 for tree mode | `resultInt` = ok |
| `model.spawn` | `arg0` = in (foreign mesh or .nif), `argInt` = 1 for tree mode | `resultInt` = placed ref FormID as an int32 bit pattern (0xFFxxxxxx arrives negative; mask with 0xFFFFFFFF). MUTATES the save |

Validation receipt: `tests/validate_command_protocol.py` (15 checks) verifies
the 5184-byte ABI layout field-for-field against a Python `ctypes` mirror and
runs a 200-round sequence-gated round-trip against a simulated dispatcher (no
stale reads, no torn results, monotonic sequences). The C++ dispatch itself is
game-bound: enable `[Native] CommandSurface`, run a client `ping`, and check
the log for `BridgeCommand: command channel active`.
