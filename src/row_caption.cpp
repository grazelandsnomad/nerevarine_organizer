#include "row_caption.h"

#include <QCoreApplication>

namespace row_caption {
namespace {

// ModListDelegate::tr, as the texts were written there.
QString tr(const char *text)
{
    return QCoreApplication::translate("ModListDelegate", text);
}

// "overwrites A +2": the first mod named, the rest counted.
QString part(const QString &verb, const QStringList &entries)
{
    if (entries.isEmpty()) return {};
    QString s = verb + QLatin1Char(' ') + entries.first().section('\t', 0, 0);
    if (entries.size() > 1)
        s += QStringLiteral(" +%1").arg(entries.size() - 1);
    return s;
}

} // namespace

Caption choose(const Inputs &in)
{
    Caption c;
    if (in.conflictNotices) {
        c.win  = part(tr("overwrites"),     in.overwrites);
        c.lose = part(tr("overwritten by"), in.overwrittenBy);
        if (!c.lose.isEmpty()) c.loseKind = LoseKind::OverwrittenBy;

        // A record clash: the mods share no file at all, so there is no
        // direction to draw and this list's order does not settle it. Shown
        // only when no file conflict competes for the space - the usual case,
        // as a translation ships its own filename.
        if (c.win.isEmpty() && c.lose.isEmpty() && !in.sameRecords.isEmpty()) {
            const QStringList f = in.sameRecords.first().split('\t');
            c.lose = tr("same %1 records as %2").arg(f.value(1), f.value(0));
            if (in.sameRecords.size() > 1)
                c.lose += QStringLiteral(" +%1").arg(in.sameRecords.size() - 1);
            c.loseKind = LoseKind::SameRecords;
        }
    }
    if (!in.translationNotices) return c;

    // Work the user has already started outranks either verdict. "No
    // translation" is true and unhelpful once they are four hundred strings
    // into translating it themselves; what they want is where they got to.
    if (in.translationInProgress > 0) {
        c.lose     = tr("Translation in progress…");
        c.loseKind = LoseKind::InProgress;
        return c;
    }
    // Coverage takes the half outright when it has something to say: a mod
    // flagged here is nearly always in a conflict with its own translation,
    // and "no translation" is the actionable way to say it.
    if (in.translationState == 1) {
        c.lose     = tr("no translation");
        c.loseKind = LoseKind::NoTranslation;
        return c;
    }
    if (in.translationState != 0) {
        // "N of M" rather than a bare count: 40 untranslated strings means
        // something different in a 60-string mod than in a 6000-string one.
        const QStringList head = in.translationDetail.value(0).split('\t');
        c.lose     = tr("%1 of %2 strings untranslated").arg(head.value(2), head.value(3));
        c.loseKind = LoseKind::PartlyTranslated;
        return c;
    }
    // Good news never takes the slot from a warning: a translation nearly
    // always overwrites its source's files, and "overwritten by" is the half
    // to act on.
    if (in.isTranslationOfOther && !in.translationPartner.isEmpty() && c.lose.isEmpty()) {
        c.lose     = tr("translation of %1").arg(in.translationPartner);
        c.loseKind = LoseKind::TranslationOf;
    }
    return c;
}

} // namespace row_caption
