#include "deployment_report.h"

namespace deployment_report {

namespace {
QString yn(bool b) { return b ? QStringLiteral("  [found]") : QStringLiteral("  [MISSING]"); }
}

QString format(const Facts &f)
{
    QString r;
    r += "Game:        " + f.gameName + " (" + f.gameId + ")\n";
    r += "Load order:  " + f.loadOrderStyle + "\n";
    r += "Steam appid: " + (f.steamAppId.isEmpty() ? QStringLiteral("(none)")
                                                    : f.steamAppId) + "\n\n";

    r += "Data folder:\n  " + (f.dataFolder.path.isEmpty()
            ? QStringLiteral("*** NOT RESOLVED - is the game installed via Steam/GOG? ***")
            : f.dataFolder.path + yn(f.dataFolder.exists)) + "\n\n";

    if (f.installDirKnown) {
        r += "Script extender: " + (f.scriptExtender.isEmpty()
                ? QStringLiteral("not installed beside the game exe")
                : f.scriptExtender + " [found]") + "\n\n";
    }

    r += "Plugins.txt:\n  " + (f.pluginsTxt.path.isEmpty()
            ? QStringLiteral("*** NOT RESOLVED - run the game once via Steam so the prefix exists ***")
            : f.pluginsTxt.path + yn(f.pluginsTxt.exists)) + "\n\n";

    if (f.showOblivionIni) {
        r += "Oblivion.ini:\n  " + (f.oblivionIni.path.isEmpty()
                ? QStringLiteral("*** NOT RESOLVED ***")
                : f.oblivionIni.path + yn(f.oblivionIni.exists)) + "\n";
        r += "  (only edited if it already exists - run the game once first)\n\n";
    }

    r += "Deployment state:\n";
    r += "  manifest: " + f.manifestPath + (f.haveManifest
            ? "  [" + QString::number(f.deployedFileCount) + " files currently deployed]"
            : QStringLiteral("  [nothing deployed yet]")) + "\n";
    r += "  backups:  " + f.backupDir + "\n\n";

    r += "Mods to deploy: " + QString::number(f.enabledInstalledMods)
       + " enabled+installed mod(s), "
       + QString::number(f.dataRootCount) + " data root(s)\n\n";

    r += "Proton prefix candidates probed (for Plugins.txt / ini):\n";
    if (f.prefixCandidates.isEmpty())
        r += "  (none - no steam appid or resolved install path)\n";
    for (int i = 0; i < f.prefixCandidates.size(); ++i) {
        const QString tag = i < f.prefixExists.size() ? f.prefixExists.at(i) : QString();
        r += "  " + f.prefixCandidates.at(i) + tag + "\n";
    }

    return r;
}

} // namespace deployment_report
