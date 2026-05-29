#pragma once

// prompts - tiny wrappers over the QMessageBox static calls that were
// hand-rolled at ~190 sites across the app.  The point is readability: a
// call site that asks the user a yes/no question should *read* like one
//
//     if (ui::confirm(this, T("title"), T("body"))) { ... }
//
// instead of spelling out the button set and the `== QMessageBox::Yes`
// comparison every time.  Title/body are taken as already-resolved QStrings
// so callers keep using T(...) / tr(...) exactly as before; behaviour is
// byte-for-byte identical to the QMessageBox calls these replace.
//
// NB filename: deliberately NOT `ui_prompts.h` - Qt's AUTOUIC treats any
// header matching `ui_*.h` as the generated output of a `*.ui` form and
// fails the build looking for `prompts.ui`.  Keep this off the `ui_` prefix.
//
// Scope note: these intentionally cover only the dominant shapes (a Yes/No
// confirmation and the three fire-and-forget severities).  Call sites that
// need bespoke button sets (Yes/No/Cancel, Save/Discard, custom button
// text, a "default button", or an inspectable QMessageBox instance) should
// keep constructing QMessageBox directly - forcing those through a helper
// would hide the very details that make them special.

#include <QString>

class QWidget;

namespace ui {

// Yes/No question.  Returns true iff the user chose Yes.  Mirrors
//   QMessageBox::question(parent, title, body, Yes|No) == QMessageBox::Yes
bool confirm(QWidget *parent, const QString &title, const QString &body);

// Fire-and-forget notifications.  Identical to the matching QMessageBox
// static call; they differ only in the icon (and, for the user, in tone).
void warn(QWidget *parent, const QString &title, const QString &body);
void info(QWidget *parent, const QString &title, const QString &body);
void critical(QWidget *parent, const QString &title, const QString &body);

} // namespace ui
