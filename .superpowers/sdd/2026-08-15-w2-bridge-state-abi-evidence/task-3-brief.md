### Task 3: Document the consumer contract

**Ownership:**
- Create: `docs/BRIDGE-ABI.md`
- Modify: `README.md`
- Do not change the public header, implementation, packaging, native replacements, or unrelated docs.

**Accepted producer contract:**
- Base commit: `0a8e393bcafcd835bb79513ad175bf5b09ab1397`.
- `SB_GetBridgeInterface` is resolved dynamically with `GetModuleHandleW` and `GetProcAddress`; consumers do not link against SkyrimBridge.
- Absence of the DLL, absence of the symbol, an unsupported interface version, an `allDataSize` mismatch, an invalid frame, teardown, and a failed copy are all supported fallback conditions rather than fatal errors.
- `CopyFrameData` is the recommended coherent read path. It returns a matching snapshot and frame index under the producer lock.
- `GetFrameData` returns a caller-thread-local snapshot. Its pointer is valid only on that calling thread until the next `GetFrameData` call on the same thread or thread exit. It must not be shared across threads or retained as long-lived state.
- Existing ENB publication behavior and Kitsuune-independent public-release boundaries are unchanged.

**Required documentation:**
1. Explain acquisition, versioning, layout checks, validity, teardown, concurrency, and fallbacks in public user/developer language with no local paths.
2. Include a complete working consumer example that includes only `<windows.h>` and `SkyrimBridgeAPI.h`, resolves the symbol, checks `version` and `allDataSize`, uses `CopyFrameData`, and returns cleanly on every unsupported/invalid condition.
3. State plainly that SkyrimBridge is optional and downstream mods must keep their own native/spatial/identity fallback.
4. Add a concise README section for non-ENB shader/framework consumers that names `SB_GetBridgeInterface` and links `docs/BRIDGE-ABI.md`.
5. Preserve current credits, provenance, `SB_Retain`, and Kitsuune interoperability/exclusion language.

**Focused verification:**
- Save the documented example as an ignored or external scratch `.cpp` file; do not commit it.
- Compile it with MSVC C++23 and repository include roots (`/I include /I src`). The translation unit itself may include only `<windows.h>` and `SkyrimBridgeAPI.h`.
- Run `python tests/validate_bridge_abi_header.py` as the only existing contract validator needed for this docs task.
- Run `git diff --check` on the task range.

**Commit:**
- `docs: document the bridge state ABI consumer contract`

**Handoff:**
- Report the exact compile command/result, changed files, and any limitation.
- The task requires independent review before release integration.
