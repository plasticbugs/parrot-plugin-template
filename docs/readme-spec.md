# The README

Your repository's `README.md` is also your plugin's manual:

- **On the cart**, the plugin's details page shows it, a page at a time,
  30 characters wide, in the menu's upper-case font.
- **On the plugin registry**, your plugin's page shows that same cart
  text, and the `README.md` itself rendered for the web.

You write one file, in ordinary GitHub-flavoured Markdown.
`cvcart_plugin(... README README.md)` converts it when the plugin is
built and stores the text in the `.CVP`.

(Release notes are something else: they are the body of the GitHub
release, and only the registry shows them.)

## What to write

The cart shows the README from the top, and 30 columns is narrow, so
**the top is for the person holding the controller**:

1. One or two sentences: what it is.
2. **Controls.** A list, a line a control.
3. Anything needed to use it: where files go on the card, what to plug in.
4. **Credits**, each with a link to its source: every emulator core,
   library, font, tune and picture that isn't yours, and its license.
5. Your license, and where your own source is.

Then whatever else you like: building, history, thanks. It is on the cart
too, further down.

This repository's own `README.md` is the pattern, and each example has a
short one.

## How Markdown becomes cart text

The converter is `sdk/firmware/revc/sdk/cvreadme.py`. To see what the
cart will show:

```sh
python3 sdk/firmware/revc/sdk/cvreadme.py README.md
```

| You write | The cart shows |
|---|---|
| a heading | the same in capitals, a blank line before and after |
| a paragraph | reflowed to 30 columns |
| a list (`-`, `*`, `+`, `1.`) | `- ` or `1. `, the text hanging under its first word; lists inside lists further in |
| a link, `[text](https://...)` | the text, then the URL; a URL that doesn't fit the rest of the line starts a line of its own |
| a link inside the repository (`LICENSE`, `#controls`) | its text alone |
| `code` | its text |
| a code block | as it is, each line cut at 30 |
| `**bold**`, `*italic*`, `~~struck~~` | the text |
| an image | `[image: its alt text]` |
| a table | a `header: value` line for each cell, a blank line between rows |
| a block quote | 2 columns further in |
| HTML | the tags dropped, their text kept |
| curly quotes, dashes, accents | their plain ASCII look-alikes; anything else `?` |

Things that follow from that:

- **Put a link at the end of its sentence or its list item**, or on a
  line of its own as `<https://...>`. A URL in the middle of a sentence
  lands in the middle of it on the cart too.
- **Prefer lists to tables** for controls. A table's rows become
  "Control: Stick / Does: moves" pairs, which is longer and no clearer.
- **Keep code blocks to 30 columns**, or leave them for the lower part:
  longer lines are cut, not wrapped.
- **Write alt text** for images; it is all the cart shows of them.
- Badges and banners at the top become `[image: ...]` lines on the cart:
  put them lower down, or leave them out.
- The cart's font is upper-case, with digits and `! # & ' ( ) * + , - . /
  : ?`. Other characters are stored as you wrote them (the registry shows
  them) and show as `?` on the TV.

## The limits

- The converted text is at most **16,384 bytes**. Longer, and it is cut
  at a line's end, with a last line saying `[The README is cut here]`.
  That is about 500 lines: a long manual belongs on the web, with a link.
- The text is printable ASCII and line ends, no line longer than 30
  characters. The converter guarantees both.

## In the file

For tools that read `.CVP` files: the README is after the image. The
header's 32-bit little-endian word at offset 280 is its offset from the
start of the file (0: no README), and the word at 284 its length in
bytes. The image and its SHA-256 are the same with or without a README.

A `.txt` file given to `README` is taken as cart text already, and
checked against the limits instead of converted.
