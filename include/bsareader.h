#ifndef BSA_READER_H
#define BSA_READER_H

// Minimal TES3 (Morrowind) .bsa TOC reader.
//
// Pure parser: no Qt widgets, no dependency on MainWindow.  Exists so the
// File Conflict Inspector can see BSA-packed assets alongside loose files
// in OpenMW's VFS precedence view - without it, a BSA retexture silently
// overrides a loose-file mod and the inspector can't explain why.
//
// Non-TES3 BSAs (Oblivion/Skyrim/FO3+ BSA, Fallout 4 .ba2) and malformed
// files return an empty list instead of throwing.  Good enough for the
// Morrowind-focused profile; extending to later Bethesda formats can be
// done by dispatching on magic.

#include <QString>
#include <QStringList>

namespace bsa {

// Enumerate file paths packed inside a TES3-format .bsa archive.
//
// Returns relative paths with forward-slash separators, lowercased - ready
// to use as keys in the conflict inspector, which normalises loose-file
// paths the same way.  An empty list means "unsupported version, malformed
// file, or read error" - treat it as "skip silently".
QStringList listTes3BsaFiles(const QString &bsaPath);

} // namespace bsa

#endif // BSA_READER_H
