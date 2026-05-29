#include "subprocess.h"

#include <QProcess>

namespace subprocess {

// Moved verbatim from MainWindow's file-local childProcessEnvironment().
// linuxdeploy's AppRun exports LD_LIBRARY_PATH / QT_PLUGIN_PATH /
// QML2_IMPORT_PATH / XDG_DATA_DIRS so the Qt6 we ship can find its own
// plugins. QProcess inherits the parent env by default, so a child Qt
// app would resolve `libQt6Core.so` and the platform/IM plugins from
// inside our squashfs - almost always an ABI mismatch with whatever Qt
// the child was linked against. The user-visible failure mode is a
// stray "QIBusPlatformInputContext: invalid portal bus" on the
// terminal followed by the child exiting silently before doing any
// real work (the IBus platform input plugin from our bundled Qt
// can't talk to the child's session bus, and the platform-plugin
// initialization aborts with it).
//
// linuxdeploy stashes the pre-AppImage values with an "_ORIG" suffix
// for exactly this purpose: restore them where present, otherwise
// unset. No-op outside the AppImage runtime.
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

    // Our AppRun wrapper prepends an in-AppImage glibc-compat shim onto
    // LD_PRELOAD (see build-appimage.sh step 6b). The shim is harmless for
    // the parent process but, when inherited by a foreign Qt binary like
    // LOOT, it dlopens an in-squashfs .so from the child's address space
    // and the child silently exits during platform-plugin init - the user-
    // visible signal is "QIBusPlatformInputContext: invalid portal bus"
    // followed by no work being done. There's no _ORIG saved for
    // LD_PRELOAD because the wrapper only ever prepends, so strip every
    // entry that lives under APPDIR and keep any user-supplied entries.
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
    // The instance overload of startDetached() (unlike the static one)
    // honours a custom process environment - which is the whole reason this
    // helper exists.
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
