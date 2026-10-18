#pragma once

// fomod_types - a parsed ModuleConfig.xml, as plain structs.
//
// Split out of fomodwizard.h so code that reasons about an installer without
// showing one (fomod_choices, and the tests) needs Qt Core only.

#include <QList>
#include <QString>

struct FomodFile {
    QString source;       // path relative to archive root
    QString destination;  // path relative to mod root (empty = same as source filename)
    int     priority = 0;
};

struct FomodFlagValue {
    QString name;    // flag name (e.g. "Default")
    QString value;   // flag value when this plugin is picked (usually "Active")
};

// A <dependencies> block, as the installer wrote it: And / Or over leaves and
// further blocks, nested as deep as the author went. Necessity NG-AE picks its
// main files with And(And(build=ae, dlc=yes), prp=no), and a reader that kept
// only one level installed the PRP and the plain versions both.
// fomod_conditions answers one against the profile.
struct FomodCondition {
    enum class Kind {
        All,       // <dependencies operator="And">: every child holds
        Any,       // <dependencies operator="Or">: one child holds
        File,      // <fileDependency file= state="Active|Inactive|Missing">
        Flag,      // <flagDependency flag= value=>: set by an earlier pick
        Game,      // <gameDependency version=>: the game is at least this
        Manager,   // <fommDependency version=>: the mod manager is at least this
        Other,     // a leaf this reader does not know - never answered
    };
    Kind    kind = Kind::All;
    QString subject;   // File: the path; Flag: the flag's name
    QString value;     // File: the state; Flag: the value; Game, Manager: the version
    QList<FomodCondition> children;   // All and Any
};

// One <pattern> of a <dependencyType>: the type an option takes while `when`
// holds. The first pattern that holds decides; none does, the default does.
struct FomodTypePattern {
    FomodCondition when;
    QString        type;
};

struct FomodPlugin {
    QString name;
    QString description;
    // <image path="fomod\images\x.jpg"> - archive-root-relative, usually
    // with backslashes; resolved case-insensitively at display time. Empty
    // when the author supplied none.
    QString imagePath;
    // "Required" | "Recommended" | "Optional" | "NotUsable" | "CouldBeUsable":
    // the plain <type>, or a <dependencyType>'s <defaultType>.
    QString type;
    // A <dependencyType>'s patterns, in order; empty for a plain <type>.
    // "Recommended when 3DNPC_FO4.esp is active" lives here, and reading it
    // as plain "Recommended" pre-selected options for mods nobody had.
    QList<FomodTypePattern> typePatterns;
    QList<FomodFile> files;    // individual files to install
    QList<FomodFile> folders;  // directories to install (contents copied)
    // Flags the plugin raises when selected. Many FOMODs use these with
    // conditionalFileInstalls below instead of attaching files to a plugin;
    // honour them at install or the mod ends up empty (EKM Corkbulb Retexture).
    QList<FomodFlagValue> conditionFlags;
};

struct FomodGroup {
    QString name;
    // "SelectExactlyOne" | "SelectAtMostOne" | "SelectAtLeastOne" | "SelectAny" | "SelectAll"
    QString type;
    QList<FomodPlugin> plugins;
};

struct FomodStep {
    QString name;
    QList<FomodGroup> groups;
};

// Module-level <conditionalFileInstalls>/<patterns>/<pattern>.
// When `when` holds against the user's picks (their conditionFlags) and the
// profile, the pattern's files/folders install on top of what the plugins
// provided. Every pattern is tested; any number may install. A pattern with
// no <dependencies> is an empty All, which always holds.
struct FomodPattern {
    FomodCondition   when;
    QList<FomodFile> files;
    QList<FomodFile> folders;
};
