# Releasing a plugin

A release is a git tag, `vX.Y.Z`, and a GitHub release that carries the
`.CVP` and up to three screenshots. The registry reads releases; it never
builds anything.

## Before you tag

- [ ] **The ID is yours and final** (`docs/plugin-rules.md`). Not
      `yourname.hello`.
- [ ] **`VERSION` in `CMakeLists.txt` is the new version**, higher than
      the last release's.
- [ ] **`README.md` starts with what it is, the controls, and the
      credits** (`docs/readme-spec.md`). Look at it as the cart will show
      it: `python3 sdk/firmware/revc/sdk/cvreadme.py README.md`
- [ ] **Credits, each with a source link**: every core, library, font,
      sound and picture you didn't make, with its author and licence. A
      plugin built with this kit contains the Parrot Cart SDK (MIT) and
      the Raspberry Pi Pico SDK (BSD 3-Clause), and TinyUSB (MIT) if it
      uses USB.
- [ ] **A link to your own source** in the README: the repository, and
      for GPL or LGPL code the exact version released (the tag).
- [ ] **`LICENSE` is your plugin's licence.** The template's is MIT; yours
      can be anything (`docs/plugin-rules.md`, "Sharing a plugin"). If you
      change it, keep `THIRD-PARTY.md` and the MIT text for the SDK's
      part.
- [ ] **The icons** read well in magenta and green on black
      (`docs/icons.md`).
- [ ] **Screenshots** in `screenshots/`: `screenshot-1.png` to
      `screenshot-3.png`, each exactly 256x192 and under 100 KB
      (`docs/screenshots.md`): `python3 tools/png256.py check screenshots/*.png`
- [ ] **It builds from a clean checkout**, and `test/run.sh` passes if you
      use it.
- [ ] **It has run on a real console**, from the card: started from the
      Plugins page, started by opening a file if it opens any, RESET back
      to the menu with its note, the console switched off while it runs
      and on again. `docs/testing.md` has the list.
- [ ] **Release notes** in `RELEASE_NOTES.md` (optional; the workflow uses
      it as the release's body, and the registry shows that): what
      changed, and whether it needs a newer cart firmware. Without the
      file, GitHub lists the commits.

## Tag it

```sh
git tag v1.0.0
git push origin v1.0.0
```

`.github/workflows/release.yml` then builds the plugin on GitHub, checks
that the tag is the version inside the `.CVP`, checks the screenshots,
and makes the release with the `.CVP` and the screenshots attached.
Download the `.CVP` from the release and run that very file on your
console once.

By hand instead (the workflow does no more than this):

```sh
cmake -B build -G Ninja -DPICO_SDK_PATH=$HOME/pico-sdk && ninja -C build
gh release create v1.0.0 build/MYPLUGIN.CVP screenshots/screenshot-*.png \
    --title "My Plugin 1.0.0" --notes-file RELEASE_NOTES.md
```

## Tell the registry

The registry is <https://github.com/plasticbugs/parrot-cart-plugins>, and
its site, with every listed plugin and the icon editor, is
<https://plasticbugs.github.io/parrot-cart-plugins/>.

To have a plugin listed, open an issue there with your repository as
`owner/repo`. You do it once: the registry follows your releases after
that. What it takes from each release:

| From | It shows |
|---|---|
| the `.CVP` attached to the release | the download; the name, version, developer, ID, icons, what it opens and uses, and the README's cart text, all read from the file |
| `screenshot-1.png` .. `screenshot-3.png` attached to the release | the screenshots |
| the release's body | the release notes |
| `README.md` at the tag | the web page's text |

One `.CVP` a release. Plugins are free and are downloaded from your
release, never bundled with a cart.
