#pragma once

// subprocess - the one place external programs get launched, so the
// AppImage Qt-environment scrub can never be forgotten.
//
// Background: under the AppImage runtime, linuxdeploy's AppRun exports
// LD_LIBRARY_PATH / QT_PLUGIN_PATH / QML2_IMPORT_PATH / ... so OUR bundled
// Qt6 finds its own plugins.  QProcess inherits the parent environment by
// default, so any *foreign* program we spawn (LOOT, the OpenMW Launcher,
// the game's own launcher, even xdg-open routing to a Qt app) would resolve
// libQt6Core.so and the platform/IM plugins from inside our squashfs - an
// ABI mismatch that makes the child silently exit with
// "QIBusPlatformInputContext: invalid portal bus".
//
// childEnvironment() rebuilds the pre-AppImage environment (restoring the
// _ORIG values linuxdeploy stashes, stripping our LD_PRELOAD shim).  It is
// a NO-OP outside the AppImage runtime, so dev builds, distro packages and
// the test suite see the system environment unchanged.
//
// Every launch helper here applies that environment.  New launch sites
// should go through these instead of QProcess::startDetached / ::execute so
// the scrub is structural, not a thing each call site has to remember.
// (This had regressed before - LOOT and the OpenMW Launcher both need it.)

#include <QProcessEnvironment>
#include <QString>
#include <QStringList>

class QProcess;

namespace subprocess {

// The environment a foreign child should run with.  Equal to the current
// system environment outside an AppImage; the de-mangled pre-AppImage
// environment inside one.
QProcessEnvironment childEnvironment();

// Apply childEnvironment() to an instance you drive yourself (you need the
// QProcess for stdout, signals, or waitForFinished).  Call before start().
void applyEnv(QProcess &p);

// Fire-and-forget launch with the scrub applied.  Same return contract as
// QProcess::startDetached (true on successful spawn).
bool startDetached(const QString &program, const QStringList &args = {});

// Synchronous launch with the scrub applied; returns the child's exit code,
// or -1 if it could not be started / timed out / crashed.  Mirrors
// QProcess::execute for the fire-and-forget "run a CLI tool" sites.
int execute(const QString &program, const QStringList &args = {},
            int timeoutMs = 30000);

} // namespace subprocess
