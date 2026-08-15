#ifndef GAME_MATCH_H
#define GAME_MATCH_H

// Does an incoming download belong to the profile it is about to land in?
//
// An nxm:// link names its game in the URL ("nxm://starfield/mods/..."), but
// the install path never looked at it: the placeholder went straight into
// whatever list happened to be on screen. Click a Starfield download while a
// Skyrim profile is active and the Starfield mod installs into Skyrim - no
// warning, wrong mods dir, wrong load order, and nothing on screen says so.
//
// The manager already knows better. Every GameProfile::id is a Nexus game
// slug (via nexusDomainFor(), which resolves the one id that is not - Skyrim
// AE asks Nexus for skyrimspecialedition), so the URL's game can simply be
// looked up against the configured profiles. This module is that lookup, kept
// free of MainWindow so it can be tested: callers resolve the domains, this
// decides what the situation is, and the UI layer decides what to say.
//
// Deliberately silent when it cannot tell. An empty URL domain, or an empty
// profile domain, is not evidence of a mismatch - the same reasoning as
// fomod::asksAboutAnotherMod, where a failed lookup is not proof of absence.

#include <QList>
#include <QString>
#include <QStringList>

namespace game_match {

enum class Verdict {
    // The active profile already serves this game. Proceed, say nothing.
    Match,
    // Another configured profile serves it. Offer to switch there.
    Elsewhere,
    // No configured profile serves it at all. Warn; there is nowhere to go.
    Unknown,
};

struct Result {
    Verdict   verdict = Verdict::Match;
    // Indices into profileDomains, for Elsewhere. More than one is possible
    // and normal: Skyrim SE and Skyrim AE are separate profiles here because
    // they are separate runtimes, but Nexus files them under one domain, so a
    // skyrimspecialedition link legitimately fits either.
    QList<int> candidates;
};

// urlDomain      - the game slug carried by the nxm:// URL (NxmTarget::game),
//                  or by a stored Nexus mod-page URL (NexusModRef::game).
// profileDomains - nexusDomainFor(id) for every configured game profile, in
//                  GameProfileRegistry::games() order, so the returned
//                  candidate indices are registry indices.
// currentIdx     - index of the active profile within profileDomains.
//
// Comparison is case-insensitive. Returns Match (the quiet answer) whenever
// the inputs cannot support a verdict: an empty urlDomain, or a currentIdx
// out of range.
Result classify(const QString    &urlDomain,
                const QStringList &profileDomains,
                int                currentIdx);

} // namespace game_match

#endif // GAME_MATCH_H
