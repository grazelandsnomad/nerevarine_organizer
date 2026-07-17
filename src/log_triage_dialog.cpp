#include "log_triage_dialog.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QFont>
#include <QLabel>
#include <QList>
#include <QMap>
#include <QPlainTextEdit>
#include <QVBoxLayout>
#include <QWidget>
#include <Qt>

#include <algorithm>

#include "translator.h"

namespace openmw {

namespace {

// "Most actionable first": the master-order crashes, then plugins that don't
// exist, then missing assets, then anything we couldn't classify.
int kindRank(LogIssueKind k)
{
    switch (k) {
        case LogIssueKind::MissingMaster:  return 0;
        case LogIssueKind::MissingPlugin:  return 1;
        case LogIssueKind::MissingAsset:   return 2;
        case LogIssueKind::OtherError:     return 3;
    }
    return 4;
}

QString kindLabel(LogIssueKind k)
{
    switch (k) {
        case LogIssueKind::MissingMaster:  return T("log_triage_kind_master");
        case LogIssueKind::MissingPlugin:  return T("log_triage_kind_plugin");
        case LogIssueKind::MissingAsset:   return T("log_triage_kind_asset");
        case LogIssueKind::OtherError:     return T("log_triage_kind_other");
    }
    return QString();
}

} // namespace

void showTriageDialog(QWidget *parent, const LogTriageReport &report,
                      const QString &logPath)
{
    // -- Group issues by suspect mod for the display ---
    //
    // Mods that show up in the log get a named group; issues with no resolved
    // suspect land under a single "Unattributed" bucket so the user still sees
    // them.
    const QString unattributed = T("log_triage_unattributed");
    QMap<QString, QList<LogIssue>> grouped;
    for (const LogIssue &i : report.issues) {
        const QString key = i.suspectMod.isEmpty() ? unattributed : i.suspectMod;
        grouped[key].append(i);
    }
    for (auto it = grouped.begin(); it != grouped.end(); ++it) {
        std::sort(it.value().begin(), it.value().end(),
                  [](const LogIssue &a, const LogIssue &b) {
            const int ra = kindRank(a.kind);
            const int rb = kindRank(b.kind);
            if (ra != rb) return ra < rb;
            return a.target.compare(b.target, Qt::CaseInsensitive) < 0;
        });
    }

    // -- Render ---
    QString summary;
    summary += QString("%1: %2\n").arg(T("log_triage_log_path"), logPath);
    summary += T("log_triage_summary_counts")
               .arg(report.errorLines)
               .arg(report.issues.size())
               .arg(grouped.size()) + "\n";

    QString body;
    if (report.issues.isEmpty()) {
        body = T("log_triage_none");
    } else {
        for (auto it = grouped.constBegin(); it != grouped.constEnd(); ++it) {
            const QString &mod = it.key();
            body += QString("\n=== %1 ===\n").arg(mod);
            for (const LogIssue &i : it.value()) {
                body += "  [" + kindLabel(i.kind) + "] " + i.target;
                if (!i.parent.isEmpty())
                    body += " → " + T("log_triage_needs") + " " + i.parent;
                body += "\n";
                body += "      " + i.detail.trimmed() + "\n";
            }
        }
    }

    QDialog dlg(parent);
    dlg.setWindowTitle(T("log_triage_title"));
    dlg.setMinimumSize(820, 560);
    auto *v = new QVBoxLayout(&dlg);

    auto *sumLbl = new QLabel(summary, &dlg);
    sumLbl->setTextInteractionFlags(Qt::TextSelectableByMouse);
    sumLbl->setStyleSheet(
        "background: #f4f1ee; padding: 8px; border-radius: 4px; "
        "font-family: monospace;");
    v->addWidget(sumLbl);

    auto *bodyEdit = new QPlainTextEdit(&dlg);
    bodyEdit->setReadOnly(true);
    bodyEdit->setLineWrapMode(QPlainTextEdit::NoWrap);
    bodyEdit->setFont(QFont("monospace"));
    bodyEdit->setPlainText(body);
    v->addWidget(bodyEdit, 1);

    auto *btns = new QDialogButtonBox(QDialogButtonBox::Close, &dlg);
    v->addWidget(btns);
    QObject::connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    dlg.exec();
}

} // namespace openmw
