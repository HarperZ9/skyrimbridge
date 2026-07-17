# Exact concave mesh collision (MOPP): investigation and verdict (2026-07-17)

Follow-on to `COLLISION-INVESTIGATION-F18.md`. Question: can SkyrimBridge
generate EXACT concave mesh collision (not the convex-decomposition
approximation), i.e. a `bhkMoppBvTreeShape` over a `bhkCompressedMeshShape`?
Method: decode the real blocks on disk; verify the tool landscape against
primary sources; no claim without evidence.

## Verdict

**Buildable, in two halves, and neither half is "reimplement Havok."** The
mesh-geometry half (`bhkCompressedMeshShape` + its data block) is a real
offline-provable reversal lane, same discipline as C9/F19. The acceleration
half (the MOPP bytecode) is obtained the way the entire community obtains
it: from Havok's own code via the free niftools `mopper.exe`, a tool
integration, not a reimplementation. The one thing that stays genuinely
not-worth-doing, now with evidence, is reimplementing Havok's MOPP builder
from scratch.

This corrects the earlier "MOPP is out of scope" framing. It is not
impossible and not forbidden. It is: one proprietary step that a free
wrapper tool already performs, plus one sizable-but-ordinary byte-layout
reversal that we do ourselves.

## Findings, with receipts

**1. The MOPP builder is proprietary and has never been openly
reimplemented (verified against primary sources).**
Every open tool that produces MOPP wraps Havok's `hkpMoppUtility`:
- niftools `mopper.exe` carries the explicit notice "Mopper uses Havok(R).
  (C)Copyright 1999-2008 Havok.com Inc."
- NifSkope's "Update MOPP Code" shells out to `NifMopp.dll` / `mopper.exe`.
- PyFFI's `pyffi.utils.mopp` only GENERATES (by calling `mopper.exe`); it
  does not parse or interpret. There is no open MOPP interpreter either.

So the offline-oracle idea (write an interpreter, differential-test a
builder against it) has no public opcode reference to stand on: the ~30
opcodes would themselves have to be reversed from a handful of bytecode
samples with no ground-truth decoder. That is the multi-week effort with a
game-bound oracle, and it duplicates what a free tool already does.

**2. The MOPP bytecode is real and opaque (decoded at the header level).**
Two real blocks (`sovspit01.nif`, `dlc2telmithryndiseased01.nif`):
1302 and 2201 bytes of BV-tree VM bytecode, with a header of `origin`
(Vector3), `scale`, and `moppDataSize`. The opcode byte histogram
(`0x00`, `0x04`, `0x52`, `0x26`, `0x10`, `0x23`...) is consistent with a
tree of scaled split-plane branch nodes and triangle-index terminals, but
the exact dialect is undocumented publicly.

**3. The mesh-geometry half IS layout-recoverable (parsed a real block).**
`bhkCompressedMeshShapeData` in `sovspit01.nif` is 2886 bytes and parses:
head words `bitsPerIndex=0x11`, `0x12`, masks `0x3ffff`/`0x1ffff`, then the
bounds/origin floats (`-0.755, -5.714, 1.265`, the SAME origin the MOPP
block carries), then the chunk / big-vertex / transform / material arrays.
It is one of the more complex NIF blocks (compressed u16 vertices, chunk
strips, per-chunk transforms and materials), so building it is a sizable
lane, but it is ordinary byte-layout work of exactly the kind C9 and F19
already did against real files. No proprietary step.

**4. The convex-decomposition path (shipped) is the right approximation.**
For props, rocks, furniture, and anything the player mostly walks around
or through a single opening, the `bhkListShape` of convex pieces
(`ConvexDecompose`, 18/18 offline) is correct and needs no Havok tool. MOPP
matters only for genuinely concave *walkable* mesh collision (interiors,
terrain-like statics, complex architecture).

## The buildable lane, if pursued

1. **bhkCompressedMeshShape builder** (offline-provable): reverse
   `bhkCompressedMeshShapeData` fully from the real blocks
   (`sovspit01`, `dlc2telmithryn*`), emit the mesh geometry from our
   triangles, validate byte-round-trip against a real file with an
   independent parser. This is the real work and it is in-scope.
2. **MOPP generation** (tool integration): invoke the free niftools
   `mopper.exe` / `NifMopp.dll` on the compressed mesh to produce the
   bytecode + origin + scale. Operator-provided binary; the same step
   NifSkope automates. Not a reimplementation.
3. **Assemble** the `bhkMoppBvTreeShape` -> `bhkCompressedMeshShape` chain
   under the templated rigid body, exactly as the convex path assembles its
   chain today.

Honest bound: step 1 is multi-session (the data block is complex). Step 2
depends on a third-party tool being present and is game/tool-bound for
validation. Until both land, the convex decomposition is the shipped answer
and its limits (approximate, possible seams) are stated at every surface.

## Why this is the honest answer to "why is anything out of scope"

Almost nothing is. The DRM-packed exe is the one true wall (encrypted on
disk; routed around via CommonLib). Everything else labeled "out of scope"
was prioritization: BC4/BC5 are simpler than the shipped BC7; skinned
meshes are a known layout; and MOPP, the hardest, turns out to be a free
tool plus a byte-layout reversal we already know how to do. "Out of scope"
meant "not yet, and here is exactly what it takes," not "cannot."

