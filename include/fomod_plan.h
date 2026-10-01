#pragma once

// fomod_plan - what the FOMOD wizard decides before the user touches anything:
// which options start ticked, what each says beside its name, its tooltip, and
// the notes under it.
//
// All of it lived in FomodWizard::buildUi, a thousand lines of passes reading
// and writing the buttons as it built them, so the only way to test a decision
// was to build the dialog. The passes now work on a plan - one entry per option
// that answers the same calls the buttons did - and the wizard applies the
// finished plan to its buttons. The passes moved verbatim, and the result was
// checked against 925 recorded runs of 187 real installers
// (tests/test_fomod_corpus.cpp).
//
// The passes, in the order they run (their comments in fomod_plan.cpp say why):
//   A   an engine question - OpenMW over MGE XE, radio or checkbox
//   A2  a language question - English
//   A3  a game-runtime pair - the build matching the game, or none without
//       its script extender
//   B   a Yes/No about another mod - its presence in the modlist
//   C   checkbox options named after a mod, or a patch for one
//   D   patch hubs - every option on
//   E   options citing a Nexus mod page the modlist has, or lacks
//   G   alternative frameworks - the installed one
//   H   one mod with and without a framework - the half that works
//   F   options whose description names a mod they require
//   then the picks stored from a previous install, where nothing above settled
//   the group or the option by what the modlist holds.
//
// Qt Core only.

#include "fomod_types.h"
#include "game_runtime.h"

#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

namespace fomod {

// What the plan is made against.
struct PlanContext {
    QStringList         installedModNames;   // display names of what is installed
    QSet<QString>       installedNexusKeys;  // "game/modId" of what is installed
    QString             gameId;              // the profile's game
    game_runtime::Probe runtime;             // what the installed game says about itself
    QString             priorChoices;        // fomod_choices::encode, from a previous install
};

struct OptionPlan {
    bool        checked = false;
    bool        enabled = true;   // Required and NotUsable options are settled by the installer
    QString     text;             // the label, the option's name plus any badge
    QString     toolTip;
    // Footnotes under the option, in the order they were made. The wizard
    // inserts each directly under its option, so the last made shows first.
    QStringList notes;
};

struct GroupPlan {
    bool exclusive = false;      // radios: SelectExactlyOne, SelectAtMostOne
    // SelectAtMostOne may pick nothing, which a radio cannot return to once
    // picked, so the wizard adds a "None" radio of its own.
    bool hasNone     = false;
    bool noneChecked = false;
    QList<OptionPlan> options;
};

using Plan = QList<QList<GroupPlan>>;   // [step][group]

Plan planSelections(const QList<FomodStep> &steps, const PlanContext &ctx);

} // namespace fomod
