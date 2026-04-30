# Nerevarine Organizer

A native Linux mod manager for OpenMW.

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
