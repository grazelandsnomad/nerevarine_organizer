# Packaging

Three pre-wired distribution formats. Pick the one that matches your target.

| Format | Directory | Status | Best for |
|---|---|---|---|
| Flatpak | `flatpak/` | Draft - needs testing on a clean builder | Sandboxed install, auto-updates, distro-agnostic |
| AppImage | `appimage/` | Script builds against locally-installed Qt6 | Portable single file, no root needed |
| AUR | `aur/` | Both `-git` and stable variants ready | Arch / Manjaro / EndeavourOS / CachyOS |

## Flatpak

```bash
flatpak install --user flathub org.kde.Platform//6.7 org.kde.Sdk//6.7
flatpak-builder --user --install --force-clean build-flatpak \
    packaging/flatpak/io.github.jalcazo.NerevarineOrganizer.yaml
flatpak run io.github.jalcazo.NerevarineOrganizer
```

Caveats that still need work:
- LOOT and OpenMW integrations expect those tools on `PATH`; inside the
  sandbox they aren't. Users will need to install LOOT/OpenMW as flatpaks
  too, and we'd have to call through `flatpak-spawn --host` for runtime
  delegation. For now: treat LOOT/OpenMW integration as "best effort" under
  Flatpak, works on host-run variants (AppImage / AUR) today.
- `nxm://` / `nxms://` handler registration requires the portal's URL
  handler API; the `.protocol` / `.desktop` writes we do in
  `checkNxmHandlerRegistration` won't take effect in the sandbox.
  Workaround: user registers the host's `nerevarine_organizer` as the
  handler before switching to the flatpak version.
- The `qtkeychain` module in the manifest pins `v0.15.0`; update when a
  newer upstream release is out.

## AppImage

```bash
bash packaging/appimage/build-appimage.sh
./Nerevarine_Organizer-0.1.0-x86_64.AppImage
```

Build prerequisites on the host:
- `linuxdeploy` + `linuxdeploy-plugin-qt` (from
  https://github.com/linuxdeploy/linuxdeploy/releases - put them on `PATH`)
- Qt 6 dev headers (Widgets + Network + Concurrent)
- `qtkeychain-qt6` dev headers
- `cmake`, `ninja`, `7z`, `unzip`

Bundles Qt6, qtkeychain, and the app itself. Users only need a recent enough
glibc (2.35+). `libsecret` / `KWallet` still need to be present on the
target system for API-key storage - they're ubiquitous on modern desktops.

## AUR

Two PKGBUILDs are in `aur/`:
- `PKGBUILD-git` - `-git` variant tracking the main branch.
- `PKGBUILD` - stable-release variant; update `pkgver` + re-run
  `updpkgsums` each release.

To publish:
```bash
cp packaging/aur/PKGBUILD-git ~/aur-nerevarine-organizer-git/PKGBUILD
cd ~/aur-nerevarine-organizer-git
updpkgsums
makepkg --printsrcinfo > .SRCINFO
git add PKGBUILD .SRCINFO
git commit -m "nerevarine-organizer-git: initial import"
git push aur main
```

Both PKGBUILDs run the three offline test suites (`test_annotation_codec`,
`test_plugin_parser`, `test_fs_utils`) in the `check()` step.
`test_nxm` is skipped because it hits the network and that's not
guaranteed in clean chroots.
