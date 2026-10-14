// test_user_state_paths - where resolveUserStatePath puts a state file: under
// AppDataLocation for an AppImage, next to a writable binary otherwise, an
// existing file winning either way; and the one-time move of a file an older
// in-tree layout left one directory above the binary.
//
// Runs with QStandardPaths test mode on, so AppDataLocation is a throwaway
// folder under ~/.qttest and the user's real one is never touched.

#include "user_state_paths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QUuid>

#include <iostream>

#include "test_harness.h"

namespace {

// A name no earlier run can have left behind.
QString freshName()
{
    return QStringLiteral("state_%1.txt")
        .arg(QUuid::createUuid().toString(QUuid::Id128).left(8));
}

QString appData()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}

bool touch(const QString &path)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write("x");
    return true;
}

void testAnAppImageAlwaysUsesAppData()
{
    std::cout << "testAnAppImageAlwaysUsesAppData\n";
    const QString name = freshName();
    qputenv("APPIMAGE", QByteArray("/tmp/Nerevarine.AppImage"));
    const QString got = resolveUserStatePath(name);
    qunsetenv("APPIMAGE");
    const QString want = QFileInfo(appData() + "/" + name).absoluteFilePath();
    check("resolves under AppDataLocation", got == want, got, want);
    check("the AppData folder was created", QFileInfo(appData()).isDir());
}

void testAWritableBinaryDirIsTheDefault()
{
    std::cout << "testAWritableBinaryDirIsTheDefault\n";
    const QString name = freshName();
    const QString want = QFileInfo(QCoreApplication::applicationDirPath()
                                   + "/" + name).absoluteFilePath();
    const QString got = resolveUserStatePath(name);
    check("a fresh file goes next to the binary", got == want, got, want);
}

void testAnExistingAppDataFileWins()
{
    std::cout << "testAnExistingAppDataFileWins\n";
    const QString name = freshName();
    const QString existing = appData() + "/" + name;
    check("can seed AppData", touch(existing));
    const QString got = resolveUserStatePath(name);
    check("the file already in AppData is the one found",
          got == QFileInfo(existing).absoluteFilePath(), got);
    QFile::remove(existing);
}

void testAFileOneDirectoryUpIsMovedNextToTheBinaryOnce()
{
    std::cout << "testAFileOneDirectoryUpIsMovedNextToTheBinaryOnce\n";
    const QString name   = freshName();
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString up     = appDir + "/../" + name;
    const QString next   = appDir + "/" + name;
    check("can write one directory up", touch(up));
    const QString got = resolveUserStatePath(name);
    check("resolves next to the binary",
          got == QFileInfo(next).absoluteFilePath(), got);
    check("the old file moved there", QFile::exists(next) && !QFile::exists(up));
    QFile::remove(next);
    QFile::remove(up);
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QStandardPaths::setTestModeEnabled(true);

    testAnAppImageAlwaysUsesAppData();
    testAWritableBinaryDirIsTheDefault();
    testAnExistingAppDataFileWins();
    testAFileOneDirectoryUpIsMovedNextToTheBinaryOnce();

    std::cout << "\n" << s_passed << " passed, " << s_failed << " failed\n";
    return s_failed == 0 ? 0 : 1;
}
