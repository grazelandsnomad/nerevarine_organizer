#include "heroic_launch.h"

#include <QDir>
#include <QFileInfo>

namespace heroic_launch {

Command build(const store_scan::HeroicRunner &runner, const QString &winePrefix,
              const QString &exePath, const QStringList &exeArgs)
{
    if (!runner.valid() || winePrefix.isEmpty() || exePath.isEmpty()) return {};

    Command c;
    c.program    = runner.bin;
    c.workingDir = QFileInfo(exePath).path();
    const QString prefix = QDir::cleanPath(winePrefix);

    if (runner.isProton()) {
        // Proton takes a verb and reads everything else out of the
        // environment. The header says what each variable is for and how it
        // was established; the order here is the order the test pins.
        //
        // Heroic spells a game it has no id for "heroic-0", so do the same.
        const QString appId = runner.appName.isEmpty() ? QStringLiteral("0")
                                                       : runner.appName;
        c.args = QStringList{QStringLiteral("run"), exePath} + exeArgs;
        c.env  = {{QStringLiteral("STEAM_COMPAT_DATA_PATH"), prefix},
                  {QStringLiteral("STEAM_COMPAT_CLIENT_INSTALL_PATH"),
                   QDir::homePath() + QStringLiteral("/.steam/steam")},
                  {QStringLiteral("STEAM_COMPAT_APP_ID"), QStringLiteral("0")},
                  {QStringLiteral("SteamAppId"), QStringLiteral("0")},
                  {QStringLiteral("SteamGameId"), QStringLiteral("heroic-") + appId}};
        return c;
    }

    // Plain Wine: the exe is the argument and WINEPREFIX names the prefix.
    // winemenubuilder is kept from writing desktop entries, as Heroic does.
    c.args = QStringList{exePath} + exeArgs;
    c.env  = {{QStringLiteral("WINEPREFIX"), prefix},
              {QStringLiteral("WINEDLLOVERRIDES"), QStringLiteral("winemenubuilder.exe=d")}};
    return c;
}

} // namespace heroic_launch
