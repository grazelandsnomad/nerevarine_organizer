#pragma once

// log_triage_attrib - the one part of the OpenMW log triage that needs the
// disk: which installed mod mentions a record id.
//
// log_triage.cpp is a pure parser and stays that way. But a line like
//   Cell reference "bs_In_DAE_roomMB_L_cor_01" is not found!
// carries a record id, not a filename, so the triage's filename-to-mod index
// can never name a suspect for it. The plugin that PLACES the object does
// contain the id as plain bytes, so searching the installed plugins for it
// names the mod to go and look at - which is the whole value of the message.
//
// Reads files, so it belongs on a worker: see MainWindow::onTriageOpenMWLog.

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

namespace openmw {

// One mod and the plugin files it ships, as absolute paths.
struct AttribSource {
    QString     modName;
    QStringList pluginPaths;
};

// For each id, the first mod whose plugins contain it, keyed by the LOWERED
// id. Ids not found anywhere are absent.
//
// Scans each file once in chunks, so a 100 MB master costs its size in reads
// and not in RAM, and stops early once every id has an owner. `maxBytes` caps
// the total read so a pathological library cannot park a worker forever;
// whatever was resolved before the cap is returned.
QHash<QString, QString> modsMentioningIds(const QList<AttribSource> &sources,
                                          const QStringList         &ids,
                                          qint64 maxBytes = 8LL << 30);

} // namespace openmw
