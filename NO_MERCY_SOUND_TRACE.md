# WWF No Mercy (USA) Rev 1 sound trace

ROM header code: `NW4E`

## Banks

| Bank | CTL | TBL | Records | Sequence object | Description |
|---|---:|---:|---:|---:|---|
| 00 | `0x016F32A0` | `0x016F6F10` | 85 | none fixed | Instruments, music, miscellaneous |
| 01 | `0x01858030` | `0x0185EDB0` | 165 | `0x01855F90` | Game sounds and voices |
| 02 | `0x01967410` | `0x01969880` | 43 | `0x01965C50` | Entrance themes |

All three CTLs contain `N64 PtrTablesV2`; all three TBLs contain `N64 WaveTables`.

## ASM bank references

The ROM initialization code contains split `lui`/`addiu` references:

- Bank 00 TBL: ROM `0x4028/0x402C`, duplicate at `0x4178/0x417C`
- Bank 00 CTL: ROM `0x4034/0x4038`
- Bank 01 TBL: ROM `0x4064/0x4068`, duplicate at `0x41DC/0x41E4`
- Bank 01 CTL: ROM `0x406C/0x4070`, duplicate at `0x4098/0x409C`
- Bank 02 TBL: ROM `0x40CC/0x40D0`, duplicate at `0x4210/0x4218`
- Bank 02 CTL: ROM `0x40D4/0x40D8`, duplicate at `0x4100/0x4104`

## Sequence and rate trace

Bank 01's sequence object has 286 wrappers and 156 selectors. Bank 02's sequence object has 200 wrappers and 43 selectors. AKI Sound Studio reads the selector-to-wave map, pitch keys, and each waveform's coarse/fine tuning to derive effective playback rates. Records without a fixed script use remain unknown rather than guessed.

## Cross-game audio comparison

Every No Mercy waveform was VADPCM-decoded and compared sample-for-sample against the supported WM2000 and Revenge Redux banks. A label is copied only when decoded PCM is exactly equal. No same-ID assumption is used.

- No Mercy records: 293
- Exact matches to at least one compared game: 47
- Exact matches carrying an existing non-empty label: 21
- Unmatched or matched-to-unlabeled sounds remain blank and editable.
