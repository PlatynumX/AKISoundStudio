# Revenge Redux sample-rate backtrace

This trace was made directly from the uploaded `WCW-nWo Revenge (USA) redux.z64` ROM.

## Audio initialization

- Audio initialization starts at ROM `0x00003D20` / RAM `0x80003120`.
- Bank 00 is loaded from CTL/TBL `0x02D62CEC` / `0x02D66BBC`.
- Bank 01 is loaded from CTL/TBL `0x03D9715C` / `0x03D9D6EC`.
- The requested mixer frequency written by the game is `0x7080` (28,800).
- Bank 01 is relocated by the AKI PtrTablesV2 loader at RAM `0x800159EC` / ROM `0x000165EC`.

## SFX script source

- The game supplies a source-resident SFX script pointer table at ROM `0x00030ACC` / RAM `0x8002FECC`.
- The following priority table begins at ROM `0x00030E14`, proving the pointer table contains `(0x30E14 - 0x30ACC) / 4 = 210` entries.
- All 210 pointers resolve to unique scripts between ROM `0x0002FAE0` and `0x00030AB8`.
- All 210 scripts parse cleanly with the existing AKI script opcode lengths.
- Opcode `0x81` contains the Bank 01 waveform ID directly in Redux. Ordinary bytes below `0x80` are pitch keys followed by their variable-length duration.

## Wave tuning source

The PtrTablesV2 loader converts two ROM tuning tables for every waveform:

- Header `+0x24`: one signed coarse-semitone byte per waveform.
- Header `+0x28`: one four-byte tuning cell per waveform; the first signed byte is divided by 100 to produce fine cents.
- Loader code adds `coarse - 48` to the fine value. The script note key is then added at playback.
- Playback code at RAM `0x80014F5C` / ROM `0x00015B5C` multiplies the resulting semitone value by `1/12` and computes a power of two before setting the voice pitch.

AKI Sound Studio expresses the corresponding WAV/playback rate as:

```text
11025 * 2^((pitchKey - 0x1F + coarseSemitones + fineCents/100) / 12)
```

## Coverage

- Bank 01 records: 149
- Records selected by a fixed ROM script and assigned ROM-derived rates: 136
- Records with no fixed script selector: 13

Unreferenced Bank 01 IDs:

```text
0009 0022 0029 0030 0042 0043 005A 0081 0082 0083 0086 0093 0094
```

These 13 remain unknown. IDs `0093` and `0094` are also the unusual tail records protected by the Redux repack regression test.

## Verified examples

| Bank/ID | ROM keys | Coarse | Fine | Derived rate(s) |
|---|---:|---:|---:|---:|
| 01/0000 | 1F, 2B | 0 | 0 | 11025, 22050 |
| 01/0001 | 1F | 0 | +10 cents | 11089 |
| 01/0031 | 28 | +2 | 0 | 20812 |
| 01/0032 | 28 | +1 | 0 | 19644 |
| 01/0033 | 28 | 0 | 0 | 18542 |
| 01/0044 | 32 | 0 | 0 | 33038 |
| 01/005C | 2C | 0 | 0 | 23361 |
| 01/005F | 31 | 0 | 0 | 31183 |
| 01/0060 | 32, 31 | 0 | 0 | 33038, 31183 |
| 01/0061-0066 | 32 | 0 | 0 | 33038 |
| 01/0067-0080 | 2B | 0 | 0 | 22050 |

## Bank 00

Bank 00 is passed to the sequenced instrument/music side of the engine, not the fixed Redux SFX script table. Its waveform playback pitch changes with musical notes. v0.5.7 therefore does not assign a single supposedly ROM-derived rate to Bank 00 records without tracing the sequence/note usage for each instrument. Existing exact-audio reference metadata remains reference metadata, not ROM-derived.
