#ifndef STORE_SCAN_H
#define STORE_SCAN_H

// store_scan - what Steam, Heroic (GOG) and Lutris already know about where
// games are installed.
//
// The locators in game_profiles.cpp used to answer "where is game X?" purely by
// guessing folder NAMES: Steam by `steamapps/common/<folder>`, Heroic by the
// last component of an entry's install_path, Lutris by substrings of a config
// filename. That fails the moment a store spells the folder differently from
// the name we shipped, and then the user is asked to find their own game.
//
// Measured on the author's machine: Gothic II is installed through Heroic at
// `/mnt/nvme_4TB/Jocs/Gothic 2 Gold`, while the shipped candidates read
// "Gothic II Gold Edition" / "Gothic 2 Gold Edition" / "Gothic II". No match, no
// detection - even though Heroic's own library cache names that install
// "Gothic 2 Gold Edition", which is a candidate we already ship and never read.
//
// So this module gathers the evidence the stores hold:
//
//   * Heroic's gog_store/installed.json  - what is installed, and WHERE
//   * Heroic's store_cache/gog_library.json - what each of those is CALLED
//   * Steam's appmanifest_<appid>.acf    - the authoritative install dir for an
//                                          app id, immune to any renaming
//   * Lutris' per-game yml               - the exe it was configured with
//
// plus a name comparison that survives the spellings these differ by (roman
// numerals, edition words, punctuation).
//
// Parsing is pure: bytes in, values out, no filesystem and no $HOME, so it is
// all unit-tested. The walkers that do touch $HOME are thin wrappers over it.

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

namespace store_scan {

// One entry of Heroic's gog_store/installed.json.
struct HeroicInstall {
    QString appName;      // GOG product id, the join key to the library cache
    QString installPath;  // absolute, and the only place the real folder shows
    QString executable;   // relative; often empty, so never rely on it alone
};

// One entry of Heroic's store_cache/gog_library.json, keyed by appName.
struct StoreTitle {
    QString title;        // "Gothic 2 Gold Edition"
    QString folderName;   // "Gothic 2 Gold"
};

// -- pure parsers ------------------------------------------------------
//
// All three tolerate malformed or truncated input by returning nothing: these
// are other programs' caches, rewritten while we may be reading them.

QList<HeroicInstall>       parseHeroicInstalled(const QByteArray &json);
QHash<QString, StoreTitle> parseHeroicLibrary(const QByteArray &json);

// The Wine/Proton prefix out of Heroic's GamesConfig/<appName>.json, which
// nests everything under the app id: {"<appName>": {"winePrefix": "...", ...}}.
// Empty when the file names no prefix, which is Heroic saying "use the
// default one".
QString parseHeroicGamePrefix(const QByteArray &json, const QString &appName);

// The runner Heroic will use for this game, out of the same file's
// wineVersion object: {"bin": "/.../GE-Proton10-34/proton", "type": "proton"}.
// `bin` empty means the file does not say, which is Heroic falling back to
// whatever its defaults choose - not something to guess at.
//
// Needed because running a game's own exe means reproducing the command
// Heroic would have run, and the two runner types take entirely different
// ones: Proton wants STEAM_COMPAT_DATA_PATH and a "run" verb, Wine wants
// WINEPREFIX and the exe.
struct HeroicRunner {
    QString bin;      // absolute path to the proton script or the wine binary
    QString type;     // "proton", "wine", "toolkit", ... as Heroic spells it
    QString appName;  // the Heroic id the runner was read for (Proton's SteamGameId)
    [[nodiscard]] bool isProton() const { return type.compare(QStringLiteral("proton"),
                                                              Qt::CaseInsensitive) == 0; }
    [[nodiscard]] bool valid() const { return !bin.isEmpty(); }
};
HeroicRunner parseHeroicGameRunner(const QByteArray &json, const QString &appName);
// The same, read off disk for an install path: the app id comes from
// installed.json, the runner from GamesConfig/<appName>.json.
HeroicRunner heroicRunnerForInstall(const QString &installPath);
HeroicRunner heroicRunnerForInstall(const QString &installPath,
                                    const QStringList &configDirs);

// The default prefix root out of Heroic's config.json
// (defaultSettings.winePrefix). Heroic puts each game in a folder named after
// it underneath.
QString parseHeroicDefaultPrefix(const QByteArray &json);

// The "installdir" of a Steam appmanifest_<appid>.acf, empty if absent. This is
// what Steam itself uses, so it is right even when the folder was renamed or
// the game shipped under a different name than its store page.
QString steamInstallDir(const QByteArray &acf);

// -- name comparison ---------------------------------------------------

// Lowercased, punctuation dropped, roman numerals folded to digits ("ii" -> 2)
// and edition noise removed ("edition", "gold", "goty", "game of the year",
// "complete", "enhanced", "definitive", "deluxe", "remastered", "the"), so the
// spellings a store and a mod manager pick for the same game land on the same
// string.
QString normalizeTitle(const QString &s);

// True when every significant token of `wanted` appears in `candidate` once
// both are normalized. Deliberately a subset test, not equality: a store name
// carries edition words and suffixes ours does not.
//
// It is a NAME test only and is not evidence on its own. "Gothic" matches
// "Gothic 2 Gold", and "Skyrim Special Edition" must not be allowed to pick up
// an Enderal install just because both ship SkyrimSE.exe. Callers pair it with
// something checkable on disk.
bool titleMatches(const QString &wanted, const QString &candidate);

// -- the stores on this machine ----------------------------------------
//
// Each takes explicit roots so a test can point it at a QTemporaryDir, with an
// overload that supplies the usual locations (native and flatpak).

QStringList heroicConfigDirs();
QList<HeroicInstall>       heroicInstalls(const QStringList &configDirs);
QList<HeroicInstall>       heroicInstalls();
QHash<QString, StoreTitle> heroicTitles(const QStringList &configDirs);
QHash<QString, StoreTitle> heroicTitles();

// The prefix Heroic runs the game installed at `installPath` in, or empty.
//
// Needed because a prefix is not only a Steam thing: a Heroic copy runs under
// Proton too, in ~/Games/Heroic/Prefixes/..., and the DLL overrides the mod
// loaders need (RED4ext, Cyber Engine Tweaks) have to be written into THAT
// registry. Looks up the per-game GamesConfig file by matching installPath
// against installed.json, then falls back to the default prefix plus the
// install folder's name, which is what Heroic does itself.
//
// The path is returned whether or not it exists yet: Heroic creates the prefix
// on first launch, and the caller is the one that knows whether an absent
// prefix matters.
QString heroicPrefixForInstall(const QString &installPath, const QStringList &configDirs);
QString heroicPrefixForInstall(const QString &installPath);

// Every existing `.../steamapps` directory, from libraryfolders.vdf plus the
// default locations. `steamapps/common` is one level below each.
QStringList steamLibraryRoots();

// Install path of a Steam app id via its appmanifest, empty when not installed
// or when the folder it names is not there.
QString steamAppInstallPath(const QString &appId);
QString steamAppInstallPath(const QStringList &steamAppsDirs, const QString &appId);

// Exe paths Lutris has been configured with, from its per-game ymls.
QStringList lutrisExePaths();

// Everywhere a game could be, from all three stores, for a caller that can
// recognise its own game on sight and does not want to guess at names at all
// (see opengothic::isGameRoot). Paths may be install roots or exes; both are
// useful, since an engine validator can climb.
QStringList allInstallPaths();

} // namespace store_scan

#endif // STORE_SCAN_H
