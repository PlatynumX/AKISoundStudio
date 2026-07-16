# AKI Sound Studio v0.7.2a

Windows-only AKI N64 sound-bank tool for **WWF WrestleMania 2000**, **Virtual Pro Wrestling 2**, **WCW/nWo Revenge Redux**, and **WWF No Mercy (USA) Rev 1**.

## v0.7.2a fixture packaging fix

- Commits `tests/fixtures/austin.wav` despite the general `*.wav` ignore rule.
- CMake verifies the fixture exists at configure time and copies it into the build tree.
- The smoke test reads the staged fixture instead of relying on a hardcoded source-tree path.
- Missing-fixture failures are identified as test configuration errors.

## What works

- Auto-detects WM2000 `NWXE`, VPW2 `NA2J`, Revenge Redux `NW2E`, and No Mercy Rev 1 `NW4E` ROMs.
- Parses AKI `N64 PtrTablesV2` sound banks.
- Decodes Nintendo VADPCM to 16-bit mono WAV.
- Imports 16-bit PCM WAV replacements.
- Encodes replacements back to Nintendo VADPCM using the original predictor book.
- Rebuilds the whole bank-local TBL instead of requiring a replacement to fit the original sample slot.
- Automatically uses only verified contiguous `00`/`FF` padding after the last waveform, stopping at the first real byte or configured CTL/TBL/sequence boundary.
- Imports the two loop points saved by WAV files and rebuilds the 16-sample Nintendo ADPCM loop state. Markerless WAVs are non-looping and never inherit the old song's positions.
- Saves patched `.z64` ROMs and repairs N64 CRC1/CRC2.
- Protects sequence/control data after a bank's exact last waveform in normal repack mode.
- Supports Expert CTL/TBL end-offset overrides.
- Shows WM2000, VPW2, and Revenge Redux sample-rate evidence, including ROM pitch keys and per-wave coarse/fine tuning.
- Lets you edit the visible list name/rate for a selected sound.
- Exports/imports hack profile CSV files containing bank locations and editable list entries.
- Auto-detects relocated AKI sound bank locations when the known stock offsets no longer contain control banks.




## v0.7.2 real WAV loop-marker regression

- Added `tests/fixtures/austin.wav` as a real-world RIFF/WAVE loop-marker fixture.
- Confirms the file contains 188,179 mono 16-bit samples at 7,000 Hz.
- Reads the two loop points exactly as start `12,544` and inclusive WAV end `133,888`; the internal/N64 exclusive end is `133,889`.
- Preview segmentation is now resolved by shared core logic: samples `0-12,543` play once, then samples `12,544-133,888` repeat.
- Resampling scales both loop points on the new sample timeline.
- Injection regression verifies the N64 loop record and 16-sample decoder state are rebuilt at loop start.
- WAV export/re-import verifies the same two loop markers survive a round trip.

## v0.7.1 full-bank relocation hardening

- Modified WM2000 banks with one-to-eight non-frame trailer bytes are accepted and decoded by complete VADPCM frames.
- The details panel reports used, available, and free in-place TBL capacity and clearly marks effectively full banks.
- Ordinary oversized replacements now relocate a full bank automatically and patch traced CTL/TBL ASM references; relocation is no longer limited to Add Sound and ROM Migration.
- Validated against the Badd Blood WM2000 hack: its shifted entrance bank has exactly 6 bytes of in-place growth, is detected automatically, and relocates without changing the occupied data immediately after the original TBL.

## v0.7.0 bank expansion, tracing, analysis, and migration

- **Add new sound to selected bank** relocates the bank when necessary, expands the PtrTablesV2 count, relocates the coarse/fine tuning and record-pointer tables, clones a chosen predictor-book template, writes optional WAV loop markers, repacks the TBL, and patches every traced MIPS CTL/TBL pointer.
- ROMs smaller than 64 MiB can grow up to the N64 64 MiB limit. A bank already at the limit must have verified internal free space or an expert relocation plan.
- **Automatic ASM pointer tracing** scans MIPS `lui` plus `addiu`/`ori` address construction, including signed-low-half correction, and reports all code references to each CTL and TBL.
- Relocated-bank detection now pairs `N64 PtrTablesV2` and `N64 WaveTables` structures independently instead of assuming the original CTL-to-TBL distance remains unchanged.
- **Waveform analysis** decodes every sound, hashes exact PCM, exports stable identities, and identifies duplicate groups within the loaded ROM. Non-frame trailer bytes on unusual tail records are ignored exactly as the decoder ignores incomplete VADPCM frames.
- **Exact cross-game matching** compares decoded PCM length and SHA-1 identity. This is the same evidence standard used for No Mercy and Revenge Redux labels; IDs alone are never considered a match.
- **Migrate into selected slot from ROM** loads another supported game, finds a unique same-name source or falls back to the same Bank/ID, decodes its PCM, preserves its two loop markers, converts to the target rate, applies the current import gain/clipping setting, encodes with the target predictor book, and automatically relocates the target bank when normal capacity is insufficient.
- All loop UI and documentation use the generic term **WAV loop markers**.

