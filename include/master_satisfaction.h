#pragma once

// master_satisfaction - pure helper that walks a list of plugins, reads their
// TES3 MAST headers, and reports which ones have unsatisfied parent files.
//
// Lives outside openmwconfigwriter.h on purpose: the writer is a pure string
// renderer, while this helper does filesystem I/O (readTes3Masters).  Keeping
// them separate preserves the writer's golden-file testability.
//
// Motivating regression: OpenMW aborts at launch with "File X asks for parent
// file Y, but it is not available or has been loaded in the wrong order" if a
// patch ESP is enabled without its parent ESM.  Common trigger is a FOMOD that
// bundles optional cross-mod patches (e.g. Hlaalu Seyda Neen bundling
// HlaaluSeydaNeen_AFFresh_Patch.ESP which needs AFFresh.esm).

#include <QList>
#include <QSet>
#include <QString>

namespace openmw {

struct PluginRef {
    QString filename;   // basename as it appears in openmw.cfg (case preserved)
    QString fullPath;   // absolute path on disk, fed to readTes3Masters
};

// For every plugin in `plugins`, reads its TES3 MAST entries.  Returns the
// set of plugin filenames (case-preserved) whose masters aren't satisfied.
//
// "Satisfied" = the master's lowercased name is in `availableLower`.
// Pre-populate `availableLower` with:
//   * lowercased filenames of every candidate plugin (so peers can depend
//     on each other),
//   * "morrowind.esm" / "tribunal.esm" / "bloodmoon.esm" (always available),
//   * any external base-game plugins the caller knows about.
//
// Transitive closure: if plugin A depends on plugin B and B is suppressed
// in the first pass, A is also returned - the function loops until the
// unsatisfied set stops growing.
QSet<QString> findUnsatisfiedMasters(
    const QList<PluginRef> &plugins,
    QSet<QString>           availableLower);

} // namespace openmw
