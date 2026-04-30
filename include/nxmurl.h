#pragma once

#include <QString>

#include <expected>

// Pure parser for Nexus Mods' nxm:// and nxms:// protocol URLs.
// Both schemes must be accepted: nxms:// is the SSL/CDN variant used for
// premium and some standard downloads, and dropping it causes KDE/KIO to
// throw "Unknown protocol: nxms" on click.

struct NxmTarget {
    QString game;       // lowercased game slug ("morrowind", etc.)
    int     modId   = 0;
    int     fileId  = 0;
    QString key;        // signed download key (empty for non-premium)
    QString expires;    // signed download expiry (empty for non-premium)
};

// On success: the parsed target.
// On failure: a short machine-readable reason:
//   "invalid-scheme" - not nxm:// or nxms://
//   "invalid-path"   - path is not /mods/{id}/files/{id}
//   "invalid-ids"    - mod or file id is not a number
std::expected<NxmTarget, QString> parseNxmUrl(const QString &rawUrl);
