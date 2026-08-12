#include "conflict_inspector.h"

#include <QBrush>
#include <QColor>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFont>
#include <QFutureWatcher>
#include <QLabel>
#include <QLineEdit>
#include <QProgressDialog>
#include <QSet>
#include <QString>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <QWidget>
#include <Qt>
#include <QtConcurrent>

#include <atomic>
#include <memory>

#include "translator.h"

namespace conflict_inspector {

void show(QWidget *parent, const QList<ConflictScanInput> &snapshot)
{
    // --- Run the scan off-thread behind a modal progress dialog with cancel ---
    auto cancel = std::make_shared<std::atomic<bool>>(false);

    QProgressDialog progress(T("conflict_inspector_scanning"),
                             T("conflict_inspector_cancel"),
                             0, 0, parent);
    progress.setWindowTitle(T("conflict_inspector_title"));
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(150);   // tiny scans skip the dialog entirely
    progress.setAutoClose(false);
    progress.setAutoReset(false);

    QFutureWatcher<ConflictMap> watcher;
    QObject::connect(&watcher, &QFutureWatcherBase::finished,
                     &progress, &QProgressDialog::accept);
    QObject::connect(&progress, &QProgressDialog::canceled, &progress,
                     [cancel] { cancel->store(true); });

    watcher.setFuture(QtConcurrent::run(scanConflicts, snapshot, cancel));
    progress.exec();
    watcher.waitForFinished();   // ensure the worker thread has actually stopped

    if (cancel->load()) return;

    const ConflictMap conflicts = watcher.result();

    QSet<QString> modsInConflicts;
    for (auto it = conflicts.constBegin(); it != conflicts.constEnd(); ++it)
        for (const auto &p : it.value()) modsInConflicts.insert(p.mod);

    QDialog dlg(parent);
    dlg.setWindowTitle(T("conflict_inspector_title"));
    dlg.setMinimumSize(820, 560);
    auto *v = new QVBoxLayout(&dlg);

    auto *explainLbl = new QLabel(T("conflict_inspector_explain"), &dlg);
    explainLbl->setWordWrap(true);
    explainLbl->setStyleSheet("color: #444; padding: 4px 2px;");
    v->addWidget(explainLbl);

    if (conflicts.isEmpty()) {
        auto *none = new QLabel(T("conflict_inspector_none"), &dlg);
        none->setStyleSheet("padding: 16px; font-style: italic;");
        v->addWidget(none);
    } else {
        auto *counts = new QLabel(
            T("conflict_inspector_counts")
                .arg(conflicts.size()).arg(modsInConflicts.size()),
            &dlg);
        counts->setStyleSheet("font-weight: bold; padding: 2px;");
        v->addWidget(counts);

        auto *filter = new QLineEdit(&dlg);
        filter->setPlaceholderText(T("conflict_inspector_filter"));
        filter->setClearButtonEnabled(true);
        v->addWidget(filter);

        auto *tree = new QTreeWidget(&dlg);
        tree->setHeaderLabels({"File / Mod", "Data folder"});
        tree->setAlternatingRowColors(true);
        tree->setRootIsDecorated(true);

        // QMap iterates sorted by key, which gives alphabetical relPath order.
        for (auto it = conflicts.constBegin(); it != conflicts.constEnd(); ++it) {
            const auto &providers = it.value();
            auto *top = new QTreeWidgetItem(tree,
                {it.key(), QString("%1 providers").arg(providers.size())});
            for (int p = 0; p < providers.size(); ++p) {
                const bool isWinner = (p == providers.size() - 1);
                QString label = providers[p].mod;
                if (!providers[p].sourceBsa.isEmpty())
                    label += T("conflict_inspector_bsa_marker")
                                 .arg(providers[p].sourceBsa);
                if (isWinner) label += T("conflict_inspector_winner_marker");
                auto *child = new QTreeWidgetItem(top,
                    {label, providers[p].root});
                if (isWinner) {
                    QFont f = child->font(0); f.setBold(true);
                    child->setFont(0, f);
                } else {
                    child->setForeground(0, QBrush(QColor(150, 150, 150)));
                }
            }
        }
        tree->resizeColumnToContents(0);
        v->addWidget(tree, 1);

        QObject::connect(filter, &QLineEdit::textChanged, &dlg,
                         [tree](const QString &q) {
            const QString needle = q.trimmed().toLower();
            for (int i = 0; i < tree->topLevelItemCount(); ++i) {
                auto *item = tree->topLevelItem(i);
                bool match = needle.isEmpty() ||
                             item->text(0).contains(needle);
                if (!match) {
                    for (int c = 0; c < item->childCount(); ++c) {
                        if (item->child(c)->text(0).toLower().contains(needle)) {
                            match = true; break;
                        }
                    }
                }
                item->setHidden(!match);
            }
        });
    }

    auto *btns = new QDialogButtonBox(QDialogButtonBox::Close, &dlg);
    v->addWidget(btns);
    QObject::connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    dlg.exec();
}

} // namespace conflict_inspector
