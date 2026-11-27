# lib/nescv

A frame plugin's picture encoder: an emulator's 256 x 192 picture, one
byte a pixel, turned into the frame kernel's lists (`parrotcart_frames()`,
`sdk/gbcv/kernel/gbkernel.asm`). The screen is Graphics II, 32 x 24
cells, each 8-pixel row in two of the console's 15 colours. Each third of
the screen has 256 pattern slots, so a cell the console already has costs
only its name. The names are double-buffered and flip in vertical blank,
so the console always shows a whole picture.

It came from the NES plugin and also runs the Neo Geo Pocket Color
plugin, which added a band over some columns only and a faster key.

| File | What |
|---|---|
| `nescv.h` | the API, with what each call does |
| `nescv.c` | the encoder, and the NES's APU as PSG bytes |
| `test.c` | the smoke test |
| `LICENSE` | the GNU LGPL 2.1 |

Add it to the plugin's sources (`lib/` is on the include path already):

```cmake
target_sources(mygame PRIVATE ${PARROTCART_DIR}/lib/nescv/nescv.c)
set_source_files_properties(${PARROTCART_DIR}/lib/nescv/nescv.c
    PROPERTIES COMPILE_OPTIONS "-O3;-fno-data-sections")
target_compile_definitions(mygame PRIVATE NESCV_FAST_KEYS)
```

Its hot functions go in RAM on the cart (`.time_critical`).

## Use

```c
#include "nescv/nescv.h"

static nescv_t enc;                 // about 92 KB: keep it static
static uint8_t list[NESCV_LIST_MAX];

nescv_init(&enc);

// when the emulator has drawn a frame, if a picture is wanted
if (nescv_wants_picture(&enc)) {
    for (int r = 0; r < NESCV_ROWS; r++)          // band r: 8 lines from line 8r
        nescv_band(&enc, r, pixels + r * 8 * pitch, pitch, lut);
    nescv_band_done(&enc, ++index, C_BLACK);      // C_BLACK: the border
}

// once each TV frame (cvf_frames() has moved on), in the plugin's loop
if (!pending) {
    n = nescv_tv_frame(&enc, psg, npsg, list);   // psg: this frame's PSG bytes
    pending = true;
}
if (pending && cvf_publish(list, n)) pending = false;   // (false: the last one isn't taken yet)
```

`lut` maps the emulator's pixel bytes to colours 1-15. With `lut` NULL
the pixels are colours 1-15 already, and the encoder takes a faster path
that keys each row by its eight colours.

A picture that changes in one place only (a handheld's screen inside a
border, say) can send just those cells: `nescv_band_start()`, then
`nescv_band_cols(e, r, px, pitch, lut, c0, c1)` for columns `c0` to
`c1 - 1` of each band that changed, then `nescv_band_done()`. The cells
left out keep what the last picture had.

`nescv_vram()` and `nescv_reg()` put the plugin's writes (sprites, a
register) at the start of the next list. `nescv_psg()` makes PSG bytes
from a NES APU's state, for a NES-like sound chip. Everything else is in
`nescv.h`.

## Switches

| Define | What |
|---|---|
| `NESCV_FAST_KEYS` | compares cells four words at a time instead of with `memcmp`, which newlib does a byte at a time. The pictures are the same, only faster. The NES plugin leaves it out only to stay what ran on its console tests |
| `NESCV_MEMO_BITS=N` | rows remembered, 2^N at 8 bytes each (12 by default: 32 KB). 10 saves 24 KB, at the cost of more rows worked out again |
| `NESCV_LUTS` | `nescv_band_luts()`, four tables for a picture dithered as it goes in |
| `NESCV_BESIDE` | `nescv_cells()`, `nescv_offer_cells()` and `nescv_prepare()`, for a picture made over several TV frames beside the encoder |

## Cost on the cart

About 10 KB of code and tables with `-O3`, and `nescv_t` is 92 KB of RAM
(68 KB with `NESCV_MEMO_BITS=10`). A busy picture can need more writes
than one TV frame has time for: it then takes two or three frames, and
the pictures in between are skipped. A third of the screen with more
than 255 different cells keeps some cells' old look for a picture
(`stats.held`).

`lib/test.sh` runs its smoke test, `test.c`, with and without
`NESCV_FAST_KEYS`: pictures go through the encoder, its lists run on a
model of the console's video RAM, and the screen read back must match
pixel for pixel.

## License

The GNU LGPL 2.1 (`LICENSE`), as the NES plugin it came from. A plugin
that uses it keeps it under the LGPL: say so in the plugin's README and
`THIRD-PARTY.md`, and link to the source of the release, as
`docs/release-checklist.md` says for GPL and LGPL code. The rest of the
plugin can stay under its own license, as long as a user could rebuild
the plugin with a changed `nescv.c` (shipping the plugin's source does
that).
