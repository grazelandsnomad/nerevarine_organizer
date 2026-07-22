#include "report_dialog.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QFont>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>
#include <Qt>

namespace ui {
namespace {

// Shared body: optional boxed summary above a read-only monospace view. The
// caller adds its own button box, which is the only thing that differs between
// the read-only report and the confirm variant.
QVBoxLayout *buildReportBody(QDialog &dlg, const QString &title,
                             const QString &text, int minWidth, int minHeight,
                             const QString &summary)
{
    dlg.setWindowTitle(title);
    dlg.setMinimumSize(minWidth, minHeight);
    auto *v = new QVBoxLayout(&dlg);

    if (!summary.isEmpty()) {
        auto *sumLbl = new QLabel(summary, &dlg);
        sumLbl->setTextInteractionFlags(Qt::TextSelectableByMouse);
        // palette() rather than fixed hex: a hardcoded light background left
        // the (theme-coloured) text unreadable once dark mode was on.
        sumLbl->setStyleSheet(
            "background: palette(alternate-base); color: palette(text); "
            "padding: 8px; border-radius: 4px; font-family: monospace;");
        v->addWidget(sumLbl);
    }

    auto *te = new QPlainTextEdit(&dlg);
    te->setReadOnly(true);
    te->setLineWrapMode(QPlainTextEdit::NoWrap);
    QFont mono(QStringLiteral("monospace"));
    mono.setStyleHint(QFont::Monospace);
    te->setFont(mono);
    te->setPlainText(text);
    v->addWidget(te, 1);

    return v;
}

} // namespace

void showMonospaceReport(QWidget *parent, const QString &title,
                         const QString &text, int minWidth, int minHeight,
                         const QString &summary)
{
    QDialog dlg(parent);
    QVBoxLayout *v = buildReportBody(dlg, title, text, minWidth, minHeight, summary);

    auto *btns = new QDialogButtonBox(QDialogButtonBox::Close, &dlg);
    QObject::connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    v->addWidget(btns);

    dlg.exec();
}

bool confirmMonospaceReport(QWidget *parent, const QString &title,
                            const QString &text, int minWidth, int minHeight,
                            const QString &summary, const QString &acceptLabel)
{
    QDialog dlg(parent);
    QVBoxLayout *v = buildReportBody(dlg, title, text, minWidth, minHeight, summary);

    auto *btns = new QDialogButtonBox(QDialogButtonBox::Cancel, &dlg);
    QPushButton *accept =
        btns->addButton(acceptLabel, QDialogButtonBox::AcceptRole);
    // Cancel keeps focus: this dialog fronts a recursive delete, so Enter must
    // never be the destructive answer.
    btns->button(QDialogButtonBox::Cancel)->setDefault(true);
    accept->setAutoDefault(false);
    QObject::connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    v->addWidget(btns);

    return dlg.exec() == QDialog::Accepted;
}

} // namespace ui
