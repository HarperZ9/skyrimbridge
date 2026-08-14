# Validation suite

SkyrimBridge has two explicit offline validation scopes:

- **Portable** runs from a fresh checkout after a Release build. It covers
  protocols, generated containers, source invariants, the public package,
  and simulators that do not need private game assets.
- **Full corpus** adds independent decoding and round-trip checks against an
  operator-supplied Skyrim mod directory. The corpus is not redistributed.

Run the portable gate:

```powershell
cmake --build build --config Release
python scripts/run_validation.py --mode portable
```

Run the full gate:

```powershell
python scripts/run_validation.py --mode full `
  --mods-root "E:\Modlists\YourList\mods"
```

The equivalent environment variable is
`SKYRIMBRIDGE_TEST_MODS_ROOT`. Full mode validates the directory before
launching tests and treats missing fixtures as failures. Portable mode
reports the corpus-dependent validators as skipped; it never represents
those checks as passes.

Each run writes:

- `build/validation/validation-<mode>.json` — machine-readable results,
  durations, revision, and output hashes.
- `build/validation/validation-<mode>.log` — raw validator output.

The JSON intentionally records whether a corpus was configured, but not its
local absolute path.
