#ifndef REBOOT_CHECK_H
#define REBOOT_CHECK_H

// Heuristic: returns true when the running kernel has no matching modules
// on disk. Catches the Debian /run/reboot-required marker and the Arch-style
// pacman-wiped /usr/lib/modules/<old> case. NixOS modules under
// /run/{booted,current}-system/kernel-modules are also accepted. Sandboxed
// or atypical layouts can still false-fire, so callers should offer an
// override.
bool isRebootPending();

#endif // REBOOT_CHECK_H
