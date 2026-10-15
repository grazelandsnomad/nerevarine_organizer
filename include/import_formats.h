#pragma once

// import_formats - the parsing half of the modlist imports, with no dialogs
// and no window: MO2's modlist.txt and meta.ini, an MO2 instance's layout, a
// Wabbajack list's JSON, and the sniffing of a dropped text file. Each
// function takes file contents or a path and gives back rows (ModEntry) or
// plain facts; ImportFlow does the asking and the inserting.

#include "modentry.h"

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace import_formats {

// The Nexus mod id an MO2 meta.ini names, or 0 when it is not a Nexus mod
// (no id, or a repository other than Nexus).
int readMO2MetaId(const QString &metaIniPath);

// The +/-/* lines of an MO2 modlist.txt, top of the list first: MO2 writes
// the highest-priority mod LAST, and the list shows it first. Blank lines and
// # comments are dropped.
QStringList mo2ModlistLines(const QString &contents);

// A separator row the way every import builds one.
ModEntry separatorRow(const QString &name);

// One row from an MO2 modlist line. `modsDir` (may be empty) is the MO2 mods
// folder: meta.ini is read there for the Nexus id, and with `linkFolder` the
// mod's folder there becomes the row's path (installed). Without a Nexus id
// the row carries a Nexus search page for its name as its source URL.
// std::nullopt for an unmanaged (*) entry, or a line with no name.
struct MO2Row {
    ModEntry entry;
    bool nexusLinked  = false;   // meta.ini named a Nexus mod
    bool folderLinked = false;   // the folder under modsDir exists (linkFolder)
};
std::optional<MO2Row> mo2Row(const QString &line, const QString &modsDir,
                             const QString &nexusGame, bool linkFolder);

// The mods and profiles folders of an MO2 instance, honouring the overrides
// in ModOrganizer.ini (its @ByteArray(...) wrapping, backslashes, relative
// paths, and a Wine drive letter that does not exist here, which falls back
// to the instance's own folder). `valid` is false when the folder is not an
// MO2 instance at all.
struct MO2Instance {
    QString modsDir;
    QString profilesDir;
    bool    valid = false;
};
MO2Instance resolveMO2Instance(const QString &instanceDir);

// One mod of a Wabbajack list, as its JSON describes it.
struct WJMod {
    QString name;
    QString nexusUrl;
    QString sourceUrl;   // non-Nexus download page (GitHub, direct HTTP, etc.)
    int     nexusId   = 0;
    qint64  sizeBytes = 0;
    bool    isSep     = false;
};
// The Nexus game slug for a Wabbajack GameType.
QString wabbajackGameSlug(const QString &gameType);
// The mods of a parsed .wabbajack "modlist" JSON: vanilla game files and the
// tools Wabbajack bundles for its own install flow are skipped, Nexus and
// GitHub/direct sources are told apart, and one Nexus mod page supplying
// several archives is one mod (the largest file's size, the shortest name).
QList<WJMod> wabbajackMods(const QJsonObject &root);
ModEntry     wabbajackRow(const WJMod &mod);

// What a dropped .txt looks like, from its first +/-/* line: MO2's bare
// names, or Nerevarine's tab-separated export.
enum class ModlistFormat { Unknown, MO2, Nerevarine };
ModlistFormat sniffModlistFormat(const QString &path);

// The mods folder a Nerevarine modlist file was written against, when that
// folder is not here: the common parent of its absolute mod paths that do
// not exist locally. Empty when every path exists, or nothing can be told.
QString detectModsBaseFromFile(const QString &path);

} // namespace import_formats
