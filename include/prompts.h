#pragma once

// prompts - thin wrappers over the QMessageBox static calls used at ~190
// sites. Lets a yes/no call read as one:
//
//     if (ui::confirm(this, T("title"), T("body"))) { ... }
//
// Title/body are already-resolved QStrings, so callers keep using T(...) /
// tr(...). Behaviour matches the QMessageBox calls these replace.
//
// Filename is NOT ui_prompts.h: Qt's AUTOUIC treats ui_*.h as generated
// output of a *.ui form and the build then fails looking for prompts.ui.
//
// Covers only the common shapes (Yes/No + the three severities). Bespoke
// button sets / default buttons / inspectable instances should keep using
// QMessageBox directly.

#include <QString>

class QWidget;

namespace ui {

// Yes/No question. True iff the user chose Yes.
bool confirm(QWidget *parent, const QString &title, const QString &body);

// Fire-and-forget notifications. Same as the matching QMessageBox static
// call; differ only in the icon.
void warn(QWidget *parent, const QString &title, const QString &body);
void info(QWidget *parent, const QString &title, const QString &body);
void critical(QWidget *parent, const QString &title, const QString &body);

} // namespace ui
