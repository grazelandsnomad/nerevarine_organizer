#pragma once

// qt_strings - Qt's own dialog strings in the app's language.
//
// The app's text comes from translations/<language>.ini through T(); the
// strings Qt draws by itself - a QWizard's "Next >" and "Finish", a message
// box's "Yes" / "No" / "OK", a file dialog's chrome - come from Qt's qtbase
// translations, which the desktop installs for the SYSTEM locale. On a
// Catalan desktop the English welcome wizard therefore closed on "Acaba"
// and "Cancel·la", and every Yes/No prompt asked "Sí" / "No".
//
// install() makes the two agree: for a language the app shows, the matching
// qtbase translation is loaded when Qt ships one; for English, a translator
// that hands Qt's source strings back is installed on top of whatever the
// desktop put there, since English has no .qm to load - it IS the source.
// Installed last, it is consulted first.
//
// Qt Core only.

#include <QString>

class QCoreApplication;

namespace qt_strings {

// The ISO 639-1 code for one of the app's language file names
// ("catalan" -> "ca"), "" for one it does not know.
QString codeFor(const QString &language);

// Make Qt's strings follow `language` (the one Translator settled on).
// Returns a one-line account of what was done, for the log.
QString install(QCoreApplication &app, const QString &language);

} // namespace qt_strings
