// tests/test_modlist_io.cpp
//
// Pins modlist_io::writeModlistFile - the synchronous file-IO half of
// the async save chain in MainWindow::saveModList.  Covers what was
// the highest-risk uncovered code in 0.4 right after the
// QtConcurrent::run move: a write failure on the worker thread used
// to be silent (only a qCWarning to log.txt, no UI signal).  The 0.4
// banner-on-failure plumbing depends on this helper returning the
// QFile errorString rather than swallowing it.
//
// "Integration" = end-to-end through the real safefs::snapshotBackup +
// QFile + QTextStream stack on a real on-disk fixture.  No MainWindow
// instantiation needed; the helper is the carved-out testable nucleus.

#include "modlist_io.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <iostream>

namespace {

int s_passed = 0;
int s_failed = 0;

void check(const char *name, bool ok, const QString &hint = {})
{
    if (ok) {
        std::cout << "  \033[32m✓\033[0m " << name << "\n";
        ++s_passed;
    } else {
        std::cout << "  \033[31m✗\033[0m " << name;
        if (!hint.isEmpty()) std::cout << " (" << hint.toStdString() << ")";
        std::cout << "\n";
        ++s_failed;
    }
}

QString readFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    return QString::fromUtf8(f.readAll());
}

// ---------------------------------------------------------------------

void testWriteModlistFile_success()
{
    std::cout << "\n[writeModlistFile: success returns nullopt + writes content]\n";
    QTemporaryDir tmp;
    if (!tmp.isValid()) { std::cerr << "tmp setup failed\n"; std::exit(1); }
    const QString path = tmp.path() + "/modlist.txt";
    const QString content = "v2-schema\nrow1\nrow2\n";

    auto err = modlist_io::writeModlistFile(path, content);
    check("returns nullopt on success", !err.has_value(),
          err.value_or("(success)"));
    check("file lands on disk", QFile::exists(path));
    check("content matches", readFile(path) == content);
}

void testWriteModlistFile_overwriteCreatesBackup()
{
    std::cout << "\n[writeModlistFile: overwrite snapshot-backs the previous content]\n";
    QTemporaryDir tmp;
    if (!tmp.isValid()) { std::cerr << "tmp setup failed\n"; std::exit(1); }
    const QString path = tmp.path() + "/modlist.txt";

    // Pre-existing content.
    {
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly)) {
            std::cerr << "couldn't pre-seed " << path.toStdString() << "\n";
            std::exit(2);
        }
        f.write("OLD\n");
    }

    auto err = modlist_io::writeModlistFile(path, "NEW\n");
    check("nullopt on overwrite", !err.has_value(),
          err.value_or("(success)"));
    check("new content present", readFile(path) == "NEW\n");

    // safefs::snapshotBackup creates a sibling file matching
    // "<basename>.bak.*".  Verify at least one such backup exists so
    // the carve-out hasn't lost the snapshot step.
    QDir dir(tmp.path());
    const QStringList baks = dir.entryList(
        {"modlist.txt.bak.*"}, QDir::Files);
    check("snapshot backup created", !baks.isEmpty(),
          QString::number(baks.size()) + " backup(s)");
}

void testWriteModlistFile_missingParentReturnsError()
{
    std::cout << "\n[writeModlistFile: missing parent dir → error string returned]\n";
    QTemporaryDir tmp;
    if (!tmp.isValid()) { std::cerr << "tmp setup failed\n"; std::exit(1); }

    // Cross-platform way to force QFile::open(WriteOnly) to fail:
    // point at a target whose parent dir doesn't exist.  QFile does
    // NOT auto-mkdir parents, so open() returns false on both Linux
    // and Windows.  This replaces an earlier POSIX-chmod-0500 test
    // that silently passed on Windows because NTFS doesn't honour
    // POSIX permission bits through MinGW's chmod() syscall - the
    // dir stayed writable and the write succeeded, breaking the
    // assertion that an error string came back.
    const QString path = tmp.path() + "/no_such_dir/sub/modlist.txt";
    auto err = modlist_io::writeModlistFile(path, "content");

    check("returns an error string on open failure", err.has_value());
    check("error string is non-empty",
          err.has_value() && !err->isEmpty());
    check("file was NOT created (no auto-mkdir)",
          !QFile::exists(path));
}

void testWriteModlistFile_emptyPathReturnsError()
{
    std::cout << "\n[writeModlistFile: empty path returns error]\n";
    auto err = modlist_io::writeModlistFile(QString(), "content");
    check("empty path produces an error", err.has_value());
}

void testWriteModlistFile_emptyContentSucceeds()
{
    std::cout << "\n[writeModlistFile: empty content writes a 0-byte file]\n";
    QTemporaryDir tmp;
    if (!tmp.isValid()) { std::cerr << "tmp setup failed\n"; std::exit(1); }
    const QString path = tmp.path() + "/empty.txt";

    auto err = modlist_io::writeModlistFile(path, QString());
    check("nullopt on empty content", !err.has_value());
    check("file exists",      QFile::exists(path));
    check("file is 0 bytes",  QFileInfo(path).size() == 0);
}

void testWriteModlistFile_serialOverwriteIsAtomicEnough()
{
    std::cout << "\n[writeModlistFile: two sequential writes leave the LAST content on disk]\n";
    QTemporaryDir tmp;
    if (!tmp.isValid()) { std::cerr << "tmp setup failed\n"; std::exit(1); }
    const QString path = tmp.path() + "/modlist.txt";

    // The async save chain in MainWindow uses
    // m_lastSaveFuture.waitForFinished() to serialize submits, so two
    // concurrent saveModList calls become two sequential
    // writeModlistFile calls on the worker.  This is the post-
    // serialization invariant: whoever ran second wins.
    (void)modlist_io::writeModlistFile(path, "FIRST\n");
    auto err = modlist_io::writeModlistFile(path, "SECOND\n");

    check("second write succeeds",          !err.has_value());
    check("second write's content lands",   readFile(path) == "SECOND\n");
}

void testWriteModlistFile_largeContentRoundtrip()
{
    std::cout << "\n[writeModlistFile: 1 MB roundtrip is byte-identical]\n";
    QTemporaryDir tmp;
    if (!tmp.isValid()) { std::cerr << "tmp setup failed\n"; std::exit(1); }
    const QString path = tmp.path() + "/big.txt";

    QString big;
    big.reserve(1 << 20);
    for (int i = 0; i < (1 << 20); ++i) big.append(QChar('A' + (i % 26)));

    auto err = modlist_io::writeModlistFile(path, big);
    check("nullopt on big write", !err.has_value());
    check("roundtrip is byte-identical (1 MB)", readFile(path) == big);
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    std::cout << "=== modlist_io::writeModlistFile ===\n";

    testWriteModlistFile_success();
    testWriteModlistFile_overwriteCreatesBackup();
    testWriteModlistFile_missingParentReturnsError();
    testWriteModlistFile_emptyPathReturnsError();
    testWriteModlistFile_emptyContentSucceeds();
    testWriteModlistFile_serialOverwriteIsAtomicEnough();
    testWriteModlistFile_largeContentRoundtrip();

    std::cout << "\n" << s_passed << " passed, " << s_failed << " failed\n";
    return s_failed == 0 ? 0 : 1;
}
