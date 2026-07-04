#pragma once

#include <QObject>
#include <QString>
#include <QUuid>

// InstallController - filesystem side of "finish installing a downloaded
// archive". No network, no Qt Widgets.
//
// Owns archive verification (size + MD5) and QProcess extraction.
//
// MainWindow owns the QListWidget row (the "placeholder") and all UI policy:
// dialogs, resetting the row on failure, calling addModFromPath on success.
// The controller signals back via queued connections carrying a per-install
// QUuid token (ModRole::InstallToken), not the widget pointer. MainWindow
// re-looks the placeholder up by token, which also covers extraction
// completing while the user is on a different profile (the placeholder then
// lives in m_strandedInstalls, not the active m_modList).
//
// Token lifecycle:
//   · prepareItemForInstall() makes a QUuid and stores it in
//     ModRole::InstallToken on the placeholder.
//   · The same QUuid is passed to verifyArchive / extractArchive.
//   · Every signal carries it back, so slots use findPlaceholderByToken()
//     instead of a QListWidgetItem* across queued connections.
//   · Persisted in the modlist so a relaunch re-matches the pending row if
//     the controller's signals never landed (crash, hard kill).

class InstallController : public QObject
{
    Q_OBJECT
public:
    enum class VerifyFailKind  { Size, Md5 };
    Q_ENUM(VerifyFailKind)
    enum class ExtractFailKind { NonzeroExit, ProgramMissing };
    Q_ENUM(ExtractFailKind)

    explicit InstallController(QObject *parent = nullptr);

    // Check archivePath against expectedMd5Lower (hex lowercase, empty = skip
    // MD5) and expectedSize (bytes, <=0 = skip size). Both skipped -> verified()
    // fires immediately.
    //
    // Size check runs on the calling thread (just a stat). MD5 runs on a
    // QtConcurrent worker; its result signal comes back via QueuedConnection so
    // the connected slot runs on the GUI thread.
    //
    // installToken is the placeholder's stable id - echoed back unchanged; the
    // controller never reads ModRole or touches a widget.
    void verifyArchive(const QString &archivePath, const QUuid &installToken,
                       const QString &expectedMd5Lower, qint64 expectedSize);

    // Spawn the extractor chosen by the archive's sniffed magic bytes, NOT its
    // file extension (a bare extensionless CDN download still routes correctly):
    // zip -> unzip, rar -> unrar then 7z, everything else -> 7z. Dest is a dir
    // under modsDir named after the archive basename.
    //
    // reuseHintPath (optional) - an existing install for this placeholder,
    // usually the current ModPath. When it lives under modsDir in a top-level
    // folder named like this archive (exact basename, or "<base>_<digits>" from
    // the legacy timestamp fallback), we wipe and extract into it instead of a
    // new "<base>_<now>" copy. Keeps modlist paths stable across machines and
    // stops per-reinstall clutter.
    //
    // Exactly one signal per call:
    //   · extractionSucceeded - extractDir (what we made) and modPath
    //     (dive-into-single-subdir result, drives FOMOD + register-as-mod).
    //   · extractionFailed - QProcess returned nonzero OR the tool was missing
    //     (no start within 3s). Kind splits the two; slot still gets extractDir
    //     to clean up.
    //
    // The token is opaque - just echoed back.
    void extractArchive(const QString &archivePath, const QString &modsDir,
                        const QUuid &installToken,
                        const QString &reuseHintPath = {},
                        const QString &looseNameHint = {});

signals:
    // Fires at the top of an MD5 run (size-only runs skip it) for a
    // "verifying..." status. Archive path echoes back for the message.
    void verificationStarted(const QString &archivePath);

    // Archive verified (or had nothing to check). MainWindow usually hands the
    // pair to extractAndAdd from here.
    void verified(const QString &archivePath, const QUuid &installToken);

    // Size or MD5 mismatch. actual/expected are pre-formatted for i18n:
    //   · Size: decimal byte counts as strings ("12345678" / "12345679")
    //   · Md5:  lowercase hex (or "(read error)" if the file wouldn't open).
    void verificationFailed(const QString &archivePath,
                            const QUuid &installToken,
                            VerifyFailKind kind,
                            const QString &actual,
                            const QString &expected);

    void extractionSucceeded(const QString &archivePath,
                             const QString &extractDir,
                             const QString &modPath,
                             const QUuid &installToken);
    // detail: exit-code string for NonzeroExit, or program name ("7z"/"unzip"/
    // "unrar") for ProgramMissing. Slot picks the i18n body by kind.
    void extractionFailed(const QString &archivePath,
                          const QString &extractDir,
                          const QUuid &installToken,
                          ExtractFailKind kind,
                          const QString &detail);
};
