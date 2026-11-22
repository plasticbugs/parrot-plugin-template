# Icons

A plugin has two icons. Each is 8x8 pixels with one bit per pixel, so a
pixel is either on or off. The menu sets the color.

| Icon | CMake | Where the menu shows it | Color |
|---|---|---|---|
| the plugin icon | `ICON icon.txt` | next to the plugin's name in Apps, in its group on Home and in the Plugin Manager, and on an Open with page | **magenta**, meaning A starts it |
| the file icon | `FILE_ICON file_icon.txt` | next to each file the plugin opens (`OPENS`), in the card's folders | **light green**, meaning A opens it in another system |

Both are drawn on black. You can't choose the color, because the menu
uses color to show what pressing A will do. Yellow folders open, cyan
items play on the console itself, green files open with a plugin, and
magenta items start one. For that reason, every plugin icon is magenta
and every plugin's file is green.

A plugin that only opens files (it has `OPENS` and not `USES
standalone`) shows the menu's plug icon in the Plugin Manager, and its
own icon only on an Open with page. If you leave out `ICON`, an app gets
the menu's window icon. If you leave out `FILE_ICON`, its files get the
plain file icon. An icon with no pixels turned on counts as no icon.

## The file

An icon is a plain text file with 8 lines of 8 characters. Use `#` for a
pixel that's on and `.` for one that's off. The SDK skips blank lines,
and anything else stops the build with an error.

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

In the `.CVP`, each icon is 8 bytes, one byte per row from the top, with
the left pixel in bit 7. The plugin icon is at offset 264 of the header,
and the file icon is at 272.

## Drawing one

You can draw them with the icon editor on the developer portal. It
shows them in the menu's colors and gives you the text to paste:
<https://plasticbugs.github.io/parrot-cart-plugins/developers/tools/>

You can also type the 64 characters yourself.

Here are some tips for icons that read well on a TV at this size:

- Draw a filled silhouette instead of an outline with details. A single
  stray pixel looks like noise on a CRT.
- Make lines two pixels wide where you can, because one-pixel vertical
  lines shimmer on composite video.
- If the shape allows, leave the top row, the bottom row and the right column
  empty, as the menu's icons do. Then icons in a list don't run into each
  other or into the name beside them.
  Icons sit right next to the first letter of the name.
- Make the file icon look related to the plugin icon, such as the same
  shape on a page, so a folder of your files is easy to recognize.

A page that lists plugins (Apps, a group or the Plugin Manager) can show
up to 22 different plugin icons, and an Open with page up to 7. A folder
of files can show up to 6 different file icons. Beyond that, the rest of
the page uses the window or the plug, and the plain file icon.
