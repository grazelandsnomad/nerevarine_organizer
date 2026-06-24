#ifndef LOGGING_H
#define LOGGING_H

#include <QLoggingCategory>
#include <QString>

// Category-tagged logging.
//
// Use the qCDebug / qCInfo / qCWarning / qCCritical macros so the category
// name shows up in every log line and users can scope verbosity per
// subsystem via the QT_LOGGING_RULES environment variable, e.g.
//
//   QT_LOGGING_RULES="nerev.install.debug=true;nerev.scan.debug=false"
//
// All categories default to QtInfo and above; override per-launch with
// the env var or settings.
namespace logging {

Q_DECLARE_LOGGING_CATEGORY(lcApp)        // application lifecycle, settings, language
Q_DECLARE_LOGGING_CATEGORY(lcInstall)    // download/verify/extract pipeline
Q_DECLARE_LOGGING_CATEGORY(lcNexus)      // Nexus API (titles, file lists, deps)
Q_DECLARE_LOGGING_CATEGORY(lcOpenMW)     // openmw.cfg / launcher.cfg sync
Q_DECLARE_LOGGING_CATEGORY(lcLoadOrder)  // plugin load order, conflict scans, masters
Q_DECLARE_LOGGING_CATEGORY(lcScan)       // size + data-folders + missing-master scans
Q_DECLARE_LOGGING_CATEGORY(lcModList)    // modlist load/save/import/export
Q_DECLARE_LOGGING_CATEGORY(lcLaunch)     // OpenMW / Steam / GOG launch flow
Q_DECLARE_LOGGING_CATEGORY(lcFomod)      // FOMOD wizard + path resolution
Q_DECLARE_LOGGING_CATEGORY(lcUi)         // UI plumbing (zoom, filter, banner, columns)

// Path of the active log file. Empty until initialize() runs.
QString currentLogPath();

// Path of the log directory (containing log.txt and rotated .1/.2/...).
QString logDirectory();

// Initialise the file logger + Qt message handler + crash handlers.
// Picks AppDataLocation/logs/ under AppImage, applicationDirPath
// otherwise. Returns the path of the active log file.
QString initialize(const QString &appVersion);

} // namespace logging

#endif // LOGGING_H
