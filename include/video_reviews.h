#ifndef VIDEO_REVIEWS_H
#define VIDEO_REVIEWS_H

#include <QString>
#include <QList>
#include <QPair>

// Hardcoded mapping of "mod display name needle" → "YouTube / review URL".
// The needle is matched against `mod.lower().contains(needle)`, so partial
// substrings work (e.g. "dagoth ur fleshed out" matches the full modlist
// entry "Dagoth Ur Fleshed Out (OpenMW)").  Grow this table as the user
// pins more reviews - no UI yet, just add a pair here.
namespace video_reviews {

inline QString urlForHardcoded(const QString &displayName)
{
    static const QList<QPair<QString, QString>> kReviews = {
        { QStringLiteral("dagoth ur fleshed out"),
          QStringLiteral("https://www.youtube.com/watch?v=NvL8vBUswB8") },
    };
    if (displayName.isEmpty()) return {};
    const QString lower = displayName.toLower();
    for (const auto &pair : kReviews)
        if (lower.contains(pair.first)) return pair.second;
    return {};
}

inline QString urlFor(const QString &displayName)
{
    static const QList<QPair<QString, QString>> kReviews = {
        { QStringLiteral("dagoth ur fleshed out"),
          QStringLiteral("https://www.youtube.com/watch?v=NvL8vBUswB8") },
    };
    if (displayName.isEmpty()) return {};
    const QString lower = displayName.toLower();
    for (const auto &pair : kReviews)
        if (lower.contains(pair.first)) return pair.second;
    return {};
}

} // namespace video_reviews

#endif // VIDEO_REVIEWS_H