### New Tools menu commands

- `Trace CTL/TBL ASM pointers`
- `Export waveform identity / duplicates...`
- `Add new sound to selected bank...`
- `Migrate into selected slot from ROM...`

## Android/Termux repository updater

Place `AKISoundStudio-v0.7.2a-source-fixture-fix.zip` and `update_aki_sound_studio_v072a_fixture_fix_termux.sh` on the phone, then run:

```bash
termux-setup-storage
bash ~/storage/downloads/update_aki_sound_studio_v072a_fixture_fix_termux.sh
```

The updater searches common Android shared-storage locations, updates or creates the public `AKISoundStudio` GitHub repository, runs the Windows GitHub Actions build, and downloads `AKISoundStudio-v0.7.2a-win64.zip`.

## Editable list entries

Select a sound, edit the name and/or rate fields on the right, then click **Apply list edit**. These list edits are sidecar metadata, not ROM text. Use **File -> Export hack profile / list...** to save them and **File -> Import hack profile / list...** to reload them later.

## Hack profile CSV

The hack profile CSV contains two row types:

```csv
kind,game_code,bank,id,name,rate_hz,confidence,method,control_offset,wave_offset,sequence_offset,description,note
bank,NA2J,01,,,,,,0x0144FD30,0x01454C20,0x0144E0A0,Game sounds,
sound,NA2J,01,0037,Thunder,11429,Manual override,User-edited list entry,,,,,
```

`bank` rows are for hacked ROMs where a CTL/TBL pair was relocated. `sound` rows are for labels and sample-rate overrides.

## Auto-detecting hacked sound locations

Use **Tools -> Auto-detect sound locations** when a ROM hack still uses AKI `N64 PtrTablesV2` banks but moved them away from stock offsets. The first implementation matches the expected bank sound counts and assumes each bank's CTL-to-TBL and CTL-to-sequence spacing stayed the same as stock. If a hack moved CTL and TBL independently, use a hack profile CSV with explicit offsets.

## Current limits

- The auto-detector is conservative and intended for relocated AKI sound blocks, not arbitrary rebuilt audio engines.
- Label/list edits do not change in-game text; they are project metadata for AKI Sound Studio.


## v0.5.1 hotfix

- Restores sound listing for stock WM2000/VPW2 after the v0.5 metadata/profile changes.
- Adds hack-header detection: WM2000-compatible ROM hacks such as CPW-style builds no longer have to keep the `NWXE` header code to open.
- Detection now falls back to the actual AKI `N64 PtrTablesV2` bank signatures and expected bank-count patterns before rejecting a ROM.
- If stock offsets fail, the existing auto-detect path still attempts to re-locate the bank control blocks.


## v0.5.2 zero-sound-list fix

- Fixes the actual v0.5/v0.5.1 blank-list regression. `LoadedRom` owns a mutable game profile and also keeps a pointer to that profile. Moving the freshly loaded ROM into the Win32 app state copied the pointer without rebinding it, so it pointed to the moved-from temporary whose bank vector was empty. Parsing then returned success with zero sounds.
- Adds explicit copy/move constructors and assignment operators that always rebind an owned profile to the destination `LoadedRom`.
- Adds regression tests for both copy and move assignment, including the exact temporary-to-app-state load path.
- Keeps the v0.4.1 bank-repack protection, expert CTL/TBL overrides, editable list rows, hack profile import/export, and relocated-bank auto-detection unchanged.


## v0.5.3 oversized replacement support

- Fixes the false “too large” warning for modest replacements that fit in verified blank padding immediately after a bank’s last waveform.
- Keeps the v0.4.1 sequence/control protection: automatic growth stops at the first nonblank byte and never crosses a configured sound-bank or sequence-object boundary.

## v0.5.4 two-point WAV loop fix

- Treats looping as exactly two WAV loop points: **loop start** and **loop end**.
- Reads the forward loop saved in standard WAV `smpl` metadata used by sampler-oriented audio editors.
- Uses those two positions exactly for the replacement waveform; the old song's numeric loop positions are never inherited.
- A WAV without saved loop points is imported as non-looping and clears the target sound's old loop pointer.
- Parses and writes the complete Nintendo `ALADPCMloop` record: start, end, repeat count, and sixteen signed decoder-state samples.
- WAV exports include the same two loop points for looped sounds.
- Adding a loop to a previously non-looped slot allocates a verified free 0x2C-byte block inside the bank CTL and avoids shared loop records.


## v0.5.5 Revenge Redux support

- Adds a built-in profile for the uploaded **WCW/nWo Revenge Redux (USA)** ROM (`NW2E`).
- Uses Redux Bank 00 CTL/TBL at `0x02D62CEC` / `0x02D66BBC`.
- Uses Redux Bank 01 CTL/TBL at `0x03D9715C` / `0x03D9D6EC`.
- Parses all **245 records**: 96 in Bank 00 and 149 in Bank 01.
- Adds `data/revenge_redux_sounds.csv` with one editable row for every Redux record.
- Keeps every Redux record visible and editable. The provisional same-ID label copy was replaced in v0.5.6 after comparing the actual audio.
- Keeps all updates on the existing public repository's `main` branch.


