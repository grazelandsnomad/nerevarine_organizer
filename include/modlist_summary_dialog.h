#pragma once

// modlist_summary_dialog - the modal behind Mods -> "Modlist summary".
//
// Split out of MainWindow so the big TU stops carrying another dialog builder.
// Everything the dialog renders arrives in a View the caller gathers, and the
// two actions are injected, so nothing in here reaches for MainWindow state.
// Unlike modlist_summary (Qt Core, unit tested) this half is Widgets and stays
// untested - keeping the arithmetic on the other side of the header is the
// point.

#include <QString>

#include <functional>

#include "modlist_summary.h"

class QWidget;

namespace modlist_summary {

// Everything shown. Strings arrive display-ready (already resolved and
// translated) so the dialog performs no lookups of its own.
struct View {
    QString profileName;
    QString platform;
    QString modsDir;
    QString openmwBinary;   // resolved path, or the "not found" string
    QString openmwCfg;
    Stats   stats;
    int     outsideCount = 0;   // mods outside modsDir; 0 hides Consolidate
};

// Modal. `onMoveMods` / `onConsolidate` re-enter the MainWindow slots that own
// those flows; each closes this dialog before running. `onConsolidate` is only
// reachable when view.outsideCount > 0.
void showDialog(QWidget *parent, const View &view,
                const std::function<void()> &onMoveMods,
                const std::function<void()> &onConsolidate);

} // namespace modlist_summary
