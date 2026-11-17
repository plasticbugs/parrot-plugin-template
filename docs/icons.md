# Icons

A plugin has two icons, both 8x8 pixels, one bit a pixel: a pixel is on
or off, and the menu chooses the colour.

| Icon | CMake | Where the menu shows it | Colour |
|---|---|---|---|
| the plugin's | `ICON icon.txt` | beside the plugin's name, on the Plugins page and wherever it is listed as an app | **magenta**: "A starts this" |
| its files' | `FILE_ICON file_icon.txt` | beside every file the plugin opens (`OPENS`), in the card's folders | **light green**: "A opens this in another system" |

Both are on black. The colour isn't yours to choose: in the menu, colour
says what pressing A will do (yellow folders open, cyan things play on the
console itself, green opens with a plugin, magenta starts one), so every
plugin's icon is magenta and every plugin's file is green.

Without `ICON`, the plugin gets the menu's plug; without `FILE_ICON`, its
files get the generic file icon. An icon with no pixels on counts as
none.

## The file

Plain text: 8 lines of 8 characters, `#` for a pixel that is on and `.`
for one that is off. Blank lines are ignored; anything else is an error
at build time.

```
..####..
.#....#.
#..##..#
#......#
#..##..#
#..##..#
.#....#.
..####..
```

In the `.CVP` each icon is 8 bytes, a byte a row from the top, the left
pixel in bit 7 (the plugin's at offset 264 of the header, its files' at
272).

## Drawing one

- **The icon editor** on the plugin registry's site draws them in the
  menu's colours and gives you the text to paste:
  <https://plasticbugs.github.io/parrot-cart-plugins/>
- Or type it: it is 64 characters.

What reads well at this size, on a TV:

- A silhouette, not an outline of details. Fill shapes; a single stray
  pixel is noise on a CRT.
- Lines two pixels wide where you can. One-pixel vertical lines shimmer
  on composite video.
- Leave the right column or the bottom row empty if the shape allows:
  icons sit directly beside the name's first letter.
- Make the file icon a relative of the plugin's icon (the same shape on a
  page, say), so a folder of your files looks like yours.

The menu shows up to 7 different plugin icons and 7 file icons on a page;
past that, the rest of the page falls back to the plug and the generic
file.
