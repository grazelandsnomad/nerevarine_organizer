#pragma once

#include <QString>

#include <expected>
#include <optional>

// Parser for Nexus' nxm:// and nxms:// protocol URLs. nxms:// is the SSL/CDN
// variant (premium + some standard downloads); drop it and KDE/KIO throws
// "Unknown protocol: nxms" on click.

struct NxmTarget {
    QString game;       // lowercased game slug ("morrowind", etc.)
    int     modId   = 0;
    int     fileId  = 0;
    QString key;        // signed download key (empty for non-premium)
    QString expires;    // signed download expiry (empty for non-premium)
};

// On failure, returns a short reason: "invalid-scheme" (not nxm/nxms),
// "invalid-path" (not /mods/{id}/files/{id}), "invalid-ids" (non-numeric id).
std::expected<NxmTarget, QString> parseNxmUrl(const QString &rawUrl);

// -- Nexus *web* (mod-page) URLs -------------------------------------------
//
// Different shape from the nxm:// URL above: game is a path segment, not the
// host, and there's no /files/ tail. This is the form stored in
// ModRole::NexusUrl: https://www.nexusmods.com/{game}/mods/{modId}
// Parsing was duplicated in ~a dozen places; this is the one home for it.

struct NexusModRef {
    QString game;        // lowercased game slug ("morrowind", etc.)
    int     modId = 0;
};

// Parse a Nexus mod-page URL into {game, modId}. Accepts ".../{game}/mods/{id}"
// with any trailing segments, query or fragment (?tab=files, /files, #section).
// nullopt when not a mod-page shape or the id isn't numeric. game is lowercased.
std::optional<NexusModRef> parseNexusModUrl(const QString &rawUrl);

// Inverse of parseNexusModUrl, and the one home for the
// "https://www.nexusmods.com/%1/mods/%2" template.
QString nexusModUrl(const QString &game, int modId);
