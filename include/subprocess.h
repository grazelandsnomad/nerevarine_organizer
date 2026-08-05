#pragma once

// subprocess - the one place external programs get launched, so the AppImage
// Qt-env scrub can't be forgotten.
//
// Under the AppImage runtime linuxdeploy's AppRun exports LD_LIBRARY_PATH /
// QT_PLUGIN_PATH / QML2_IMPORT_PATH so our bundled Qt6 finds its own plugins.
// QProcess inherits the parent env, so any foreign program we spawn (LOOT,
// OpenMW Launcher, xdg-open routing to a Qt app) resolves libQt6Core.so + the
// platform/IM plugins from inside our squashfs - an ABI mismatch that makes
// the child silently exit with "QIBusPlatformInputContext: invalid portal bus".
//
// childEnvironment() rebuilds the pre-AppImage env (restores linuxdeploy's
// _ORIG values, strips our LD_PRELOAD shim). NO-OP outside the AppImage, so
// dev builds, distro packages and tests see the system env unchanged.
//
// Route new launch sites through these helpers, not QProcess::startDetached /
// ::execute, so the scrub is structural. Has regressed before - LOOT and the
// OpenMW Launcher both need it.

#include <QProcessEnvironment>
#include <QString>
#include <QStringList>

class QProcess;

namespace subprocess {

// Env a foreign child should run with: the system env outside an AppImage,
// the de-mangled pre-AppImage env inside one.
QProcessEnvironment childEnvironment();

// Apply childEnvironment() to a QProcess you drive yourself (when you need
// stdout, signals, or waitForFinished). Call before start().
void applyEnv(QProcess &p);

// Fire-and-forget launch with the scrub. Like QProcess::startDetached
// (true on successful spawn).
bool startDetached(const QString &program, const QStringList &args = {});

// Synchronous launch with the scrub; returns the child's exit code, or -1 if
// it couldn't start / timed out / crashed. Like QProcess::execute.
int execute(const QString &program, const QStringList &args = {},
            int timeoutMs = 30000);

} // namespace subprocess