## v0.5.6 verified Revenge Redux label mapping

- Compared the uploaded stock WM2000 ROM (`SHA-1 442d417a52ed672ca1a47e7261a5414debb1e27a`) directly with the uploaded Revenge Redux ROM (`SHA-1 0695b127b654a1d6b79ffe7e62fb8f2981c26d5c`).
- Decoded each valid Nintendo VADPCM record and matched sounds by exact PCM length and exact decoded sample data inside the corresponding bank.
- Redux Bank 00 is not a straight WM2000 copy: it has 96 records versus 46, and only 32 Redux records exactly match a WM2000 Bank 00 sound. Several matching sounds moved to different IDs.
- Redux Bank 01 has 149 records versus 147, with 113 exact decoded-audio matches. Thirty-four overlapping records differ, and two Redux tail records use non-frame-aligned data that cannot be decoded by the normal VADPCM comparison path.
- `data/revenge_redux_sounds.csv` now copies only labels supported by an exact decoded-audio match. It contains 72 confirmed named rows; every unmatched record is left unlabeled for manual identification.
- Removes the provisional labels from changed Bank 01 IDs `005F` through `0066` and corrects shifted Bank 00 labels such as cheering at Redux `0033`/`0034`.
- See `REVENGE_REDUX_WM2K_COMPARISON.md` for the comparison method and corrected mapping summary.


## v0.6.1 WWF No Mercy Rev 1 support

- Adds a built-in `NW4E` profile for WWF No Mercy (USA) Rev 1.
- Traces three ROM banks: 85, 165, and 43 records, for 293 total sounds.
- Uses ROM sequence objects for Banks 01 and 02 to derive playback rates from selector maps, pitch keys, and per-wave coarse/fine tuning. Sounds without fixed script references remain unknown.
- Compares every decoded No Mercy waveform sample-for-sample against WM2000 and Revenge Redux. Only exact matches inherit labels; unmatched sounds remain blank and editable.
- Removes editor-specific terminology: loop metadata is described simply as two WAV loop markers.
- See `NO_MERCY_SOUND_TRACE.md` for offsets, ASM references, sequence objects, and comparison results.

## v0.6.0 Revenge Redux ROM rate trace

- Traces the 210 source-resident Redux SFX scripts referenced by the pointer table at ROM `0x00030ACC`.
- Parses opcode `0x81` as a direct Bank 01 waveform selector and collects every pitch key used with that waveform.
- Derives ROM playback-equivalent rates for **136 of 149 Bank 01 waveforms**. The 13 records with no fixed script reference remain unknown rather than receiving guessed rates.
- Reads the PtrTablesV2 coarse-semitone byte table at header `+0x24` and the signed fine-cents byte at the start of each four-byte tuning entry referenced by header `+0x28`.
- Applies the engine formula `11025 * 2^((key - 0x1F + coarse + fine/100) / 12)`. Multiple ROM pitch uses are retained as alternate rates.
- Displays the traced pitch keys, coarse semitones, and fine cents in the sound details panel and exports them in metadata CSV.
- Bank 00 remains without one fixed per-wave rate because it is the instrument/music bank and its samples are played across musical notes; v0.6.0 does not pretend those note-dependent samples have a single ROM rate.
- See `REVENGE_REDUX_RATE_BACKTRACE.md` for the binary trace and unmapped record list. `REVENGE_REDUX_RATE_BACKTRACE.csv` contains all 149 Bank 01 rows with keys, tuning, rates, and trace status.

## v0.6.0 loop-marker preview playback

Preview playback now honors the selected sound's two stored loop points. Audio before the loop start plays once; the start-to-end region then repeats until **Stop** is pressed. Sounds without valid loop points continue to play once normally. This uses Windows `waveOut` buffers rather than `PlaySound`, which only supports whole-file looping and ignores WAV `smpl` markers.


## v0.6.0 automatic import resampling

When an imported WAV rate differs from the selected sound rate, the app now offers to resample automatically, import unchanged, or cancel. Automatic conversion uses windowed-sinc interpolation with anti-alias filtering and scales WAV `smpl` loop start/end points to the new sample timeline before VADPCM encoding.

## v0.6.0 import amplification

The replacement row now includes an **Import gain dB** field and a default-enabled **Limit clip** option. Gain is applied to the final mono PCM after optional rate conversion and before Nintendo VADPCM encoding. Positive values amplify (`+6 dB` is approximately double amplitude); `0 dB` leaves the WAV unchanged. With clipping prevention enabled, the app reduces only the excess requested gain needed to keep samples inside signed 16-bit PCM. Disabling it applies the requested gain and hard-clamps out-of-range samples. Sample rate, duration, and WAV loop-marker positions are unchanged.
