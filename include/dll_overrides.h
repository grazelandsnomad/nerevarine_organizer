#pragma once

// dll_overrides - make Wine/Proton actually load the wrapper DLLs a mod drops
// next to the game .exe.
//
// A whole class of mods works by shipping a DLL named after a system library -
// dxgi.dll, d3d9.dll, dinput8.dll - and putting it beside the executable.
// Windows' loader searches the application directory before system32, so the
// mod's copy wins and it chain-loads the real one behind it. That is how
// ReShade, ENB, Souls Mod Engine and Dark Souls II's DS2LightingEngine (the
// path tracing mod) all hook a game.
//
// Wine does NOT do that. Those names resolve to Wine's own builtin
// implementation no matter what sits next to the exe, so the mod deploys
// perfectly, the files are all in the right place, and nothing whatsoever
// happens in game. The prefix has to be told, per name, to prefer the native
// (mod-supplied) DLL:
//
//   [Software\\Wine\\DllOverrides]
//   "dxgi"="native,builtin"
//
// This header is the pure half: transforms over the *text* of a prefix's
// user.reg, no filesystem and no globals, so the splicing is unit-tested.
// Reading and writing the file belongs to the caller, which does it only for
// the prefix of the game being deployed.

#include <QString>
#include <QStringList>

namespace dll_overrides {

// The registry value we write. "native" = the mod's DLL, "builtin" = Wine's,
// tried in that order - so if a mod DLL is removed the game still starts.
inline constexpr char kNativeBuiltin[] = "native,builtin";

// True for DLL names Wine resolves itself and that a mod may legitimately want
// to serve from the game folder instead.
//
// A whitelist on purpose. Mods also ship helper DLLs of their own (an upscaler
// blob, a plugin, a bundled runtime); those are loaded by explicit path, need
// no override, and adding entries for them would litter the prefix with keys
// that do nothing. Accepts a bare name or a file name ("dxgi", "dxgi.dll"),
// case-insensitively.
bool isWrapperDll(const QString &fileName);

// The wrapper DLL names among `relPaths` (deploy-manifest rels), lowercased,
// de-duplicated, sorted. Only top-level paths count: a DLL inside a subfolder
// is not on the loader's search path, so overriding its name would be wrong.
QStringList wrapperDllsIn(const QStringList &relPaths);

// Add "<dll>"="native,builtin" to the [Software\\Wine\\DllOverrides] section of
// `userReg`, creating the section if the prefix has none, and keeping entries
// in the alphabetical order Wine itself writes them.
//
// A name that already has a value is left exactly as it is and reported via
// `skipped`, never `added`: it may be the user's own ReShade override, and
// removeOverrides must never take away something we did not put there.
QString addOverrides(const QString &userReg, const QStringList &dlls,
                     QStringList *added = nullptr, QStringList *skipped = nullptr);

// Remove exactly `dlls` from the section. Every other entry, section and the
// file header are left byte-for-byte alone.
QString removeOverrides(const QString &userReg, const QStringList &dlls,
                        QStringList *removed = nullptr);

} // namespace dll_overrides
