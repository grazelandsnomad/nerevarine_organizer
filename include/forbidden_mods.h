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
    ForbiddenModsRegistry(const QString &filePath, QObject *parent = nullptr);

    // File first; on first run the file is missing - migrate from legacy
    // QSettings keys, seed the built-in Wabbajack entry, then write the file.
    // Idempotent. Call once at startup.
    void load();

    // Lookup by Nexus identifier. Returns nullptr if not on the list.
    const ForbiddenMod *find(const QString &game, int modId) const;

    int size() const { return m_list.size(); }

    // Modal manage-list dialog (table + add/edit/remove). Saves on edits.
    void showManageDialog(QWidget *parent);

private:
    void save();

    QString             m_filePath;
    QList<ForbiddenMod> m_list;
};

#endif // FORBIDDEN_MODS_H
