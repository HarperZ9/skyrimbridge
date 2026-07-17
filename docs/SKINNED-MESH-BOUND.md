# Skinned meshes: scope bound (2026-07-17)

Why ModelCodec emits static `BSTriShape` only, and exactly what skinned
(animated, skeleton-deformed) mesh import would take. Grounded in a survey
of 300 real skinned NIFs, not guessed.

## Verdict

**A real multi-session R&D lane, and the hardest part is not the byte
format.** The skin blocks are layout-recoverable like everything else here,
but two things make skinned import fundamentally different from the static,
texture, and collision work: a source mesh must be rigged to Skyrim's exact
skeleton to be usable at all, and the only correctness oracle (does it
deform correctly with the animated skeleton) is entirely in-game. Not built;
bounded here so the decision is informed.

## What the survey shows (300 skinned NIFs)

The skin chain is four interlocking blocks, not one:
- `NiSkinInstance` (318) / `BSDismemberSkinInstance` (401) - the skin binder;
  bodies and armor use the dismember variant, which carries body-part slot
  partitions (the dismemberment/biped-slot system).
- `NiSkinData` (719) - the bone list, per-bone bind-pose transforms, and
  per-vertex bone weights.
- `NiSkinPartition` (719) - the hardware-skinning partitions: a bone palette
  per partition, up to 4 bones per vertex, triangle strips per partition,
  and the skinned vertex stream.
- Skinned-shape vertexDesc differs from our static `0x...07650408`: skinned
  shapes carry bone indices + weights in the vertex (and `BSDynamicTriShape`
  for morphable meshes), so the 32-byte static vertex is not the layout.

## What a builder would require

1. **glTF skin import**: read `JOINTS_0` / `WEIGHTS_0` accessors, inverse
   bind matrices, and the joint node hierarchy. (OBJ has no skinning; glTF
   only.) Tractable.
2. **Skeleton mapping - the real blocker**: Skyrim skinning references bones
   by exact name (`NPC Root`, `NPC Spine`, `NPC L UpperArm`, ...). A generic
   glTF skeleton will not match, so a source mesh has to have been authored
   FOR Skyrim's skeleton (as body/armor mods are). Auto-retargeting an
   arbitrary rig to Skyrim's is its own hard problem, out of scope. So the
   realistic feature is "import a mesh already rigged to Skyrim's skeleton,"
   not "rig any mesh."
3. **NiSkinData / NiSkinPartition generation**: bind poses, per-vertex
   weight normalization to <=4 bones, partitioning into bone-palette groups
   within the hardware bone limit, strip generation per partition. Sizable
   but layout-recoverable byte work.
4. **BSDismemberSkinInstance** for bodies: assign triangles to biped-slot
   partitions. Needs slot intent from the source (a convention or metadata).
5. **Skinned vertex stream**: the enlarged vertex with bone indices/weights;
   the vertexDesc bits differ per case (recoverable from the survey).

## Honest bound

Steps 1, 3, 5 are ordinary byte-format reversal (the survey already locates
the blocks). Step 2 is the wall: without a Skyrim-rigged source there is
nothing to import, and retargeting is a separate research problem. And every
result is game-bound - the only test of correct deformation is loading it on
an animated actor in-game. That combination (skeleton prerequisite + purely
in-game oracle) is why skinned import is deferred while static meshes,
textures, and collision - which are offline-provable and skeleton-free - are
done. The reflection spine already exposes related records; the mesh side of
skinning is the open R&D.
