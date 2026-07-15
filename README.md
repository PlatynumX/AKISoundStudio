# AKI Sound Studio v0.5.4

Windows-only AKI N64 sound-bank tool for **WWF WrestleMania 2000** and **Virtual Pro Wrestling 2**.

## What works

- Auto-detects WM2000 `NWXE` and VPW2 `NA2J` ROMs.
- Parses AKI `N64 PtrTablesV2` sound banks.
- Decodes Nintendo VADPCM to 16-bit mono WAV.
- Imports 16-bit PCM WAV replacements.
- Encodes replacements back to Nintendo VADPCM using the original predictor book.
- Rebuilds the whole bank-local TBL instead of requiring a replacement to fit the original sample slot.
- Automatically uses only verified contiguous `00`/`FF` padding after the last waveform, stopping at the first real byte or configured CTL/TBL/sequence boundary.
- Imports the two loop points saved by Wavosaur-compatible WAV files and rebuilds the 16-sample Nintendo ADPCM loop state. Markerless WAVs are non-looping and never inherit the old song's positions.
- Saves patched `.z64` ROMs and repairs N64 CRC1/CRC2.
- Protects sequence/control data after a bank's exact last waveform in normal repack mode.
- Supports Expert CTL/TBL end-offset overrides.
- Shows WM2000 and VPW2 sample-rate evidence.
- Lets you edit the visible list name/rate for a selected sound.
- Exports/imports hack profile CSV files containing bank locations and editable list entries.
- Auto-detects relocated AKI sound bank locations when the known stock offsets no longer contain control banks.


## Android/Termux repository updater

Place `AKISoundStudio-v0.5.4-source-wavosaur-loop-points.zip` and `update_aki_sound_studio_v054_wavosaur_loop_points_termux.sh` on the phone, then run:

```bash
termux-setup-storage
bash ~/storage/downloads/update_aki_sound_studio_v054_wavosaur_loop_points_termux.sh
```

The updater searches common Android shared-storage locations, updates or creates the public `AKISoundStudio` GitHub repository, runs the Windows GitHub Actions build, and downloads `AKISoundStudio-v0.5.4-win64.zip`.

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

## v0.5.4 Wavosaur two-point loop fix

- Treats looping as exactly two WAV loop points: **loop start** and **loop end**.
- Reads the forward loop saved in standard WAV `smpl` metadata used by sampler-oriented editors such as Wavosaur.
- Uses those two positions exactly for the replacement waveform; the old song's numeric loop positions are never inherited.
- A WAV without saved loop points is imported as non-looping and clears the target sound's old loop pointer.
- Parses and writes the complete Nintendo `ALADPCMloop` record: start, end, repeat count, and sixteen signed decoder-state samples.
- WAV exports include the same two loop points for looped sounds.
- Adding a loop to a previously non-looped slot allocates a verified free 0x2C-byte block inside the bank CTL and avoids shared loop records.
