#pragma once

#include <QList>
#include <QString>
#include <QStringList>

struct FsProgress;

// Pure FOMOD install work: carry out what the wizard decided (execute), then
// promote its output directory or fall back to the raw extract, and on promote
// optionally rename the directory to a human-readable title. Side effects are
// filesystem-only; unit-testable against a QTemporaryDir.

namespace fomod_install {

// One file or folder the installer puts in place: an <file> or <folder> entry
// of a required list, a picked option, or a satisfied conditional pattern.
struct PlannedCopy {
    QString source;        // relative to the archive root, as the installer spells it
    QString destination;   // relative to the install dir; "" = its root for a
                           // folder, the file's own name for a file
    bool    isFolder = false;
};

// Everything the wizard decided, as data: FomodWizard reads its buttons on the
// UI thread and hands this over, and execute() does the copying on a worker.
// Copying a big installer's picks took minutes, and on the UI thread the
// window read as crashed the whole time.
struct Plan {
    QString            archiveRoot;   // where the installer's sources live
    QString            installDir;    // what execute() fills (fomod_install/)
    QList<PlannedCopy> copies;        // in order: later entries overwrite earlier
};

// Start `plan.installDir` afresh and carry out every copy in order, last
// writer wins, pulling in the lua an .omwscripts manifest declares but the
// installer never lists (fomod_scripts). Returns the sources it could not find
// or copy. With `progress`, counts bytes against the plan's total.
QStringList execute(const Plan &plan, FsProgress *progress = nullptr);

enum class PromoteOutcome {
    Promoted,      // fomodPath was valid; finalModPath is now the install root
    EmptyFallback  // fomodPath was empty; caller should warn + keep raw extract
};

struct PromoteResult {
    PromoteOutcome outcome;
    QString        finalModPath;        // always non-empty
    bool           extractDirRemoved = false;  // true when wrapper tidied up
    QString        leftover;   // leaveExtract: extractDir, set aside to delete
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
// `leaveExtract`       set extractDir aside (safefs::setAside) instead of
//                      deleting it, and name it in `leftover` for the caller
//                      to delete off the critical path.
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
                      const QString &modsDir,
                      bool leaveExtract = false);

} // namespace fomod_install
