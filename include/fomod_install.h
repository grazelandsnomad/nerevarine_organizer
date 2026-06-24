#pragma once

#include <QString>

// Pure post-FOMOD bookkeeping: promote a FomodWizard output directory or
// fall back to the raw extract, and on promote optionally rename the
// directory to a human-readable title. Side effects are filesystem-only;
// unit-testable against a QTemporaryDir.

namespace fomod_install {

enum class PromoteOutcome {
    Promoted,      // fomodPath was valid; finalModPath is now the install root
    EmptyFallback  // fomodPath was empty; caller should warn + keep raw extract
};

struct PromoteResult {
    PromoteOutcome outcome;
    QString        finalModPath;        // always non-empty
    bool           extractDirRemoved = false;  // true when wrapper tidied up
};

// `extractDir`         where the archive was extracted. Removed on promote
//                      so radio-exclusive FOMOD variants the user didn't
//                      pick stop leaking as data= paths.
// `currentModPath`     modPath before the wizard ran. Returned only on
//                      EmptyFallback.
// `fomodPath`          path the wizard reported on apply.
// `titleHintSanitized` filename-safe Nexus title, or empty (then the
//                      install inherits extractDir's basename). Caller
//                      sanitises; this helper does not pull in
//                      fsutils::sanitizeFolderName.
// `modsDir`            parent directory; numeric suffix appended on
//                      collision.
//
// EmptyFallback: fomodPath missing or empty (and removed if it existed).
// Promoted:      finalModPath is the relocated directory.
//                extractDirRemoved is false only when the initial move
//                out of extractDir failed; finalModPath still points at
//                fomodPath and the caller must clean up.
PromoteResult promote(const QString &extractDir,
                      const QString &currentModPath,
                      const QString &fomodPath,
                      const QString &titleHintSanitized,
                      const QString &modsDir);

} // namespace fomod_install
