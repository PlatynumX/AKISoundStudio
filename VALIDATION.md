# Validation log - AKI Sound Studio v0.5.7

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


## v0.5.5 Revenge Redux validation

- Analyzed uploaded ROM title `REVENGE REDUX`, game code `NW2E`, SHA-1 `0695b127b654a1d6b79ffe7e62fb8f2981c26d5c`.
- Confirmed exactly two `N64 PtrTablesV2` control banks.
- Parsed 96 Bank 00 records and 149 Bank 01 records: **245 total**.
- Loaded copied WM2000 labels for matching IDs and retained all Redux-only rows as editable unlabeled entries.
- Decoded and re-encoded Bank 01 / `005F`; round-trip SNR was lossless for the decoded source in this test.
- Completed a bank-local replacement/repack without losing the two non-frame-aligned tail records in Bank 01.
- Repaired CRC1/CRC2 to `9A8FA1FE` / `980E1D55` in the in-memory validation copy.
- GCC C++17 build: PASS.
- CTest synthetic suite: PASS.
- Full uploaded Redux ROM parser/decoder/replacement smoke test: PASS.


## v0.5.6 WM2000-to-Revenge Redux audio comparison

- Stock WM2000 SHA-1: `442d417a52ed672ca1a47e7261a5414debb1e27a`.
- Revenge Redux SHA-1: `0695b127b654a1d6b79ffe7e62fb8f2981c26d5c`.
- Compared exact encoded bytes, predictor books, loop records, decoded sample counts, and decoded PCM hashes.
- Bank 00: WM2000 46 records; Redux 96 records; 32 exact decoded-audio matches. All 32 also have identical encoded bytes, predictor books, and loop records.
- Bank 01: WM2000 147 records; Redux 149 records; 113 exact decoded-audio matches. All 113 also have identical encoded bytes, predictor books, and loop records.
- Confirmed named Redux rows after content matching: 72.
- Removed or corrected 35 provisional labels that had been copied by matching ID rather than matching audio.
- Added five labels at shifted Redux IDs where the audio exactly matches a differently numbered WM2000 record.
- Full Redux parser/replacement smoke test verifies shifted Bank 00 label `0033 = cheering` and verifies changed Bank 01 record `005F` remains unlabeled.


## v0.5.7 Revenge Redux ROM rate trace

- Parsed the source script pointer table at ROM `0x00030ACC` as 210 pointers.
- All 210 scripts resolved inside ROM `0x0002FAE0-0x00030AB8` and parsed without an unknown/truncated opcode.
- Found direct Bank 01 waveform selectors for 136 of 149 records.
- Confirmed the 13 unreferenced IDs remain without `ROM-derived` confidence.
- Added signed coarse-semitone and fine-cent parsing from PtrTablesV2 header pointers `+0x24` and `+0x28`.
- Regression checks: `01/0001 = 11089 Hz` with +10 cents; `01/0031 = 20812 Hz` with +2 semitones; `01/0044 = 33038 Hz`; `01/005F = 31183 Hz`; `01/005A` remains untraced.
- Full Redux parse/repack test still parses 245 records and preserves nonstandard tail records `0093/0094`.
