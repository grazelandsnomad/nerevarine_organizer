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

} // namespace ui
