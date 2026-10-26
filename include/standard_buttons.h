#pragma once

// standard_buttons - a dialog's standard buttons in the app's language.
//
// qt_strings puts Qt's own strings in the language the app shows, and that
// reaches a wizard's Back / Next / Finish. A message box's or a button row's
// OK / Yes / No / Cancel do not ask the translators, though: Qt takes their
// text from the platform theme, and KDE's answers from KDE's own catalogs in
// the DESKTOP's language. On a Catalan Plasma desktop every prompt of the
// English app said "Sí" / "No", translator or not.
//
// install() relabels them as each button row is shown, with the text Qt's
// own default gives, through the translators qt_strings installed - the same
// language as everything else. A button the app has relabelled itself ("Update
// Selected (3)" on an Ok button) is left alone: only a text that is still the
// theme's own is replaced. Icons and keyboard handling stay the theme's.

#include <QDialogButtonBox>
#include <QHash>
#include <QString>

class QApplication;

namespace standard_buttons {

// The text Qt gives `button` when the platform theme has none of its own,
// through the installed translators: "&Yes" in English. Empty for NoButton
// or a button Qt names no text for.
QString text(QDialogButtonBox::StandardButton button);

// Give `box`'s standard buttons text(button) - each whose current text is
// still the one `themeTexts` says the theme gave it (keyed by
// QDialogButtonBox::StandardButton). A button with any other text was set by
// the app and keeps it.
void relabel(QDialogButtonBox *box, const QHash<int, QString> &themeTexts);

// Relabel every QDialogButtonBox (QMessageBox's included) as it is shown.
// Reads the theme's texts once, from a button row nobody sees.
void install(QApplication &app);

} // namespace standard_buttons
