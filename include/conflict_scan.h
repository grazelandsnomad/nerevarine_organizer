#ifndef CONFLICT_SCAN_H
#define CONFLICT_SCAN_H

#include <QList>
#include <QMap>
#include <QString>

#include <atomic>
#include <memory>

struct ConflictScanInput {
    QString modLabel;
    QString modPath;
};

struct ConflictProvider {
    QString mod;
    QString root;
    QString sourceBsa; // empty for loose files; filename (e.g. "Morrowind.bsa") otherwise
};

using ConflictMap = QMap<QString, QList<ConflictProvider>>;

// Worker-thread entry point. Touches only POD types + filesystem - no Qt
// widgets, no MainWindow state. Callers pass a shared cancel flag; the scan
// checks it between mods, between data roots, and inside the per-directory
// iterator so kill-latency stays low even on huge mods.
ConflictMap scanConflicts(QList<ConflictScanInput> mods,
                          std::shared_ptr<std::atomic<bool>> cancel);

#endif // CONFLICT_SCAN_H
