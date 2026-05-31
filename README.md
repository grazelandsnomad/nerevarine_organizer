# Nerevarine Organizer

A native Linux mod manager for OpenMW.

## What's new in 0.5

- **FOMOD case-folder fix.** Installing a FOMOD with patches no longer
  leaves duplicate `Meshes` + `meshes` folders that differ only in case.
  Destinations reconcile against what's already staged (reusing the
  existing folder's casing) and a later patch correctly overwrites the
  base file it replaces.
- **FOMOD optional steps fixed.** Radio-button groups that allow "none"
  (`SelectAtMostOne`, e.g. OAAB_Saplings' patch steps) now offer a *None*
  option and no longer force you to pick a patch.
- Internal: a `ui::` prompt-helper layer (~80 `QMessageBox` sites
  collapsed) and a single `subprocess::` chokepoint for launching
  external programs, so the AppImage Qt env-scrub applies to every LOOT /
  OpenMW Launcher / game launch.

Full notes: [`docs/release-notes/0.5.md`](docs/release-notes/0.5.md)
(prior: [0.4](docs/release-notes/0.4.md), [0.3.1](docs/release-notes/0.3.1.md),
[0.3](docs/release-notes/0.3.md), [0.2](docs/release-notes/0.2.md))

# Tech Stack
C++26 and Qt6.

### Build from source

CachyOS / Arch / Manjaro:
```sh
sudo pacman -S --noconfirm qt6-base qt6-networkauth qt6-svg qtkeychain cmake ninja p7zip
```

Ubuntu / Debian:
```sh
sudo apt install qt6-base-dev qt6-networkauth-dev libqt6keychain-dev cmake ninja-build p7zip-full
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
normal distros regardless of how new your build host's glibc is — this is what
prevents the `version `GLIBC_2.xx' not found` failures on Fedora / Steam Deck.
Needs Docker or Podman:

```sh
./build-portable.sh            # AppImage + plain release, both portable
./build-portable.sh appimage   # AppImage only
```

See `packaging/Dockerfile.portable` for the base image (Ubuntu 24.04 = glibc
2.39) and the rationale. Don't ship an AppImage built directly on a
bleeding-edge host — `build-appimage.sh` now aborts if a bundled library still
requires a glibc newer than 2.41, but the container is the real fix.

### Optional runtime tools

- [LOOT](https://loot.github.io/) for plugin load-order sorting. Invoked
  on-demand from the toolbar; if missing, the sort step is skipped.
- `unrar` for `.rar` extraction (preferred over 7z). `unzip` for `.zip`.

## Contributing

Bug reports and pull requests are welcome.

### Codebase

| Area | File |
|---|---|
| Main window, toolbar, modlist, download queue, config sync | `src/mainwindow.cpp` |
| Pure `openmw.cfg` renderer (testable, no Qt widgets) | `src/openmwconfigwriter.cpp` |
| TES3 `.bsa` TOC reader (conflict inspector) | `src/bsareader.cpp` |
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
CI runs `ctest -L unit` on Ubuntu 24.04 with GCC 14 / Qt 6 from the
distro repos. The integration tests need KIO `.protocol` files
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

## Credits

- App icon: [Crystal](https://opengameart.org/content/crystal) by
  Aeynit, CC BY 3.0.
- Plugin load-order sorting via [LOOT](https://loot.github.io/)
  (GPL-3.0), invoked through its CLI. No LOOT code is linked into the
  binary.
- FOMOD installer format by Black Tree Gaming (NexusMods).
- Credential storage via
  [qtkeychain](https://github.com/frankosterfeld/qtkeychain) (BSD).
