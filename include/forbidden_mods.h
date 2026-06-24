#ifndef FORBIDDEN_MODS_H
#define FORBIDDEN_MODS_H

#include <QList>
#include <QObject>
#include <QString>

class QWidget;

struct ForbiddenMod {
    QString name;
    QString url;
    QString annotation;
};

class ForbiddenModsRegistry : public QObject {
    Q_OBJECT
public:
    explicit ForbiddenModsRegistry(QObject *parent = nullptr);

    // (Re)bind to a game's forbidden-mods file and load it.  Forbidden lists
    // are per-game so the manager dialog and the install-time block only ever
    // see the active game's entries - a Morrowind/OpenMW entry can never match
    // an Oblivion install, so showing it under Oblivion was pure noise.
    //
    // `filePath` is the per-game file (forbidden_mods_<gameId>.txt). The first
    // time the morrowind file is loaded it one-time-migrates the legacy single
    // `forbidden_mods.txt` (every historical entry is a Morrowind/OpenMW mod)
    // at `legacyPath` and seeds the built-in entry; other games start empty.
    // Idempotent. Call at startup and on every game switch.
    void reload(const QString &filePath, const QString &gameId,
                const QString &legacyPath);

    // Lookup by Nexus identifier. Returns nullptr if not on the list.
    const ForbiddenMod *find(const QString &game, int modId) const;

    int size() const { return m_list.size(); }

    // Modal manage-list dialog (table + add/edit/remove). Saves on edits.
    void showManageDialog(QWidget *parent);

private:
    void load();
    void save();

    QString             m_filePath;
    QString             m_gameId;
    QString             m_legacyPath;
    QList<ForbiddenMod> m_list;
};

#endif // FORBIDDEN_MODS_H
