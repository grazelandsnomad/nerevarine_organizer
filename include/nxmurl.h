#pragma once

#include <QString>

#include <expected>
#include <optional>

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

// -- Nexus *web* (mod-page) URLs -------------------------------------------
//
// Distinct shape from the nxm:// protocol URL above: the game is a path
// segment, not the host, and there's no /files/ tail.  This is the form
// stored in ModRole::NexusUrl and shown in the browser:
//   https://www.nexusmods.com/{game}/mods/{modId}
// Parsing it was open-coded (split('/'), check parts[1]=="mods",
// parts[2].toInt) in ~a dozen places; this is the single source of truth.

struct NexusModRef {
    QString game;        // lowercased game slug ("morrowind", etc.)
    int     modId = 0;
};

// Parse a Nexus mod-page web URL into {game, modId}.  Accepts the canonical
// stored form ".../{game}/mods/{modId}" with any trailing segments, query or
// fragment (?tab=files, /files, #section).  Returns nullopt when the path
// isn't a mod-page shape or the id isn't numeric.  game is lowercased to match
// NxmTarget and the stored slug.
std::optional<NexusModRef> parseNexusModUrl(const QString &rawUrl);

// Build the canonical mod-page URL for (game, modId) - the inverse of
// parseNexusModUrl for the stored form, and the single home for the
// "https://www.nexusmods.com/%1/mods/%2" string template.
QString nexusModUrl(const QString &game, int modId);
