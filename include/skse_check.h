#ifndef SKSE_CHECK_H
#define SKSE_CHECK_H

// skse_check - whether the script-extender plugins on disk can run against
// the game that is installed.
//
// The case this exists for, measured on a real profile: Skyrim updated itself
// to 1.7.99, the user updated SKSE and the Address Library along with it, and
// the game still died at startup with
//
//   REL/ID.h(164): Unsupported address library format: 5
//   This means this script extender plugin is incompatible with the address
//   library available for this version of the game.
//
// Nothing in that message says which mod it is talking about, and it is not
// the mod anybody would suspect. SKSE was right, the Address Library was
// right; what was wrong was every OTHER plugin. The address library ships one
// database per game version, each stamped with a format number, and the one
// for 1.7.99 is format 5 while every database before it is 1 or 2. A plugin
// compiled before that format existed reads the header, does not recognise
// it, and stops the game. Eight mods were in that state and the dialog named
// none of them - it named a DLL, and the first one to load at that.
//
// What can be known for certain, all of it already on disk:
//
//   * the game's own version, out of the executable (see pe_info)
//   * the format of each address-library database, out of its first bytes
//   * for each plugin, when it was built and whether it uses the address
//     library at all - a plugin that scans for byte signatures instead is
//     untouched by any of this and must not be blamed for it
//
// What cannot: whether a given plugin's compiled-in code accepts format 5.
// There is no field for it. So the rule is chronological and says so: a
// plugin built before the game version existed was not built for it.
//
// Pure. Facts in, findings out, no filesystem and no wording - the sentences
// live in the dialog as literal T() keys so the parity check can see them.

#include "pe_info.h"

#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QList>
#include <QString>

namespace skse_check {

// One versionlib-<runtime>.bin / version-<runtime>.bin from the Address
// Library, identified by the header it starts with.
struct Database {
    QString          file;
    pe_info::Version runtime;
    int              format = 0;
    bool valid() const { return format > 0 && runtime.valid; }
};

// One .dll in the game's script-extender plugin folder, with the mod that put
// it there.
struct Plugin {
    QString             file;      // "EngineFixes.dll"
    QString             mod;       // owning mod, empty if the manifest has none
    QDateTime           built;
    pe_info::SksePlugin declared;
};

struct Facts {
    pe_info::Version game;          // from the game executable
    QDateTime        gameBuilt;     // its linker stamp
    QString          loaderFile;    // "skse64_1_7_99.dll", if one is there
    pe_info::Version loaderRuntime; // the runtime that name claims
    QList<Database>  databases;
    QList<Plugin>    plugins;
    // Fallback date anchor: the newest address-library database file's own
    // build stamp, used when the game executable carries no usable one.
    QDateTime        newestDatabase;
};

struct Stale {
    QString          mod;
    QString          file;
    QDateTime        built;
    // The runtime the plugin's own record names, when it names a real one.
    // Authors who set an independence flag sometimes leave a placeholder
    // there (MergeMapper says 1.0.0), so it is only ever extra weight behind
    // the build date, never the reason on its own.
    pe_info::Version declaredFor;
};

struct Findings {
    pe_info::Version game;
    QList<Stale>     stale;
    bool             loaderMismatch  = false;
    pe_info::Version loaderRuntime;
    bool             missingDatabase = false;
    int              databaseFormat  = 0;   // the format that triggered `stale`

    bool empty() const {
        return stale.isEmpty() && !loaderMismatch && !missingDatabase;
    }
};

// Read a database's header: format, then the runtime it is for. Needs the
// first 20 bytes; anything shorter comes back invalid.
Database parseAddressLibraryHeader(const QByteArray &head,
                                   const QString &file = QString());

// Read the facts off disk. FS-touching but path-explicit - every location is
// the caller's to resolve and there is no $HOME, no QSettings and no global
// state here - so the same call a launch makes is the one a test or a probe
// can make against a real install.
//
// `owners` maps a Data-relative path, lowercased, to the mod that deployed it;
// the deploy manifest already records exactly that. A file with no entry keeps
// an empty mod rather than being guessed at.
Facts gather(const QString &gameExePath, const QString &gameRoot,
             const QString &pluginsDir, const QHash<QString, QString> &owners);

// The verdict. Findings are ordered by mod name so the dialog is stable.
Findings evaluate(const Facts &facts);

} // namespace skse_check

#endif // SKSE_CHECK_H
