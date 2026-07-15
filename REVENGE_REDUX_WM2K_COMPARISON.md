# WM2000 vs. Revenge Redux sound-bank comparison

## ROMs compared

- WWF WrestleMania 2000 (USA): `442d417a52ed672ca1a47e7261a5414debb1e27a`
- WCW/nWo Revenge Redux (USA): `0695b127b654a1d6b79ffe7e62fb8f2981c26d5c`

## Method

Both ROMs were loaded through AKI Sound Studio's normal `N64 PtrTablesV2` parser. Every valid Bank 00 and Bank 01 record was decoded with its own predictor book. A Redux record was considered a match only when a WM2000 record in the same bank had the same decoded sample count and every decoded 16-bit PCM sample was identical.

For every accepted match, the encoded VADPCM bytes, predictor book, and loop record were also identical. This avoids copying labels solely because two records occupy the same numeric ID.

## Results

| Bank | WM2000 records | Redux records | Exact decoded-audio matches |
|---|---:|---:|---:|
| 00 | 46 | 96 | 32 |
| 01 | 147 | 149 | 113 |

Redux Bank 00 is rearranged and expanded; it is not a direct copy. Redux Bank 01 is mostly shared, but 34 overlapping records differ. Redux Bank 01 IDs `0093` and `0094` are non-frame-aligned tail records and were preserved but not decoded for matching.

The corrected Redux CSV contains **72 named rows confirmed by exact audio matches**.

## Newly recovered shifted labels

| Redux bank | Redux ID | Confirmed label | Matching WM2000 ID |
|---|---:|---|---:|
| 00 | 0016 | guitar 6 | 0014 |
| 00 | 0023 | guitar 10 | 001A |
| 00 | 0033 | cheering | 0021 |
| 00 | 0034 | cheering (loop) | 0022 |
| 00 | 0035 | Explosion | 002A |

## Corrected labels at IDs that previously inherited the wrong WM2000 name

| Redux bank | Redux ID | Provisional name | Confirmed name | Matching WM2000 ID |
|---|---:|---|---|---:|
| 00 | 0013 | guitar 5 | bass (loop) | 0011 |
| 00 | 0015 | guitar 7 | guitar 5 | 0013 |
| 00 | 0017 | guitar 8 | guitar 7 | 0015 |
| 00 | 0019 | guitar 9 (loop) | guitar 8 | 0017 |
| 00 | 0021 | cheering | timpani | 0018 |
| 00 | 0022 | cheering (loop) | guitar 9 (loop) | 0019 |
| 00 | 0024 | ? (loop) | guitar 11 (loop) | 001B |
| 00 | 0026 | sax 1 | guitar 12 | 001C |
| 00 | 0028 | sax 3 | guitar 14 (loop) | 001E |
| 00 | 0029 | flute | sawtooth? (loop) | 001F |

## Provisional labels removed because no exact WM2000 audio match exists

| Redux bank | Redux ID | Removed provisional name |
|---|---:|---|
| 00 | 000D | guitar 2 (loop) |
| 00 | 0010 | guitar 4 (loop) |
| 00 | 0011 | bass (loop) |
| 00 | 0014 | guitar 6 |
| 00 | 0018 | timpani |
| 00 | 001A | guitar 10 |
| 00 | 001B | guitar 11 (loop) |
| 00 | 001C | guitar 12 |
| 00 | 001D | guitar 13 (loop) |
| 00 | 001E | guitar 14 (loop) |
| 00 | 001F | sawtooth? (loop) |
| 00 | 0025 | bass ride |
| 00 | 0027 | sax 2 |
| 00 | 002A | Explosion |
| 00 | 002B | Howl |
| 00 | 002C | some sort of synth (loop) |
| 00 | 002D | gong -------------------------------------------------------------------------------- |
| 01 | 005F | Three Count Four (fall) |
| 01 | 0060 | Give Up |
| 01 | 0061 | Time Up |
| 01 | 0062 | Draw Game |
| 01 | 0063 | Ring Out |
| 01 | 0064 | Double Ring Out |
| 01 | 0065 | Knockout |
| 01 | 0066 | Loser |

Every other matching named row retained its WM2000 label. Unmatched Redux rows remain blank with an identification note in `data/revenge_redux_sounds.csv`.
