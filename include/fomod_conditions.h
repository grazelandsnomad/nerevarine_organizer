#pragma once

// fomod_conditions - the questions a FOMOD installer asks about the setup it
// installs into, read from ModuleConfig.xml and answered.
//
// Installers ask them in two places. An option's type can depend on them:
//
//   Bunker Hill CTD Fix - Alternate Versions        (Necessity NG-AE)
//     3DNPC               Optional, but Recommended when 3DNPC_FO4.esp is active
//     Scrap Everything    Optional, but Recommended when ScrapEverything.esp is
//     ...
//
// and so can which files a pick installs (<conditionalFileInstalls>). The
// wizard used to read the first as if the condition always held - every one
// of those options "Recommended", so the last one stayed selected on a list
// with neither mod - and the second as one flat list of flags, so a nested
// And(And(build=ae, dlc=yes), prp=no) lost its last leaf and installed the
// PRP and the plain main files both.
//
// Three answers, not two. A file the manager cannot see, or a game version it
// could not read, is Unknown, and Unknown never decides anything: a type
// pattern that comes out Unknown is skipped, and a conditional install that
// does installs nothing. The installer's default stands instead - the
// author's own answer for "the condition does not hold", and the safer of
// the two mistakes: a patch wrongly ticked can stop the game on a missing
// master, a patch wrongly left off costs one click.
//
// Qt Core only.

#include "fomod_types.h"
#include "pe_info.h"

#include <QHash>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>

class QXmlStreamReader;

namespace fomod {

// Readers for FomodWizard::parse(), called with `xml` on the start element
// they are named for; each consumes through its matching end element. Names
// are matched case-insensitively, as the rest of the parse does.
FomodCondition readDependencies(QXmlStreamReader &xml);
// Fills plugin.type (a plain <type>, or a <dependencyType>'s <defaultType>)
// and plugin.typePatterns.
void readTypeDescriptor(QXmlStreamReader &xml, FomodPlugin &plugin);

enum class FileState { Missing, Inactive, Active };

// What the setup holds at a path relative to the game's data folder:
// "3DNPC_FO4.esp", "F4SE\Plugins\RobCo_Patcher.dll". nullopt when that
// cannot be known; an empty function knows nothing at all.
using FileStateFn = std::function<std::optional<FileState>(const QString &path)>;

struct Facts {
    FileStateFn             file;
    pe_info::Version        game;    // invalid = not known
    QHash<QString, QString> flags;   // raised by the picks so far; unset reads as ""
};

enum class Truth { False, True, Unknown };

// And: False if any child is, else Unknown if any child is, else True.
// Or: True if any child is, else Unknown if any child is, else False.
// An empty And holds and an empty Or does not. A manager version always
// holds - this is a manager, and new enough; a leaf the reader did not know
// is Unknown.
Truth evaluate(const FomodCondition &condition, const Facts &facts);

// What an option is in this setup, and why.
struct TypeVerdict {
    QString type;            // what to treat the option as
    int     pattern = -1;    // the pattern that decided; -1 = the default did
    // Decided by what the setup holds - a file or the game version took part
    // with a known answer - and no pattern before the decision was Unknown.
    // Such a verdict is a fact about this setup, not a preference.
    bool    fromFacts = false;
    // Files asked to be active: found by the deciding pattern; not found by
    // the patterns before it; and of those, the ones a pattern that would
    // have recommended (or required) the option asked for.
    QStringList found;
    QStringList lacked;
    QStringList lackedToRecommend;
    bool    byGame = false;  // a game version took part in the deciding pattern
};

// The first pattern that holds decides; Unknown ones are skipped; none, and
// the default (plugin.type) does. An option with no patterns is plugin.type
// and never fromFacts.
TypeVerdict resolveType(const FomodPlugin &plugin, const Facts &facts);

} // namespace fomod
