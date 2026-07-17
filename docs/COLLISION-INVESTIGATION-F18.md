# F18: convex-hull collision investigation (2026-07-16)

Question under investigation: can SkyrimBridge generate walkable collision
for converted meshes WITHOUT the Havok-SDK-bound MOPP path, using
`bhkConvexVerticesShape`? Method: parse real modlist NIFs offline; no
assumption survives unless the files show it.

## Verdict

**Feasible, green-light.** MOPP-free convex collision is routine shipping
practice, its byte layout is fully recovered and internally consistent, and
the one genuinely variable-size block (the shape itself) is arithmetic we
can generate. The rigid body is a constant-size block that can be templated
from a known-good static. Implementation is a bounded lane, not R&D.

## Findings, with receipts

**1. MOPP-free convex collision is common (survey of 6,000 NIFs).**
530 files use `bhkMoppBvTreeShape` (mesh collision), but 342 use
`bhkConvexVerticesShape`, and **323 of those have no MOPP block anywhere in
the file**. Props, clutter, and true statics (e.g. `deerskullstatic.nif`)
ship this way. The premise, previously labeled moderate, is confirmed.

**2. The block chain and byte layout (verified `consumed == blockSize` on
real files).**

```
root NiNode.collisionRef ─> bhkCollisionObject (10 bytes)
    target  i32   (the root node)
    flags   u16   (0x0081 observed)
    body    i32 ─> bhkRigidBody (250 bytes, constant size)
        shape   i32 ─> bhkConvexVerticesShape
        filter  layer u8 + flags u8 + group u16  (layer 1 = static observed)
        ... 242 bytes of motion/physics state (see finding 4)

bhkConvexVerticesShape (variable size, exact arithmetic):
    material  u32     (SkyrimHavokMaterial; 0x1DD9C611 = stone most common)
    radius    f32     (convex radius; observed 0.0 .. 0.05)
    2 x hkWorldObjCinfoProperty   (24 bytes, zeros in the wild)
    numVertices u32, vertices float4[] (xyz in Havok units, w = 0)
    numNormals  u32, normals  float4[] (unit face normal, w = plane d;
                                        all observed lengths exactly 1.0)
```

**3. The Havok scale constant.** Ratio of render-mesh extent to hull extent
across 17 measurable MOPP-free files: 15 cluster in **70.93 .. 82.0**,
approaching ~70 from above exactly as hull inset predicts (the hull is
tighter than render extremes, and the convex radius pads it). The floor at
70.93 is consistent with the community-documented constant (~69.97-69.99
game units per meter; the data cannot resolve the fourth digit, and a
0.03 percent error is invisible for collision). Two outliers (8.8, 22.1)
are mis-scaled ported assets, not counterexamples: `horkertusk.nif` (a
retexture-mod port) has a plain chain, node scale 1.0, and collision ~8x
too large for its render mesh; that defect class is exactly what in-game
validation catches instantly.

**4. The rigid body is template-viable.** All 40 sampled bodies are
`bhkRigidBody` of exactly 250 bytes (plus 4 samples of a 254-byte variant,
not needed). Bytes differing across samples sit in the shape ref, the
filter, and the mass/inertia/motion region, which a FIXED static body does
not exercise. Generation strategy: embed the 250 bytes of a known-good
static's body (`deerskullstatic.nif`, layer 1) verbatim and patch the shape
ref. This is the same byte-template philosophy the C9 mesh writer used, and
it is why the C9 output is engine-loadable by construction.

**5. Defaults observed in the wild:** material `0x1DD9C611` (stone) is the
most common on convex shapes; filter layer 1 (static) on true statics,
4 (clutter) on movable props; convex radius 0..0.05.

## Generation recipe (the follow-on implementation lane)

1. Quickhull over the mesh vertices in NIF units (well-bounded, ~150 lines).
2. Scale by 1/69.99 into Havok units; emit `float4` vertices, w = 0.
3. Emit deduplicated face planes: unit normal + d = -(n . p).
4. `bhkConvexVerticesShape`: stone material, radius 0.05, zeroed cinfo
   properties.
5. `bhkRigidBody`: 250-byte static template, patch shape ref (+ layer 1).
6. `bhkCollisionObject` (target = root, flags 0x0081) referenced from the
   root node's collisionRef; add `BSXFlags` with the havok bit as real
   statics carry.
7. Surface as an option on ConvertModel/SpawnModel; OFF by default until
   the operator walks against a generated hull in-game (scale and
   walkability are the acceptance test; finding 3's outliers show exactly
   what failure looks like).

Honest limits: a single convex hull cannot be concave (an archway's opening
fills in); that is inherent to the shape class and the reason mesh-accurate
collision (MOPP) remains out of scope. For props, rocks, furniture-scale
statics, and anything a player mostly walks around rather than through,
convex is the right 80 percent.

Analysis scripts (session scratchpad, re-runnable): `survey_collision.py`,
`parse_convex_collision.py`, `pin_scale_and_body.py`, `probe_outlier.py`.
