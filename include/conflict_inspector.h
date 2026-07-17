#pragma once

// conflict_inspector - the "Inspect conflicts" modal: runs the asset-conflict
// scan off-thread behind a cancellable progress dialog, then renders the
// per-file provider tree with the winning mod marked.
//
// Split out of MainWindow so the big TU stops carrying it. The caller builds
// the POD snapshot - QListWidgetItem isn't thread-safe, so that walk has to
// happen on the UI thread - and hands it in; nothing here touches MainWindow
// state. The scan itself is conflict_scan (worker-safe, no widgets).

#include <QList>

#include "conflict_scan.h"

class QWidget;

namespace conflict_inspector {

// Modal. Scans `snapshot` off-thread with a cancel button; nothing is shown if
// the user cancels.
void show(QWidget *parent, const QList<ConflictScanInput> &snapshot);

} // namespace conflict_inspector
