#ifndef BACKUP_MANAGER_H
#define BACKUP_MANAGER_H

#include <QObject>
#include <QString>

#include <functional>

class QMenu;
class QWidget;

// Manages the two snapshot mechanisms for the modlist file:
//   · Rotating auto-backups: <livePath>.bak.<stamp>, written by safefs on
//     every save. Surfaced via showRestoreBackupDialog().
//   · User-marked checkpoints: <livePath>.good.<stamp>, written only by
//     explicit "Mark current" action. Surfaced via the toolbar dropdown.
class BackupManager : public QObject {
    Q_OBJECT
public:
    using LivePathFn = std::function<QString()>;
    using SaveFn     = std::function<void()>;

    BackupManager(LivePathFn livePath, SaveFn saveBeforeMark,
                  QObject *parent = nullptr);

    // Modal "restore backup" dialog (rotating .bak.<stamp> snapshots).
    void showRestoreBackupDialog(QWidget *parent);

    // User-marked checkpoint flow.
    void markCurrentAsGoodState(QWidget *parent);
    void populateGoodStatesMenu(QMenu *menu, QWidget *parent);

signals:
    // After a successful restore, MainWindow does loadModList +
    // reconcileLoadOrder + updateModCount + updateSectionCounts +
    // scheduleConflictScan; if fullSync==true (good-state restore) it also
    // calls syncGameConfig.
    void restoredFromDisk(bool fullSync);

    // Routed to the status bar.
    void statusMessage(const QString &msg, int timeoutMs);

private:
    void restoreGoodState(const QString &path, const QString &label,
                          QWidget *parent);
    void deleteGoodState(const QString &path, const QString &label,
                         QWidget *parent);

    LivePathFn m_livePath;
    SaveFn     m_saveBeforeMark;
};

#endif // BACKUP_MANAGER_H
