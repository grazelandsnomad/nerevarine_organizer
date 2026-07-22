#pragma once

// log_triage - pure parser for ~/.config/openmw/openmw.log. Classifies the
// runtime errors OpenMW emits and, for plugin failures, names the owning mod
// so the user isn't hunting through a 10k-line log by hand.
//
// Qt Core only - no widgets, no I/O, no MainWindow state; golden-file
// testable. The MainWindow slot stays a thin wrapper that reads the log,
// builds the TriageMod list from m_modList, and renders the result.
//
// Matched error shapes are the ones OpenMW still emits in release builds (see
// include/master_satisfaction.h and the Fatal-error note around
// src/mainwindow.cpp:7400). Anything error-shaped but unrecognised falls into
// OtherError so new OpenMW log shapes aren't silently swallowed.

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

    // "Warning: Saved game dependency X.ESP is missing." - about the SAVE, not
    // the install. OpenMW records the content list at save time, so this fires
    // whenever a mod renames a plugin (.ESP -> .esm is common) or an optional
    // patch is no longer selected. The install can be perfectly healthy and
    // the same content loading fine under its new name.
    //
    // Classified separately because it is benign and unfixable by reinstalling:
    // left in OtherError it reads as a broken install, which is exactly how it
    // was misread in practice.
    SaveGameDependency,
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
