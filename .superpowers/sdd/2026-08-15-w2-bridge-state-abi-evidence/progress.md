# SDD ledger — plan: docs/superpowers/plans/2026-08-15-w2-bridge-state-abi.md

Branch: `feat/bridge-state-abi`
Base: `3757cc4` (plan cherry-picked as `56197e3`)
Spec context: `C:/dev/truth-enb/docs/superpowers/specs/2026-08-15-effects11-dual-target-design.md`

Baseline: `python tests/validate_command_protocol.py` passed 15/15.

## Pre-flight task/interface scan

| Task | Produces / consumes | Internal consistency and cross-task finding |
|---|---|---|
| 1 | Public C ABI header + drift validator; consumed by Tasks 2–3 and external hosts | Field order/version checks are consistent. Public header currently depends on `src/core/BridgeData.h`, so packaging/docs must ship or expose that semantic layout deliberately. Add compile-time standard-layout/trivial-copy assertions and a capacity-checked snapshot copy entry point. |
| 2 | DLL export + publish lifecycle; consumes Task 1, touches existing ENB frame path | Plan’s raw live pointer can tear for an arbitrary in-process reader. Preserve the pointer for same-thread/short-lived compatibility, but append a `CopyFrameData` function that copies a stable published snapshot and is the recommended consumer path. Existing ENB push order remains unchanged. |
| 3 | Consumer contract; consumes Tasks 1–2 | Document absent module, older symbol, version/size mismatch, teardown, and concurrent-read fallbacks. Compile the documented safe-copy example, not only acquisition. |
| 1→2 | ABI declaration → implementation/export | Exact field order and calling convention are load-bearing; exported function returns static storage and no exceptions cross boundary. |
| 2→3 | Lifetime/thread rules → documentation | Docs must match implementation’s stable-copy guarantees and explicitly bound raw-pointer lifetime. |

Ruling: append safe-copy ABI — retain `GetFrameData` for the planned first consumer, append `CopyFrameData(destination, size, frameIndex)` in v1 and make it the recommended path; if wrong, the cost is one extra function pointer, while omitting it makes “any in-process consumer” a data-race claim.

Ruling: keep ENB publication unchanged — snapshot/counter publication happens only after sanitization and the existing `ENBInterface::PushAllData`; if wrong, consumers may observe one-frame-late data but ENB behavior remains stable.

## Task status

- Task 1 — complete and independently approved. Implementation commit `eaa0e2e`; review-fix commit `7d585bc`. The public v1 C++/C-linkage ABI, producer-only export macro, safe-copy field, and drift validator are accepted.
- Task 2 — complete and independently approved. Implementation commit `3f51e9c`; race-safety fix `0a8e393`. The export, all six v1 fields, caller-local raw snapshot, coherent safe-copy path, post-sanitization publication, and latched teardown contract are accepted. Focused header/source/PE validators passed; `SB_GetBridgeInterface` is present in the Release DLL.
- Task 3 — complete and independently approved at `fe10fce`. `docs/BRIDGE-ABI.md` and the README now document dynamic optional acquisition, exact version/size/function checks, coherent `CopyFrameData`, thread-local raw-pointer lifetime, teardown and all fallback states. The exact complete example compiled with installed MSVC `/std:c++latest /c /I include /I src` (this compiler does not accept the spelling `/std:c++23`).
- Task 4 — complete and independently reviewed (approve-with-notes) at `b9f46fb`, closeout `0ac5df8`. Distributable build is the safe default (native replacements OFF, compiled engine-fix default follows the flag with a static_assert), ABI SDK/docs ship in the archive, packaging unconditionally rejects native-suite markers in plugin and proxy, marker tables cross-checked, public copy scrubbed with credits/provenance kept. Deterministic package: SkyrimBridge-3.0.0.zip, 931,797 bytes, sha256 611a51e8450fd22d791e28cfe1ab586df5cc6f2b2697df9a549b5b3dd011899d, reproduced byte-identically. Full detail in task-4-report.md. The two sev3 notes recorded here (detectDead/drunk flags never set; teardown latch only on the ENB exit path) were the pre-upload backlog and are resolved below; nothing uploads from this campaign.
- Campaign closeout 2026-08-27 — branch merged to main by fast-forward and pushed. No Nexus page created, no upload; merge is not ship.

## Sev3 closeout, 2026-08-27

Both pre-upload sev3 notes resolved on branch `fix/w2-sev3-preupload`, merged
to main by fast-forward at `fa0406c` and pushed.

- `477e6ed` documents `detectDead` (VisionEffects.z) and `drunk`
  (MiscEffects.z) as reserved. The vendored CommonLibSSE-NG headers expose no
  detect-dead archetype (`RE::EffectArchetypes::ArchetypeID` has only
  kDetectLife) and no alcohol or intoxication actor value, so neither flag has
  an implementable engine signal on this surface. Both fields now publish a
  literal 0.0f with a reserved comment; BridgeData.h, both shader headers, and
  the scalar-flag validator agree on the wording. The v1 ABI layout is
  untouched.
- `fa0406c` latches teardown on the standalone path. `HookedWndProc` handles
  WM_DESTROY and calls `NotifyStandaloneWindowDestroyed()` in main.cpp, gated
  by the existing `s_enbDrivesUpdates` flag so the ENB OnExit sequence is
  unchanged; `MarkTeardown()` is idempotent. WM_DESTROY fires on the game's
  message thread before DLL unload, avoiding the loader-lock hazard that
  ruled out a DllMain design. The ABI implementation validator pins the
  guard-then-latch shape.

Full gate on the branch head: every tests/validate_*.py green (corpus-gated
codecs SKIP by convention, no game install here), source and PE ABI checks
pass, release-package validator passes, and two independent package builds
reproduced SkyrimBridge-3.0.0.zip byte-identically at 932037 bytes, sha256
91d1a68ae81ed892f4e49570860961cb2dcd5c14e5d2ae518befabcfe56b7180. The prior
recorded package (931797 bytes, 611a51e8...) is superseded by content, as
expected. Upload to Nexus remains a human gate; merge is not ship.
