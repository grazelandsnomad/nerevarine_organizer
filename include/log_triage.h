#pragma once

// log_triage - pure parser for ~/.config/openmw/openmw.log.  Classifies the
// runtime errors OpenMW emits and, for plugin-level failures, names the mod
// in the modlist that owns the offending .esp / .esm so the user gets a
// "Hlaalu Seyda Neen's HlaaluSeydaNeen_AFFresh_Patch.ESP is the culprit"
// answer instead of hunting through a 10k-line log by hand.
//
// No Qt widgets, no filesystem I/O, no MainWindow state - same pattern as
// openmwconfigwriter.h.  Keeps the function golden-file testable and lets
// the MainWindow slot stay a thin wrapper that just reads openmw.log,
// builds the TriageMod list from its m_modList, and renders the result
// into a dialog.
//
// Error shapes matched here are the ones OpenMW still emits in release
// builds (cross-checked against comments already in this codebase - see
// include/master_satisfaction.h and the "Fatal error: Failed loading X.esp"
// comment at src/mainwindow.cpp:7400).  Lines that look like errors but
// don't fit any known shape fall into OtherError so the user still sees
// them in the report and the triage doesn't silently swallow new log
// shapes added by future OpenMW versions.

#include <QList>
#include <QString>
#include <QStringList>

namespace openmw {

// Per-mod info the triage needs to name a suspect when a log line mentions
// a plugin filename.  Populated from m_modList in the caller.
struct TriageMod {
    QString     displayName;   // what we show to the user
    QStringList plugins;       // plugin basenames (case preserved)
};

enum class LogIssueKind {
    // "File X.esp asks for parent file Y.esm, but it is not available or has
    // been loaded in the wrong order" - same pattern master_satisfaction.h
    // guards against at write time, but this catches what slipped through.
    MissingMaster,

    // "Fatal error: Failed loading X.esp: the content file does not exist".
    // Usually means the orphan-scrub in syncOpenMWConfig missed an entry OR
    // the user ticked a plugin in the launcher that has no providing mod.
    MissingPlugin,

    // Asset-level failures: missing texture / mesh / sound / icon.  OpenMW
    // prints several phrasings ("Can't find texture X", "Error loading
    // X.nif", "Failed to load X") - all fold into this bucket.  The target
    // is the asset path/name; the suspect is only set if the asset happens
    // to be a plugin-shaped filename we recognise.
    MissingAsset,

    // Error/Fatal line that didn't fit any known shape.  Kept so the user
    // sees the raw message instead of having it disappear.
    OtherError,
};

struct LogIssue {
    LogIssueKind kind;
    QString      target;      // plugin filename, asset path, or raw excerpt
    QString      parent;      // MissingMaster: the .esm that's missing; else {}
    QString      suspectMod;  // display name of the owning mod, or empty
    QString      detail;      // full log line, for the "show me why" column
};

struct LogTriageReport {
    QList<LogIssue> issues;
    int errorLines = 0;       // every "[... E]" / "Fatal error:" line scanned
};

// Parse `logText` (full contents of openmw.log) and return a structured
// report.  `mods` lets the triage resolve plugin filenames to the modlist
// display name; pass an empty list if cross-referencing isn't needed.
//
// Matching is case-insensitive for plugin filenames (OpenMW mixes .ESP /
// .esp / .Esp freely in its output).  Duplicate errors for the same
// target collapse into one issue with the first-seen detail line - a log
// from a crashed run can otherwise repeat the same missing master 80+
// times as every subsequent load touches it.
LogTriageReport triageOpenMWLog(const QString         &logText,
                                const QList<TriageMod> &mods);

} // namespace openmw
