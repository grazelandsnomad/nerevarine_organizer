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

struct FomodPlugin {
    QString name;
    QString description;
    // <image path="fomod\images\x.jpg"> - archive-root-relative, usually
    // with backslashes; resolved case-insensitively at display time. Empty
    // when the author supplied none.
    QString imagePath;
    QString type;   // "Required" | "Recommended" | "Optional" | "NotUsable" | "CouldBeUsable"
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
// When the <dependencies> flag requirements are met by the user's step
// selections, the pattern's files/folders install on top of what the plugins
// provided. Operator "And" (all match) or "Or" (any match); default "And".
struct FomodPattern {
    QString op = QStringLiteral("And");   // "And" | "Or"
    QList<FomodFlagValue> flagDeps;       // required (flag, value) pairs
    QList<FomodFile> files;
    QList<FomodFile> folders;
};
