# Nerevarine Organizer

A native Linux mod manager for OpenMW.

## 0.77 - in development

The next release, built from `main`. Its notes grow in
[`docs/release-notes/0.77.md`](docs/release-notes/0.77.md) as changes land.

### Latest release: 0.76.1

Download: [AppImage or plain Linux
binary](https://github.com/grazelandsnomad/nerevarine_organizer/releases/tag/v0.76.1).
The AppImage is portable and runs on most distros, Steam Deck included. It
needs FUSE 2 (`libfuse2t64` or `libfuse2` on Ubuntu and Debian, `fuse2` on
Arch); without it, start it with `--appimage-extract-and-run`. The plain
binary keeps its mod lists and settings next to itself, so unpack a new
version over the old folder to keep them.

It brings, among the rest:

- **0.76.1:** switching game and back no longer claims your mod list changed
  and asks you to deploy again.
- **Move Mod Library moves the whole library.** It used to carry only the
  folders a row pointed at directly, leaving a library of download folders
  largely where it was while reporting success; and picking a disk root
  strewed the mods over it. Now everything travels, into a folder of its own
  on a disk root, and the summary says what stayed behind.
- **Cyberpunk 2077 is a normal entry in the game menu**, run against a real
  install at last. A mod that is one bare `.archive` reaches
  `archive/pc/mod` instead of the install root, and a GOG copy's mod loaders
  are registered in the prefix Heroic actually uses.
- **Diagnose Last OpenMW Run reads missing-object errors**, one entry per
  object with a repeat count, and names the mod that places it.
- **A download for a game you have no profile for offers to add it**, finds
  the install and puts the mod in its own list, instead of leaving you the
  choice between the wrong list and nothing.
- **Start greys out and says Running** while the game is up, so a second
  click cannot launch it again.

Guides: [mod conflicts and how to fix each kind](docs/conflicts.md).

Full notes: [`docs/release-notes/0.76.1.md`](docs/release-notes/0.76.1.md)
(prior: [0.76](docs/release-notes/0.76.md), [0.75](docs/release-notes/0.75.md),
[0.74](docs/release-notes/0.74.md))

# Tech Stack
C++26 and Qt6.

### Build from source

CachyOS / Arch / Manjaro:
```sh
sudo pacman -S --noconfirm qt6-base qt6-networkauth qt6-svg qtkeychain cmake ninja p7zip
```

Ubuntu / Debian:
```sh
sudo apt install qt6-base-dev qt6-networkauth-dev qtkeychain-qt6-dev cmake ninja-build p7zip-full
```

Fedora:
```sh
sudo dnf install qt6-qtbase-devel qt6-qtnetworkauth-devel qt6keychain-devel cmake ninja-build p7zip
```

Then:
```sh
cmake -S . -B build -GNinja
cmake --build build
./build/nerevarine_organizer
```

### Portable release build (AppImage + plain binary)

Release artifacts are built **inside an old-glibc container** so they run on
normal distros regardless of how new your build host's glibc is - this is what
prevents the `version `GLIBC_2.xx' not found` failures on Fedora / Steam Deck.
Needs Docker or Podman:

```sh
./build-portable.sh            # AppImage + plain release, both portable
./build-portable.sh appimage   # AppImage only
```

See `packaging/Dockerfile.portable` for the base image (Ubuntu 24.04 = glibc
2.39) and the rationale. Don't ship an AppImage built directly on a
bleeding-edge host - `build-appimage.sh` now aborts if a bundled library still
requires a glibc newer than 2.41, but the container is the real fix.

### Optional runtime tools

- [LOOT](https://loot.github.io/) for plugin load-order sorting. Invoked
  on-demand from the toolbar; if missing, the sort step is skipped.
- `unrar` for `.rar` extraction (preferred over 7z). `unzip` for `.zip`.
- A **LibreTranslate-compatible server** if you would rather not use Google's
  free endpoint for machine translation (Settings -> Machine translation
  provider). One way to get one:
  ```sh
  docker run -d -p 5000:5000 libretranslate/libretranslate --load-only en,es
  ```
  First start downloads the language models; after that it works offline.

## Contributing

Bug reports and pull requests are welcome.

### Codebase

| Area | File |
|---|---|
| Main window, toolbar, modlist, download queue, config sync | `src/mainwindow.cpp` |
| Pure `openmw.cfg` renderer (testable, no Qt widgets) | `src/openmwconfigwriter.cpp` |
| TES3 `.bsa` TOC reader (conflict inspector) | `src/bsareader.cpp` |
| Which mod wins a shared file (load-order aware) | `src/conflict_direction.cpp` |
| TES4-family record reader (plugin-level clashes) | `src/plugin_records.cpp` |
| Backup / cross-FS copy + verify | `src/safe_fs.cpp` |
| Same-modpage auto-link, dep resolver | `src/deps_resolver.cpp` |
| TES3 plugin parsing + recursive data scan | `src/pluginparser.cpp` |
| Path / name sanitisation | `include/fs_utils.h` |
| Annotation codec (modlist storage) | `include/annotation_codec.h` |
| First-run wizard | `src/firstrunwizard.cpp` |
| Mod list delegate (custom painter) | `src/modlistdelegate.cpp` |
| Separator dialog + presets | `src/separatordialog.cpp` |
| FOMOD installer | `src/fomodwizard.cpp` |
| Translator (.ini-based) | `src/translator.cpp` |

### Tests

```sh
ctest --test-dir build -L unit       # offline tests
ctest --test-dir build               # also run integration (needs a Nexus API key)
```
Every suite is one file in `tests/`, linked against the `nerevarine_core`
library that holds everything but `main()`; a new suite is its file plus one
line in `tests/CMakeLists.txt`.
CI runs `ctest -L unit` on Ubuntu 24.04 with GCC 14 / Qt 6 from the
distro repos, with warnings as errors.

Formatting: `.clang-format` describes the house style. Run `git clang-format`
on your changes before committing - changed lines only, never a whole-file
reformat. The integration tests need KIO `.protocol` files
registered and a live Nexus API key, so they only run locally.

## License

GPL-3.0-or-later. See `LICENSE`. Derivative works must be released under
the same terms. If you ship a fork, the source has to ship with it.

## Disclaimer

Use at your own risk. The software is provided "AS IS" without warranty.
The author is not responsible for lost or corrupted modlists, broken
`openmw.cfg` / `SkyrimPrefs.ini` / other game configs, broken load
orders, NexusMods account issues, or any other consequence of running
the app. Back up your mods directory, modlist file and game configs
before major version bumps or running "Move mod library", INI-tuning,
or auto-sort.

## AI disclosure

This tool was programmed with assistance of Claude and other AI tools.

## Credits

- App icon: [Crystal](https://opengameart.org/content/crystal) by
  Aeynit, CC BY 3.0.
- Plugin load-order sorting via [LOOT](https://loot.github.io/)
  (GPL-3.0), invoked through its CLI. No LOOT code is linked into the
  binary.
- FOMOD installer format by Black Tree Gaming (NexusMods).
- Credential storage via
  [qtkeychain](https://github.com/frankosterfeld/qtkeychain) (BSD).
