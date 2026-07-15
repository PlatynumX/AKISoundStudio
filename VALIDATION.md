# Validation log - AKI Sound Studio v0.5.4

Validated in the Linux build environment with the platform-independent core smoke test.

## WM2000

- ROM detected as `NWXE`.
- Parsed 238 sounds.
- Bank 01 / `005F` decoded successfully.
- VADPCM round-trip encode/decode passed.
- CRC repair returned non-zero CRC1/CRC2.
- Protected entrance sequence region regression still passes.

## VPW2

- ROM detected as `NA2J`.
- Parsed 516 sounds.
- Bank 01 / `0037` decoded successfully.
- VADPCM round-trip encode/decode passed.
- CRC repair returned non-zero CRC1/CRC2.
- Protected entrance sequence region regression still passes.

## v0.5 additions

- Added editable visible list name/rate fields.
- Added hack profile CSV export/import for list entries and bank locations.
- Added conservative auto-detection for relocated AKI `N64 PtrTablesV2` bank locations.
- The core still builds with GCC and Clang-compatible C++17 settings.


## v0.5.1 hotfix validation

- Rebuilt and ran the core parser against the uploaded stock WM2000 ROM: 238 sounds parsed.
- Rebuilt and ran the core parser against the uploaded stock VPW2 ROM: 516 sounds parsed.
- Verified structural profile detection for a WM2000-compatible ROM with a changed header code: 238 sounds parsed through the WM2000 profile.


## v0.5.2 profile-binding regression

- Reproduced the zero-sound failure in code review: default move assignment left `LoadedRom::profile` aimed at the moved-from temporary `customProfile`; its moved bank vector was empty.
- Added destination-rebinding copy/move semantics for `LoadedRom`.
- Added a synthetic regression test that fails unless copied and moved ROM objects point to their own `customProfile`.
- Added a full-ROM regression path that mirrors `App.cpp`: `LoadRom` into a temporary, move into persistent app state, then parse.
- GCC C++17 build: PASS.
- Clang C++17 build: PASS.
- CTest synthetic suite: PASS.


## v0.5.3 safe-padding regression

- Added a synthetic AKI `N64 PtrTablesV2` bank and verified a longer waveform used contiguous blank TBL padding without touching the protected object that followed it.

## v0.5.4 Wavosaur two-point loop regression

- Verified a markerless WAV replacing an already-looped sound clears the old loop pointer instead of inheriting stale sample positions.
- Exported a WAV with one forward loop, re-imported it, and verified the two positions round-trip exactly.
- Imported those two points into a previously non-looped target and verified a new non-overlapping 0x2C-byte CTL loop block was allocated and referenced.
- Verified the sixteen-sample Nintendo ADPCM loop state is rebuilt from the newly encoded waveform at the imported loop start.
- GCC C++17 build: PASS.
- Clang C++17 build: PASS.
- CTest synthetic suite: PASS.
