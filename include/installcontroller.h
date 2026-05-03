#pragma once

#include <QObject>
#include <QString>
#include <QUuid>

// InstallController - local-filesystem side of the "finish installing a
// downloaded archive" pipeline.  No network, no Qt Widgets dependencies.
//
// Step 2 of the mainwindow.cpp god-object breakup.  Currently owns the
// archive-verification stage (size + MD5 check) and the QProcess-based
// extraction; future commits can pull in the FOMOD wizard kick-off too.
//
// Widget-facing side:
//   · MainWindow owns the QListWidget row (the "placeholder") and drives the
//     UI-policy decisions: displaying dialogs, resetting the row after a
//     failure, calling into addModFromPath on success, etc.
//   · The controller signals back via queued connections with a
//     per-install QUuid token (ModRole::InstallToken) -- not the widget
//     pointer.  MainWindow looks the placeholder back up by token, which
//     covers the previously-implicit case of "extraction completes while
//     the user is on a different profile" (the placeholder lives in
//     m_strandedInstalls, not in the active m_modList).
//
// The token lifecycle:
//   · MainWindow::prepareItemForInstall() generates a new QUuid via
//     QUuid::createUuid() and stores it in ModRole::InstallToken on the
//     placeholder row.
//   · The same QUuid is passed to verifyArchive / extractArchive.
//   · Every signal carries it back, so slots can findPlaceholderByToken()
//     without dereferencing a QListWidgetItem* across queued connections.
//   · Persisted in the modlist file so a relaunch can match the pending
//     row back up if the InstallController's pending signals never landed
//     (process crash, hard kill, etc.).

class InstallController : public QObject
{
    Q_OBJECT
public:
    enum class VerifyFailKind  { Size, Md5 };
    Q_ENUM(VerifyFailKind)
    enum class ExtractFailKind { NonzeroExit, ProgramMissing };
    Q_ENUM(ExtractFailKind)

    explicit InstallController(QObject *parent = nullptr);

    // Size/MD5-check `archivePath` against `expectedMd5Lower` (hex,
    // lowercased, empty = skip MD5) and `expectedSize` (bytes, <=0 = skip
    // size check).  When both are skipped, verified() fires immediately.
    //
    // Size check runs on the calling thread (cheap, no I/O past stat).
    // MD5 runs on a QtConcurrent worker; the success/failure signal for
    // that path is emitted via a QueuedConnection back to this controller,
    // so the slot connected to it always runs on the GUI thread.
    //
    // `installToken` is the placeholder's stable identity -- the controller
    // never inspects ModRole values and never touches a widget; the token
    // travels through unchanged so slots can look the row up.
    void verifyArchive(const QString &archivePath, const QUuid &installToken,
                       const QString &expectedMd5Lower, qint64 expectedSize);

    // Spawn the archive extractor appropriate for `archivePath`'s extension
    // (unzip / unrar / 7z; .fomod and unknown extensions fall through to 7z
    // which sniffs the real container).  Destination is a directory under
    // `modsDir` named after the archive's basename.
    //
    // `reuseHintPath` (optional) - absolute path of an existing install for
    // this placeholder, usually the current `ModPath` role.  When it lives
    // under `modsDir` in a top-level folder named like this archive
    // (exact basename, or "<baseName>_<digits>" from the legacy timestamp
    // fallback), we wipe that folder and extract into it instead of
    // creating a new "<baseName>_<now>" copy.  Keeps modlist paths stable
    // across machines (synced modlist still matches the folder name on
    // every machine) and stops per-reinstall clutter from piling up.
    //
    // Outcomes (exactly one signal per call):
    //   · extractionSucceeded - with extractDir (what we created) and
    //     modPath (dive-into-single-subdir result, used by the caller to
    //     decide FOMOD + register-as-mod).
    //   · extractionFailed - QProcess ran but returned nonzero OR the tool
    //     was missing (couldn't start within 3 s).  Kind distinguishes the
    //     two; slot still gets extractDir so it can clean up.
    //
    // The token is opaque to the controller -- it only echoes it back.
    void extractArchive(const QString &archivePath, const QString &modsDir,
                        const QUuid &installToken,
                        const QString &reuseHintPath = {});

signals:
    // Fires at the top of an MD5 run (size-only runs skip it) so the UI can
    // show a "verifying…" status.  Archive path echoes back for message
    // formatting.
    void verificationStarted(const QString &archivePath);

    // Archive has been verified (or had nothing to verify).  MainWindow
    // normally hands the pair to extractAndAdd from here.
    void verified(const QString &archivePath, const QUuid &installToken);

    // Size or MD5 mismatch.  `actual` and `expected` are formatted for the
    // caller's i18n strings:
    //   · Size: decimal byte counts as strings ("12345678" / "12345679")
    //   · Md5:  lowercase hex (or "(read error)" if the file couldn't be
    //           opened for hashing).
    void verificationFailed(const QString &archivePath,
                            const QUuid &installToken,
                            VerifyFailKind kind,
                            const QString &actual,
                            const QString &expected);

    void extractionSucceeded(const QString &archivePath,
                             const QString &extractDir,
                             const QString &modPath,
                             const QUuid &installToken);
    // `detail` is the exit-code-as-string for NonzeroExit, or the program
    // name ("7z" / "unzip" / "unrar") for ProgramMissing.  Slot formats
    // the i18n body based on kind.
    void extractionFailed(const QString &archivePath,
                          const QString &extractDir,
                          const QUuid &installToken,
                          ExtractFailKind kind,
                          const QString &detail);
};
