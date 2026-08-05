#include "subprocess.h"

#include <QProcess>

namespace subprocess {

// linuxdeploy's AppRun exports LD_LIBRARY_PATH / QT_PLUGIN_PATH / etc so our
// bundled Qt6 finds its own plugins. A child Qt app inherits these and would
// load libQt6Core.so + platform/IM plugins from our squashfs - an ABI mismatch
// with whatever Qt it was built against. Symptom: "QIBusPlatformInputContext:
// invalid portal bus" then the child exits silently before doing any work.
//
// linuxdeploy stashes the originals with an "_ORIG" suffix: restore those where
// present, else unset. No-op outside the AppImage runtime.
QProcessEnvironment childEnvironment()
{
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    if (qEnvironmentVariableIsEmpty("APPIMAGE")) return env;

    static const QStringList kAppImageVars = {
        "LD_LIBRARY_PATH",            "QT_PLUGIN_PATH",
        "QT_QPA_PLATFORM_PLUGIN_PATH",
        "QML2_IMPORT_PATH",           "QML_IMPORT_PATH",
        "QTWEBENGINEPROCESS_PATH",
        "PYTHONHOME",                 "PYTHONPATH",
        "PERLLIB",
        "GTK_PATH",                   "GTK_DATA_PREFIX",
        "GTK_EXE_PREFIX",             "GTK_IM_MODULE_FILE",
        "GDK_PIXBUF_MODULE_FILE",     "GDK_PIXBUF_MODULEDIR",
        "GST_PLUGIN_SYSTEM_PATH",     "GST_PLUGIN_SYSTEM_PATH_1_0",
        "FONTCONFIG_FILE",            "FONTCONFIG_PATH",
        "XDG_DATA_DIRS",
    };
    for (const QString &v : kAppImageVars) {
        const QString orig = env.value(v + "_ORIG");
        if (!orig.isEmpty())
            env.insert(v, orig);
        else
            env.remove(v);
    }

    // AppRun prepends an in-AppImage glibc-compat shim onto LD_PRELOAD
    // (build-appimage.sh step 6b). Harmless for us, but a foreign Qt binary
    // like LOOT inheriting it dlopens an in-squashfs .so and exits silently
    // during platform-plugin init ("QIBusPlatformInputContext: invalid portal
    // bus", no work done). No _ORIG exists since the wrapper only prepends, so
    // strip every entry under APPDIR and keep user-supplied ones.
    if (env.contains("LD_PRELOAD")) {
        const QString appDir = qEnvironmentVariable("APPDIR");
        QStringList kept;
        const QStringList parts =
            env.value("LD_PRELOAD").split(':', Qt::SkipEmptyParts);
        for (const QString &p : parts) {
            if (!appDir.isEmpty() && p.startsWith(appDir + '/'))
                continue;
            kept << p;
        }
        if (kept.isEmpty()) env.remove("LD_PRELOAD");
        else                env.insert("LD_PRELOAD", kept.join(':'));
    }

    return env;
}

void applyEnv(QProcess &p)
{
    p.setProcessEnvironment(childEnvironment());
}

bool startDetached(const QString &program, const QStringList &args)
{
    // The instance startDetached() honours a custom env; the static one
    // doesn't, which is why this helper exists.
    QProcess p;
    p.setProgram(program);
    p.setArguments(args);
    p.setProcessEnvironment(childEnvironment());
    return p.startDetached();
}

int execute(const QString &program, const QStringList &args, int timeoutMs)
{
    QProcess p;
    p.setProgram(program);
    p.setArguments(args);
    p.setProcessEnvironment(childEnvironment());
    p.start();
    if (!p.waitForFinished(timeoutMs)) {
        p.kill();
        p.waitForFinished(1000);
        return -1;
    }
    if (p.exitStatus() != QProcess::NormalExit)
        return -1;
    return p.exitCode();
}

} // namespace subprocess
