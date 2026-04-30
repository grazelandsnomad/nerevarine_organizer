#include "reboot_check.h"

#include <QFileInfo>
#include <QString>
#include <QSysInfo>
#include <QtGlobal>

bool isRebootPending()
{
#ifdef Q_OS_LINUX
    if (QFileInfo::exists(QStringLiteral("/run/reboot-required")) ||
        QFileInfo::exists(QStringLiteral("/var/run/reboot-required")))
        return true;

    const QString running = QSysInfo::kernelVersion();
    if (running.isEmpty()) return false;

    // FHS layouts plus the NixOS activation symlinks. Without the NixOS
    // entries the heuristic false-fires on NixOS, where modules live under
    // /run/{booted,current}-system/kernel-modules/lib/modules/<v>.
    static const char *const moduleRoots[] = {
        "/usr/lib/modules/",
        "/lib/modules/",
        "/run/booted-system/kernel-modules/lib/modules/",
        "/run/current-system/kernel-modules/lib/modules/",
    };
    for (const char *root : moduleRoots) {
        if (QFileInfo::exists(QString::fromLatin1(root) + running))
            return false;
    }
    return true;
#else
    return false;
#endif
}
