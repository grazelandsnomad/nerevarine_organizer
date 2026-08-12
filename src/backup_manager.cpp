#include "backup_manager.h"

#include <QAbstractItemView>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileInfoList>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QString>
#include <QVBoxLayout>
#include <Qt>

#include "backup_ops.h"
#include "translator.h"
#include "prompts.h"

BackupManager::BackupManager(LivePathFn livePath, SaveFn saveBeforeMark,
                              QObject *parent)
    : QObject(parent),
      m_livePath(std::move(livePath)),
      m_saveBeforeMark(std::move(saveBeforeMark))
{
}

void BackupManager::showRestoreBackupDialog(QWidget *parent)
{
    const QString livePath = m_livePath();
    const QFileInfo liveInfo(livePath);
    const QDir      dir        = liveInfo.dir();
    const QString   liveName   = liveInfo.fileName();
    const QString   bakPattern = liveName + ".bak.*";

    // Sort by name, newest first: the YYYYMMDD-HHMMSS suffix makes
    // lexicographic order == time order, so no stat() needed.
    QFileInfoList snapshots = dir.entryInfoList(
        {bakPattern}, QDir::Files | QDir::Readable, QDir::Name | QDir::Reversed);

    if (snapshots.isEmpty()) {
        ui::info(parent, T("restore_backup_title"), T("restore_backup_none").arg(liveName));
        return;
    }

    QDialog dlg(parent);
    dlg.setWindowTitle(T("restore_backup_title"));
    dlg.setMinimumWidth(620);

    auto *v = new QVBoxLayout(&dlg);

    auto *header = new QLabel(T("restore_backup_body").arg(liveName), &dlg);
    header->setWordWrap(true);
    v->addWidget(header);

    auto *list = new QListWidget(&dlg);
    list->setSelectionMode(QAbstractItemView::SingleSelection);

    const QString stampPrefix = liveName + ".bak.";
    const qint64 liveSize = liveInfo.exists() ? liveInfo.size() : -1;
    for (const QFileInfo &fi : snapshots) {
        const QString fname = fi.fileName();
        const QString stamp = fname.startsWith(stampPrefix)
            ? fname.mid(stampPrefix.size()) : QString();
        const QDateTime dt = QDateTime::fromString(stamp, "yyyyMMdd-HHmmss");
        const QString whenStr = dt.isValid()
            ? dt.toString("yyyy-MM-dd HH:mm:ss")
            : fname;
        const double kb = fi.size() / 1024.0;
        QString sizeTag = QString::number(kb, 'f', 1) + " KB";
        if (liveSize >= 0 && fi.size() != liveSize)
            sizeTag += QString("  (Δ%1 B)").arg(fi.size() - liveSize);

        auto *li = new QListWidgetItem(QString("%1   -   %2").arg(whenStr, sizeTag), list);
        li->setData(Qt::UserRole, fi.absoluteFilePath());
        li->setToolTip(fi.absoluteFilePath());
    }
    list->setCurrentRow(0);
    v->addWidget(list, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, &dlg);
    auto *restoreBtn = buttons->addButton(T("restore_backup_restore"),
                                           QDialogButtonBox::AcceptRole);
    restoreBtn->setDefault(true);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    connect(list, &QListWidget::itemDoubleClicked, &dlg, &QDialog::accept);
    v->addWidget(buttons);

    if (dlg.exec() != QDialog::Accepted) return;
    auto *sel = list->currentItem();
    if (!sel) return;

    const QString chosenPath = sel->data(Qt::UserRole).toString();
    const QString chosenLabel = sel->text();

    if (QMessageBox::question(parent, T("restore_backup_confirm_title"),
            T("restore_backup_confirm_body").arg(chosenLabel, liveName),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;

    if (!backup_ops::restoreSnapshot(livePath, chosenPath)) {
        ui::critical(parent, T("restore_backup_fail_title"),
                     T("restore_backup_fail_body").arg(livePath));
        return;
    }

    emit restoredFromDisk(/*fullSync=*/false);
    emit statusMessage(T("restore_backup_done").arg(chosenLabel), 6000);
}

void BackupManager::markCurrentAsGoodState(QWidget *parent)
{
    const QString livePath = m_livePath();
    if (!QFile::exists(livePath)) {
        ui::warn(parent, T("good_state_mark_title"), T("good_state_mark_nothing").arg(QFileInfo(livePath).fileName()));
        return;
    }

    // Save first so the snapshot captures in-memory state, not the last
    // thing written to disk.
    if (m_saveBeforeMark) m_saveBeforeMark();

    const QString stamp = QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss");
    const auto marked = backup_ops::markGoodState(livePath, stamp);
    if (!marked) {
        ui::critical(parent, T("good_state_mark_fail_title"),
                     T("good_state_mark_fail_body").arg(livePath + ".good." + stamp));
        return;
    }
    emit statusMessage(
        T("good_state_marked").arg(QDateTime::currentDateTime()
            .toString("yyyy-MM-dd HH:mm:ss")), 6000);
}

void BackupManager::writePreSortCheckpoint(const QByteArray &modlistContent)
{
    const QString   livePath = m_livePath();
    const QFileInfo liveInfo(livePath);
    const QDir      dir = liveInfo.dir();
    if (!dir.exists())
        return;

    // Dedupe against the newest good state: Size/Date sorts are view-only and
    // don't change the saved order, so sorting repeatedly must not spawn a run
    // of identical checkpoints. Only a real change since the last one earns a
    // new snapshot.
    const QString pattern = liveInfo.fileName() + ".good.*";
    const QFileInfoList goods = dir.entryInfoList(
        {pattern}, QDir::Files | QDir::Readable, QDir::Name | QDir::Reversed);
    if (!goods.isEmpty()) {
        QFile prev(goods.first().absoluteFilePath());
        if (prev.open(QIODevice::ReadOnly) && prev.readAll() == modlistContent)
            return;
    }

    const QString stamp    = QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss");
    const QString goodPath = livePath + ".good." + stamp;
    if (QFile::exists(goodPath))
        return;   // already checkpointed within this same second

    QFile out(goodPath);
    if (out.open(QIODevice::WriteOnly)) {
        out.write(modlistContent);
        out.close();
        emit statusMessage(T("good_state_presort"), 5000);
    }
}

void BackupManager::populateGoodStatesMenu(QMenu *menu, QWidget *parent)
{
    menu->clear();
    const QString livePath = m_livePath();
    const QFileInfo liveInfo(livePath);
    const QDir dir = liveInfo.dir();
    const QString liveName = liveInfo.fileName();
    const QString stampPrefix = liveName + ".good.";
    const QString pattern     = stampPrefix + "*";

    menu->addAction(T("good_state_mark_action"),
                    this, [this, parent]{ markCurrentAsGoodState(parent); });

    const QFileInfoList snapshots = dir.entryInfoList(
        {pattern}, QDir::Files | QDir::Readable, QDir::Name | QDir::Reversed);

    if (snapshots.isEmpty()) {
        menu->addSeparator();
        auto *empty = menu->addAction(T("good_state_none"));
        empty->setEnabled(false);
        return;
    }

    menu->addSeparator();
    auto *headerLbl = menu->addAction(T("good_state_restore_header"));
    headerLbl->setEnabled(false);

    for (const QFileInfo &fi : snapshots) {
        const QString fname = fi.fileName();
        const QString stamp = fname.startsWith(stampPrefix)
            ? fname.mid(stampPrefix.size()) : QString();
        const QDateTime dt = QDateTime::fromString(stamp, "yyyyMMdd-HHmmss");
        const QString label = dt.isValid()
            ? dt.toString("yyyy-MM-dd HH:mm:ss")
            : fname;

        auto *entryMenu = menu->addMenu(label);
        const QString path = fi.absoluteFilePath();
        entryMenu->addAction(T("good_state_restore_one"),
            this, [this, path, label, parent]{ restoreGoodState(path, label, parent); });
        entryMenu->addAction(T("good_state_delete_one"),
            this, [this, path, label, parent]{ deleteGoodState(path, label, parent); });
    }
}

void BackupManager::restoreGoodState(const QString &path, const QString &label,
                                      QWidget *parent)
{
    const QString livePath = m_livePath();
    if (!QFile::exists(path)) {
        ui::warn(parent, T("good_state_restore_fail_title"), T("good_state_restore_fail_body").arg(path));
        return;
    }

    if (QMessageBox::question(parent, T("good_state_restore_confirm_title"),
            T("good_state_restore_confirm_body").arg(label),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;

    if (!backup_ops::restoreSnapshot(livePath, path)) {
        ui::critical(parent, T("good_state_restore_fail_title"),
                     T("good_state_restore_fail_body").arg(livePath));
        return;
    }

    emit restoredFromDisk(/*fullSync=*/true);
    emit statusMessage(T("good_state_restored").arg(label), 6000);
}

void BackupManager::deleteGoodState(const QString &path, const QString &label,
                                     QWidget *parent)
{
    if (QMessageBox::question(parent, T("good_state_delete_confirm_title"),
            T("good_state_delete_confirm_body").arg(label),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;
    if (!backup_ops::deleteSnapshot(path)) {
        ui::critical(parent, T("good_state_delete_fail_title"),
                     T("good_state_delete_fail_body").arg(path));
        return;
    }
    emit statusMessage(T("good_state_deleted").arg(label), 5000);
}
