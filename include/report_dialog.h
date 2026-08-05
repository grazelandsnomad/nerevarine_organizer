#pragma once

// report_dialog - a modal that shows read-only, copyable monospace text with a
// Close button. Several "Inspect ..." slots open-coded the same QPlainTextEdit +
// QDialogButtonBox block; this is that block, once.

#include <QString>

class QWidget;

namespace ui {

// Modal. `text` is shown verbatim in a no-wrap monospace, read-only editor.
// A non-empty `summary` renders as a boxed, selectable header above the body
// (the "Inspect OpenMW setup" shape); empty means body only.
void showMonospaceReport(QWidget *parent, const QString &title,
                         const QString &text, int minWidth, int minHeight,
                         const QString &summary = {});

// Same layout, but with `acceptLabel` and Cancel instead of Close. Returns true
// only when the user picked the accept button. Cancel is the default button:
// the caller is fronting a destructive action, so Enter must not confirm it.
bool confirmMonospaceReport(QWidget *parent, const QString &title,
                            const QString &text, int minWidth, int minHeight,
                            const QString &summary, const QString &acceptLabel);

} // namespace ui
