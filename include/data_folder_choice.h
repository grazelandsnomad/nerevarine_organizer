#ifndef DATA_FOLDER_CHOICE_H
#define DATA_FOLDER_CHOICE_H

// Which folders inside one mod the game is told to load, and which of them
// the user has switched off.
//
// A mod can hold several folders that each look like mod data: a BAIN set
// whose packages were all installed, a texture pack shipping 1024/ and 2048/,
// a compilation with one folder per variant. Every one of them is registered,
// and until now nothing showed that, let alone allowed changing it (GitHub
// issue #2: "all of them are autoregistered but this info is really nowhere
// to be seen/changed").
//
// The exclusion itself is not new: the prompt that offers to re-enable a
// bundled patch has always written "<modPath>\t<folder>" into the declined
// set, which the config writer consults. This names that key format, lists
// the folders a mod offers, and marks the ones currently switched off, so a
// dialog can show the lot and write the same keys back.

#include <QList>
#include <QPair>
#include <QSet>
#include <QString>
#include <QStringList>

namespace data_folder_choice {

// The key one folder is stored under. The relative path, so two folders
// named "textures" under different packages are different keys; for a
// top-level folder that is just its name, which is what the patch prompt
// has always written, so keys already on disk keep matching.
QString key(const QString &modPath, const QString &folderPath);

struct Folder {
    QString     path;              // absolute
    QString     label;             // relative to the mod folder
    QStringList plugins;           // content files directly inside, may be empty
    bool        registered = true; // false once the user switches it off
};

// Every folder of this mod the game would be told about, in the order the
// config writer emits them, each marked with whether it is still registered.
// pluginDirs is what the plugin scan found; resourceRoots is the fallback a
// mod with no plugins at all gets. Both are passed in so this stays free of
// the filesystem.
QList<Folder> folders(const QString &modPath,
                      const QList<QPair<QString, QStringList>> &pluginDirs,
                      const QStringList &resourceRoots,
                      const QSet<QString> &declined);

} // namespace data_folder_choice

#endif // DATA_FOLDER_CHOICE_H
