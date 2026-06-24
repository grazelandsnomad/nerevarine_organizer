#ifndef BSA_READER_H
#define BSA_READER_H

// Minimal TES3 (Morrowind) .bsa TOC reader. Pure parser, no Qt widgets.
//
// Lets the File Conflict Inspector see BSA-packed assets next to loose files;
// without it a BSA retexture silently overrides a loose-file mod and the
// inspector can't explain why.
//
// Non-TES3 BSAs (Oblivion/Skyrim/FO3+, Fallout 4 .ba2) and malformed files
// return an empty list rather than throwing. Extend to later formats by
// dispatching on magic.

#include <QString>
#include <QStringList>

namespace bsa {

// List file paths packed in a TES3-format .bsa.
//
// Returns relative, forward-slash, lowercased paths - matching how the
// conflict inspector normalises loose-file paths. Empty list means
// unsupported version / malformed / read error; skip silently.
QStringList listTes3BsaFiles(const QString &bsaPath);

} // namespace bsa

#endif // BSA_READER_H
