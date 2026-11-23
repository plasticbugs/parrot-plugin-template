# lib/scalen

A picture's pens scaled from N columns to M and N lines to M, keeping
strokes one pixel wide, for pictures that don't scale by 3/4. It uses the
same rules as the SDK's `cvscale34()` (`sdk/firmware/revc/sdk/cvscale.h`)
with the pairs spread evenly, so 4k to 3k gives exactly `cvscale34()`'s
picture. Robotron needed 278 to 256, 215 to 179, 292 to 256 and 240 to
192.

| File | What |
|---|---|
| `scalen.h` | the API, with an example |
| `scalen.c` | the scaler |
| `test.c` | the smoke test |

Add it to the plugin's sources. `lib/` is on the include path already,
and `cvscale.h` comes with every frame plugin (a screen plugin adds
`${PARROTCART_SDK}/sdk/cvscale.c` as well):

```cmake
target_sources(mygame PRIVATE ${PARROTCART_DIR}/lib/scalen/scalen.c)
```

`scalen()` makes a range of output lines, so a plugin can scale one
8-line band at a time into a small buffer instead of keeping the whole
scaled screen. `scalen_row()` and `scalen_line()` are the two halves, for
redrawing only the places that changed.

`lib/test.sh` runs its smoke test, `test.c`.

It's the kit's own code (MIT).
