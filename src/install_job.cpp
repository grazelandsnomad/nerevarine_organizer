#include "install_job.h"

#include "bain.h"
#include "fomod_copy.h"
#include "fomod_install.h"
#include "fs_progress.h"
#include "safe_fs.h"

#include <QDir>
#include <QString>
#include <QtGlobal>

namespace install_job {

namespace {

// Set `dir` aside for the caller to delete, or delete it here if it cannot be.
void leave(const QString &dir, Result &r)
{
    const QString tomb = safefs::setAside(dir);
    if (!tomb.isEmpty()) r.leftovers << tomb;
    else QDir(dir).removeRecursively();
}

} // namespace

Result run(const Work &w, FsProgress *progress)
{
    auto phase = [progress](FsProgress::Phase p, qint64 total) {
        if (progress) progress->begin(p, total);
    };

    Result r;
    // What gets installed, and what a Merge leaves behind to delete.
    QString content = w.modPath;
    QString discard = w.extractDir;

    if (w.stage != Work::Stage::None) {
        QString staged = w.modPath;   // FomodRaw: the mod root itself
        if (w.stage == Work::Stage::Bain) {
            staged = bain::stage(w.modPath, w.bainChosen, progress);
            if (staged.isEmpty()) {
                phase(FsProgress::Phase::Finishing, 0);
                leave(w.extractDir, r);
                r.outcome = Result::Outcome::NothingStaged;
                return r;
            }
        } else if (w.stage == Work::Stage::Fomod) {
            fomod_install::execute(w.fomodPlan, progress);
            staged = w.fomodPlan.installDir;
        }

        // Moves the staged result out and sets the rest of the unpack -
        // unpicked packages and options - aside, so they cannot leak in as
        // data.
        phase(FsProgress::Phase::Finishing, 0);
        const auto p = fomod_install::promote(w.extractDir, w.modPath, staged,
                                              w.title, w.modsDir,
                                              /*leaveExtract=*/true);
        if (!p.leftover.isEmpty()) r.leftovers << p.leftover;
        if (p.outcome == fomod_install::PromoteOutcome::EmptyFallback) {
            r.outcome = Result::Outcome::EmptyFallback;
        } else {
            content = p.finalModPath;
        }
        discard = content;
    }
    r.finalPath = content;

    // A Merge whose target went missing (deleted between picking Merge and
    // the download finishing), or already is the content, has nothing to
    // overlay: the content is registered as it is.
    //
    // Either way the job ends by flushing the drive once: files are copied
    // without a sync each (fomod_copy::copyFile), and a Replace deletes the
    // old folder as soon as this one is registered.
    if (w.mergeTarget.isEmpty()
        || QDir::cleanPath(w.mergeTarget) == QDir::cleanPath(content)
        || !QDir(w.mergeTarget).exists()) {
        safefs::flushFileSystemOf(r.finalPath);
        return r;
    }

    QString target = w.mergeTarget;
    if (!w.forkTo.isEmpty()) {
        phase(FsProgress::Phase::Merging, 0);
        if (safefs::copyTreeVerified(w.mergeTarget, w.forkTo)) {
            target   = w.forkTo;
            r.forked = true;
        } else {
            // A merge in place beats a lost optional.
            r.forkFailed = true;
        }
    }

    // Last writer wins and folder names fold case, so the new files override
    // the existing mod's - MO2's "merge".
    phase(FsProgress::Phase::Merging, fomod_copy::sizeOf(content));
    fomod_copy::copyContents(content, target, progress);

    // What was merged is redundant now. Never the target itself.
    if (!discard.isEmpty()
        && QDir::cleanPath(discard) != QDir::cleanPath(target)
        && QDir(discard).exists()) {
        phase(FsProgress::Phase::Finishing, 0);
        leave(discard, r);
    }
    r.merged    = true;
    r.finalPath = target;
    safefs::flushFileSystemOf(r.finalPath);
    return r;
}

} // namespace install_job
