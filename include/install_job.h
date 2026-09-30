#pragma once

// install_job - the file work between "Install" and the mod being in the list:
// stage what the installer picked, move the result into place, and overlay it
// on an existing mod when a Merge asked for that.
//
// All of it ran on the UI thread. Installing Tamriel Data (HD) - a 2.75 GB
// BAIN archive, about 54,000 files - copied every picked file one at a time,
// then deleted the unpacked archive, and the window said "Not responding" for
// many minutes; it was closed twice, taken for broken. run() is the same work
// done on a worker, reporting as it goes. MainWindow keeps the part that needs
// the list: finding the row again, registering the folder, the messages.
//
// Qt Core only; unit-tested against QTemporaryDir fixtures.

#include "fomod_install.h"

#include <QString>
#include <QStringList>

struct FsProgress;

namespace install_job {

struct Work {
    enum class Stage {
        None,       // install the unpacked archive as it is
        Bain,       // stage the chosen BAIN packages (bain::stage), then promote
        Fomod,      // carry out the FOMOD wizard's plan, then promote
        FomodRaw,   // the FOMOD wizard said to install the archive as it is:
                    // promote the mod root itself, out of its wrapper
    };
    Stage   stage = Stage::None;
    QString extractDir;   // where the archive was unpacked
    QString modPath;      // the mod root inside it
    QString modsDir;
    QString title;        // sanitized Nexus title; "" keeps the unpacked name

    QStringList         bainChosen;   // Stage::Bain
    fomod_install::Plan fomodPlan;    // Stage::Fomod

    // A Merge: overlay the result onto this existing mod folder. When another
    // profile shares that folder, it is first copied to forkTo and the copy is
    // merged into instead, leaving the other profile's mod untouched.
    QString mergeTarget;
    QString forkTo;
};

struct Result {
    enum class Outcome {
        Installed,       // finalPath holds the mod
        NothingStaged,   // the picked packages held no files: nothing to
                         // install; the unpack is among the leftovers
        EmptyFallback,   // the installer produced nothing: the raw unpack
                         // is what finalPath holds
    };
    Outcome outcome = Outcome::Installed;
    QString finalPath;

    bool merged     = false;   // finalPath is the merge target (or its fork)
    bool forked     = false;   // ...the fork
    bool forkFailed = false;   // the fork could not be made; merged in place

    // Folders the job is done with - the rest of the unpack, a copy already
    // merged - set aside (safefs::setAside) rather than deleted: an unpack of
    // 54,000 files took half a minute to delete on an NTFS drive, and the mod
    // need not wait for that. The caller deletes them in the background.
    QStringList leftovers;
};

Result run(const Work &work, FsProgress *progress = nullptr);

} // namespace install_job
