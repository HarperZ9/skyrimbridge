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

### Registered schemas (788 fields total)

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
| DDS BC1/DXT1 | yes | yes | decode: both 3- and 4-color modes; encode: opaque |
| DDS BC3/DXT5 | yes | yes | decode honors the always-4-color rule; encode carries alpha |
| DDS BC7 / DX10 / cubemaps / volumes | no | no | honest nulls |

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
cgf "SkyrimBridge.ConvertTextureFmt" "in.png" "out.dds" "BC3"  ; BC1 | BC3 | RGBA8
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

Both harnesses are faithful Python ports of the C++ and run offline against
the real files in the modlist.

---

## 4. Native API reference (30 natives, script name `SkyrimBridge`)

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
| TextureScanNow | `int (bool dryRun)` — converted (or would-convert) count |

---

## 5. Config dialect (SBConfig)

One flat-INI grammar everywhere: `[Section]`, `Key = Value`, `;` comments,
`R,G,B[,A]` tuples, hex form IDs (`0x10A232`), `[Base:Qualifier]` override
suffix. Parser: `src/core/SBConfig.h`. No third-party config formats in the
runtime path; importers translate legacy files one-time where compatibility
is needed.
