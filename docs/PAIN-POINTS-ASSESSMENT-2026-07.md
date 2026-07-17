# Community pain-point assessment (2026-07, lane G)

Method: domain knowledge cross-checked against current community discussion
and the tool landscape (web survey 2026-07-16), then against what this
modlist itself ships (every installed mod is a revealed pain point). The
column that matters is the last one: what has genuinely never been built.

| Category | The pain | Existing tooling | The unbuilt gap |
|---|---|---|---|
| Performance archaeology | "Which cell/refs/lights tank my FPS" is manual CK/xEdit spelunking guided by forum lore; shadow-casting lights are the classic killer (engine renders ~4 shadow lights at once, and every one is expensive) | INI shadow tweaks, per-area patch mods (e.g. the ELFX/SMIM fps patches), ENB profiler (frame-level only, no attribution) | **A one-command in-game cell census with plugin attribution**: refs by type, shadow lights ranked by distance, per-plugin ref counts. Nothing does this live. -> G20, BUILT below |
| Script load / save health | Script-heavy saves degrade; diagnosis is post-mortem (load the save in ReSaver after the damage) | ReSaver/Fallrim (offline), Papyrus INI logging (spammy, offline) | **Live Papyrus VM stats over an external channel** (running/suspended stacks, top script instance counts). CommonLib exposes the VM internals; API risk moderate. -> G21, next |
| Runtime conflict identity | "Which plugin won this record" in-game | More Informative Console (selected ref, UI only), xEdit (offline) | Scriptable full override chain (form -> ordered source files) over the channel; cheap on EngineReflect. Folded into G20 (attribution) + a future reflect verb |
| Dark face / facegen | Facegen mismatch shows black faces | Face Discoloration Fix (runtime regen) largely SOLVES it; CK Ctrl+F4 workflow | A proactive detector (name culprits before you meet them) is possible but the fix mod has drained most of the pain. Low priority |
| Textures | conversion, compression, foliage mips | texconv/CAO offline | Largely closed BY THIS PROJECT (B5-B8, F15); live re-load of an already-loaded texture remains open (engine cache RE) |
| Meshes / collision | import, collision authoring | Blender+nifly (fiddly), 3ds Max Havok (paywalled/ancient) | Largely closed BY THIS PROJECT (C9, F14, F19); concave/MOPP stays out of scope |
| Trees / grass authoring | wind painting, GRAS plumbing | hand-painting in Blender | Closed BY THIS PROJECT (F16, F17); TESLandTexture (placement) is the remaining one-block add |
| Grass cache | NGIO precache is slow and crash-prone | NGIO | A resumable/robust cache is NGIO's internal domain; poor fit for us |
| Modlist regression testing | "did my change break anything" = launch and wander | nothing composable | **Automated smoke tour**: external driver walks the game through N cells via the command channel, collecting log/crash evidence. Genuinely unbuilt; needs teleport + status verbs. -> G22, candidate |
| Load order / conflicts (static) | plugin conflicts, dependencies | LOOT/xEdit/Wrye Bash mature | No gap worth entering |
| LOD | generation complexity | DynDOLOD ecosystem | Its own universe; integration point only |
| Animations | behavior generation, T-pose | Nemesis/Pandora | Mature, no gap for us |
| Crash diagnosis | cryptic logs | Crash Logger, Trainwreck, online analyzers | Attribution exists; incremental at best |

Sources consulted: STEP forum shadow-optimization threads, the ELFX SMIM fps
patch page, Nexus Face Discoloration Fix / dark-face guides, r/skyrimmods
recurring-frustration threads (compatibility, load order, script-heavy saves,
clean removal).

## Verdict and build order (lane G)

The two highest-value unbuilt fixes both ride infrastructure this project
already has (the command channel, EngineReflect, dumps):

- **G20 cell performance census** (`CellReport` native + `cell.report`
  verb): BUILT 2026-07-16 late. Read-only, SEH-inherited from the dispatch
  context, plugin-attributed. Turns a forum-lore manual hunt into one
  console command.
- **G21 live Papyrus VM monitor**: next; moderate CommonLib API risk,
  read-only, same channel surface.
- **G22 modlist smoke tour**: the driver composes existing pieces plus a
  teleport verb; scope it after G21.

Honest scope: G20 counts and attributes; it does not measure GPU cost
directly (no per-draw timing). The shadow-light list is the proven proxy the
community already hunts by hand.
