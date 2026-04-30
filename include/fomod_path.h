#ifndef FOMOD_PATH_H
#define FOMOD_PATH_H

// FOMOD path resolution helper.
//
// ModuleConfig.xml files are almost universally authored on Windows, which
// means the paths inside routinely:
//   · use backslash separators  ("00 Core\Meshes\foo.nif")
//   · mis-case folder names     ("00 Core\Meshes" vs actual "00 Core/meshes")
//
// On Windows both failure modes are silently tolerated by the FS layer.  On
// Linux QFile::copy / QDir::mkpath fail strictly, return false, and the
// FOMOD wizard ends up producing an empty install folder - which is exactly
// how OAAB_Data.esm vanished from the launcher.
//
// This helper is the single chokepoint every FOMOD file/folder reference
// passes through before it hits the filesystem.  It:
//   1. Normalises '\' → '/'.
//   2. Walks each path segment, accepting an exact match first, then
//      falling back to a case-insensitive scan of that directory.
//
// Returns an absolute filesystem path on success, or an empty QString if any
// segment is missing entirely (the caller should report / count this, not
// swallow it).

#include <QString>

namespace fomod {

QString resolvePath(const QString &root, const QString &relative);

} // namespace fomod

#endif // FOMOD_PATH_H
