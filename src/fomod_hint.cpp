#include "fomod_hint.h"

#include <QRegularExpression>
#include <QStringList>

namespace fomod {

bool asksAboutAnotherMod(const QString &stepName, const QString &groupName)
{
    // ModuleConfig.xml text carries the author's line breaks and indentation, so
    // fold whitespace first: "Do you\n    use X?" is the same question.
    const QString question =
        (stepName + u' ' + groupName).simplified().toLower();

    // Phrases that can only be asking after something the user already has.
    // Deliberately short of "patch", "addon" and "compatibility": plenty of
    // options patch the mod's own files, and a wrong "not in your modlist" is
    // worse than no hint at all.
    static const QStringList kSetupPhrases = {
        QStringLiteral("do you have"),
        QStringLiteral("do you use"),
        QStringLiteral("do you run"),
        QStringLiteral("do you own"),
        QStringLiteral("are you using"),
        QStringLiteral("are using"),        // "if you are using X"
        QStringLiteral("if you have"),
        QStringLiteral("if you use"),
        QStringLiteral("if using"),
        QStringLiteral("users of"),
        QStringLiteral("already have"),
        QStringLiteral("already installed"),
        QStringLiteral("in your load order"),
        QStringLiteral("in your modlist"),
        QStringLiteral("in your mod list"),
    };
    for (const QString &phrase : kSetupPhrases)
        if (question.contains(phrase)) return true;

    // "Is X installed?" - the mod name sits in the middle, where no fixed phrase
    // can reach it, so match the end of the clause instead. Guarded against
    // "files to be installed", which describes what this installer is about to
    // do rather than what the user already runs.
    static const QRegularExpression kAlreadyInstalled(
        QStringLiteral("\\binstalled\\b\\s*(?:[?.!,]|$)"));
    static const QRegularExpression kAboutToInstall(
        QStringLiteral("\\b(?:be|been|being|get|gets|getting)\\s+installed\\b"));
    return kAlreadyInstalled.match(question).hasMatch()
        && !kAboutToInstall.match(question).hasMatch();
}

} // namespace fomod
