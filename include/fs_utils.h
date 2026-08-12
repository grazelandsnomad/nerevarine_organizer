#ifndef FS_UTILS_H
#define FS_UTILS_H

// fs_utils - pure filesystem-naming helpers extracted from MainWindow so
// they're reachable by unit tests without linking Qt Widgets.

#include <QChar>
#include <QString>

namespace fsutils {

// Trim an arbitrary string (typically a Nexus mod title) into something safe
// for a folder name on every supported OS.
//
// Rules:
//   · Keep letters, digits, and Unicode code points classified as
//     letter-or-number (QChar::isLetterOrNumber) - non-ASCII names survive.
//   · Keep a small whitelist of punctuation that every major filesystem
//     accepts: space, hyphen, underscore, dot, parens, apostrophe, ampersand.
//   · Other whitespace characters (tab, newline, non-breaking space, …) are
//     folded to a regular space.
//   · Everything else - `/ \ : * ? " < > |` and friends - is dropped.
//   · Internal whitespace runs are collapsed; leading/trailing whitespace
//     stripped (via QString::simplified()).
//
// Worst-case input: empty string / all-invalid input → returns an empty
// QString; callers should fall back to a default name.
inline QString sanitizeFolderName(const QString &s)
{
    QString out; out.reserve(s.size());
    for (QChar c : s) {
        if (c.isLetterOrNumber() || c == ' ' || c == '-' || c == '_' ||
            c == '.' || c == '(' || c == ')' || c == '\'' || c == '&')
            out += c;
        else if (c.isSpace())
            out += ' ';
    }
    return out.simplified();
}

} // namespace fsutils

#endif // FS_UTILS_H
