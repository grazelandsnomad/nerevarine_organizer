#pragma once

// fomod_choices - what was picked in a FOMOD wizard, as the string the modlist
// keeps (its "fomod" key), and back.
//
// Stored choices used to be bare positions: "step:group:option;...". Replayed
// onto an UPDATED installer they meant whatever sits at those positions now.
// OAAB_Data's picks from 2.6.2, "0:0:1;0:0:6;0:1:0", came back in 2.7.1 as
// MGE XE, a seventh engine option that does not exist, and Breton Knife-Ears:
// the new installer had moved its engine group to the front. Each pick now
// carries its step, group and option names, and a replay finds the option by
// name wherever the update moved it.
//
// Qt Core only; the wizard's own tests drive it.

#include "fomod_types.h"

#include <QSet>
#include <QString>

namespace fomod_choices {

// The wizard's key for one option, (si << 32) | (gi << 16) | pi. A group's key
// is this shifted right by 16.
constexpr quint64 key(int si, int gi, int pi)
{
    return (quint64(si) << 32) | (quint64(gi) << 16) | quint64(pi);
}

// The picks in `checked`, in installer order, each as
// "si:gi:pi:<step>/<group>/<option>" with the names percent-encoded, so no
// name can contain a separator; joined by ';'.
QString encode(const QList<FomodStep> &steps, const QSet<quint64> &checked);

// The options a stored string picks in THIS installer.
//
// A named entry is found, in order: at its recorded position if the three
// names there still match; else wherever the same step, group and option
// names are; else wherever the same group and option are, whatever the step
// is now called. Never by option name alone - "Yes" and "No" are in half the
// groups of any installer. An entry found nowhere is dropped.
//
// A bare "si:gi:pi" entry (recorded before names were) carries nothing to
// check, so the record is trusted only if every one of its positions exists in
// this installer. One that cannot - an option past the end of its group -
// proves the record describes another layout, and then none of it is used:
// the wizard falls back to its own recommendations, and the install that
// follows records names.
QSet<quint64> decode(const QList<FomodStep> &steps, const QString &stored);

} // namespace fomod_choices
