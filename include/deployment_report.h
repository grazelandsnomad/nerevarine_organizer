#pragma once

// deployment_report - formats the read-only Bethesda "Inspect deployment"
// text from already-resolved facts. Qt Core only (no Widgets, no filesystem,
// no MainWindow), so it's unit-testable against a plain struct.
//
// The gathering stays in MainWindow: every path here is resolved by the
// file-static bethesda* helpers that the real deploy/undeploy path also uses
// (bethesdaResolveDataDir, resolveBethesdaPluginsTxt, ...), so they can't move
// without dragging that path along. This module owns only the formatting - the
// part that was worth making testable and worth not re-deriving by eye.

#include <QString>
#include <QStringList>

namespace deployment_report {

// One resolvable path plus whether it currently exists. An empty `path` means
// "could not resolve", rendered distinctly from "resolved but missing".
struct PathState {
    QString path;
    bool    exists = false;
};

struct Facts {
    QString     gameName;
    QString     gameId;
    QString     loadOrderStyle;      // human-readable, resolved by the caller
    QString     steamAppId;          // empty -> "(none)"

    PathState   dataFolder;
    QString     scriptExtender;      // loader filename; empty -> not installed
    bool        installDirKnown = false;   // gates the script-extender line

    PathState   pluginsTxt;
    bool        showOblivionIni = false;
    PathState   oblivionIni;

    QString     manifestPath;
    bool        haveManifest = false;
    int         deployedFileCount = 0;
    QString     backupDir;

    int         enabledInstalledMods = 0;
    int         dataRootCount = 0;

    QStringList prefixCandidates;    // Proton prefixes probed
    QStringList prefixExists;        // parallel to prefixCandidates; "  [found]"/"  [MISSING]"
};

// Build the copyable monospace report shown in the dialog.
[[nodiscard]] QString format(const Facts &f);

} // namespace deployment_report
