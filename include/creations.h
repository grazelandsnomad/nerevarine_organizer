#pragma once

// creations - Bethesda.net Creations as mod-list rows.
//
// Bethesda.net offers no download. A Creation's page has one button, GET
// IN-GAME, and the game's own Creations menu fetches the files straight into
// Data/'s root, lower-cased, recording each one in ContentCatalog.txt beside
// Plugins.txt:
//
//   {"CSV2_<uuid>": {"Files": ["x - main.ba2", "x.esl"], "FilesSize": N,
//                    "Timestamp": N, "Title": "...", "Version": "...",
//                    "AchievementSafe": true}, ...}
//
// The uuid is the one in the page's address, so a pasted link and a download
// meet by id. Creation Club items are in the catalog too; the game lists their
// plugins in <game>/*.ccc and always loads them, so those stay the game's.
//
// This is the pure part: the link, the catalog, the decision of what to take
// over, and the move of the files into the mod library. DeployFlow owns the
// paths, the rows and the timing: files move only inside a deploy, which links
// them straight back, so the game never loses them.

#include "modentry.h"

#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QUuid>

#include <functional>
#include <optional>

namespace creations {

// -- The link ---------------------------------------------------------------

struct CreationRef {
    QString gameSlug;   // the site's game segment: "fallout4", "skyrim", "starfield"
    QString id;         // the uuid, lower-cased
    QString slug;       // the readable tail ("FEVer_Dreams"); may be empty
};

// https://creations.bethesda.net/<lang>/<game>/details/<uuid>[/<slug>], the
// lang segment optional, host case and "www." ignored, query and fragment
// ignored, a missing scheme tolerated. nullopt for anything else.
std::optional<CreationRef> parseCreationUrl(const QString &url);

// https://creations.bethesda.net/en/<game>/details/<id>[/<slug>]
QString canonicalUrl(const CreationRef &ref);

// The slug as a name: percent-decoded, '_' as spaces, whitespace collapsed.
// The id when there is no slug. The catalog's Title replaces it once the game
// has downloaded the Creation.
QString provisionalTitle(const CreationRef &ref);

// The profile ids a site game slug can stand for ("skyrim" is the page for
// both Skyrim profiles). Empty for a slug this manager has no game for.
QStringList profileIdsForSlug(const QString &gameSlug);

// The inverse: the site's slug for a profile id, empty when the site has no
// pages for that game (so the "Add from Bethesda.net" action can hide).
QString slugForProfile(const QString &gameId);

// -- The game's catalog -----------------------------------------------------

struct CatalogEntry {
    QString     key;        // as in the file ("CSV2_<uuid>")
    QString     id;         // the uuid, lower-cased
    QString     title;
    QString     version;
    QStringList files;      // bare names at Data/'s root, as the catalog spells them
    qint64      size = 0;   // FilesSize
    qint64      timestamp = 0;
};

// Entries with a non-empty Files list; bad JSON gives none.
QList<CatalogEntry> parseContentCatalog(const QByteArray &json);

// Where the catalog may sit, in probe order: Fallout 4 keeps it beside
// Plugins.txt (AppData/Local/<game>), Starfield under My Games/<game>.
QStringList catalogCandidates(const QStringList &localAppDataDirs,
                              const QStringList &myGamesDirs);

// The plugins the game always loads: every *.ccc in the game root, lines
// ending in a plugin extension, lower-cased. Fallout4IDs.ccc (numbers) drops out.
QSet<QString> cccPlugins(const QString &gameRoot);

// -- What Data/ holds -------------------------------------------------------

struct DataFile {
    QString spelling;        // the name as on disk
    bool    symlink = false; // a link we (or someone) put there, not a download
};

// Data/'s root files, keyed by lower-cased name.
QHash<QString, DataFile> dataRootFiles(const QString &dataDir);

// -- Where everything is ----------------------------------------------------

// Filled by the flow from its path resolution; a test builds one by hand.
struct Paths {
    QString dataDir;        // the game's Data/
    QString catalogPath;    // ContentCatalog.txt; empty while the game has none
    QString gameRoot;       // where the *.ccc live
    QString manifestPath;   // this profile's Data/ deploy manifest
};

// -- The decision -----------------------------------------------------------

struct RowInfo {
    QUuid   rowId;
    bool    installed = false;
    QString version;    // ModEntry::creationVersion
    QString modPath;
    QString label;      // the name the deploy records as the mod (sourceMod)
};

struct Adoption {
    QUuid        rowId;             // null: no row yet, make one
    CatalogEntry entry;
    bool         reinstall = false; // the game re-downloaded a newer version
};

struct Skipped {
    QString title;  // the Creation
    QString file;   // its file that stops it
    QString label;  // the mod of ours that deploys that name
};

struct Plan {
    QList<Adoption>     adopt;        // files are there: take them over
    QList<CatalogEntry> unrequested;  // downloaded, no row, not ignored
    QList<Adoption>     notYet;       // a row waits, files not all there
    QList<Skipped>      skipped;      // a file of it is another mod's
};

// rowsById: the rows whose URL is a Creation, keyed by its id (installed ones
// too, for re-adoption). dataRoot: dataRootFiles(). cccLower: cccPlugins().
// deployedLowerToLabel: the manifest's root-level rel paths, lower-cased, to
// the mod label that deployed them. ignoredIds: Creations whose row the user
// removed before it was adopted.
Plan planAdoption(const QList<CatalogEntry> &catalog,
                  const QHash<QString, RowInfo> &rowsById,
                  const QHash<QString, DataFile> &dataRoot,
                  const QSet<QString> &cccLower,
                  const QHash<QString, QString> &deployedLowerToLabel,
                  const QSet<QString> &ignoredIds);

// -- The move ---------------------------------------------------------------

// <modsDir>/<sanitized title>, with _2, _3... when that folder exists.
QString destFolderFor(const QString &modsDir, const QString &title);

using MoveProgress = std::function<void(qint64 doneBytes, qint64 totalBytes)>;

struct MoveResult {
    QStringList moved;   // the names moved (Data/'s spelling)
    QString     failed;  // the file the move stopped at; empty = all moved
    QString     error;   // why
};

// Move each catalog file from Data/'s root (matched ignoring case) into
// destDir, keeping Data/'s spelling. Rename when the filesystems allow, else a
// streamed copy, size-checked, then the source removed. A symlink source is
// refused. On failure everything is put back and Data/ is as before.
MoveResult moveCreationFiles(const QString &dataDir, const QStringList &catalogFiles,
                             const QString &destDir, const MoveProgress &progress = {});

// -- The rows ---------------------------------------------------------------

// A row that waits for the game's download: not installed, ticked, named
// provisionally, its URL the canonical page.
ModEntry waitingRow(const CreationRef &ref, const QString &tooltip);

// The row once its files sit in destDir: installed there, named from the
// catalog, versioned, its URL set when it had none.
void applyAdoption(ModEntry &e, const CatalogEntry &entry, const QString &destDir,
                   const QString &url);

} // namespace creations
