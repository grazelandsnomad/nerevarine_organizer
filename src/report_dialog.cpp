#include "report_dialog.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QFont>
#include <QLabel>
#include <QPlainTextEdit>
#include <QVBoxLayout>
#include <QWidget>
#include <Qt>

namespace ui {

void showMonospaceReport(QWidget *parent, const QString &title,
                         const QString &text, int minWidth, int minHeight,
                         const QString &summary)
{
    QDialog dlg(parent);
    dlg.setWindowTitle(title);
    dlg.setMinimumSize(minWidth, minHeight);
    auto *v = new QVBoxLayout(&dlg);

    if (!summary.isEmpty()) {
        auto *sumLbl = new QLabel(summary, &dlg);
        sumLbl->setTextInteractionFlags(Qt::TextSelectableByMouse);
        sumLbl->setStyleSheet(
            "background: #f4f1ee; padding: 8px; border-radius: 4px; "
            "font-family: monospace;");
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

    auto *btns = new QDialogButtonBox(QDialogButtonBox::Close, &dlg);
    QObject::connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    v->addWidget(btns);

    dlg.exec();
}

} // namespace ui
