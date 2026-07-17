#pragma once

// log_triage_dialog - the modal that renders openmw::triageOpenMWLog's output.
//
// Split out of MainWindow so the big TU keeps only the part that needs its
// state (reading the log and building the TriageMod index from the mod list).
// The parsing itself is log_triage (Qt Core, no widgets).
//
// FOLLOW-UP: the grouping/ordering in here (bucket by suspect mod, then
// most-actionable-kind first) is pure and belongs in log_triage, which has no
// tests yet. It stays here for now so this change is a straight move out of
// MainWindow rather than a move plus a redesign.

#include <QString>

#include "log_triage.h"

class QWidget;

namespace openmw {

// Modal. `logPath` is shown in the summary header so the user knows which log
// was read.
void showTriageDialog(QWidget *parent, const LogTriageReport &report,
                      const QString &logPath);

} // namespace openmw
