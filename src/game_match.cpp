#include "game_match.h"

namespace game_match {

Result classify(const QString    &urlDomain,
                const QStringList &profileDomains,
                int                currentIdx)
{
    Result r;

    // Nothing to compare against. Silence beats a guess: a download with no
    // game in its URL is not evidence that it belongs somewhere else.
    if (urlDomain.isEmpty()) return r;
    if (currentIdx < 0 || currentIdx >= profileDomains.size()) return r;

    // The active profile serves this game - the overwhelmingly common case,
    // and the one that must cost nothing.
    if (profileDomains[currentIdx].compare(urlDomain, Qt::CaseInsensitive) == 0)
        return r;

    for (int i = 0; i < profileDomains.size(); ++i) {
        if (i == currentIdx) continue;
        // An unconfigured profile (blank domain) matches nothing; skip rather
        // than let it collide with an empty-string comparison.
        if (profileDomains[i].isEmpty()) continue;
        if (profileDomains[i].compare(urlDomain, Qt::CaseInsensitive) == 0)
            r.candidates.append(i);
    }

    r.verdict = r.candidates.isEmpty() ? Verdict::Unknown : Verdict::Elsewhere;
    return r;
}

} // namespace game_match
