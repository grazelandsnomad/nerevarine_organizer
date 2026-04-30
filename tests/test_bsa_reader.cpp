// tests/test_bsa_reader.cpp
//
// Builds minimal TES3 BSA blobs on disk and round-trips them through
// bsa::listTes3BsaFiles.  Covers the happy path + the degradation paths
// the conflict inspector relies on ("empty list, don't throw" for every
// kind of bad input).
//
// Build + run:
//   ./build/tests/test_bsa_reader

#include "bsareader.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>

#include <cstring>
#include <iostream>

static int s_passed = 0;
static int s_failed = 0;

static void check(const char *name, bool ok)
{
    if (ok) {
        std::cout << "  \033[32m✓\033[0m " << name << "\n";
        ++s_passed;
    } else {
        std::cout << "  \033[31m✗\033[0m " << name << "\n";
        ++s_failed;
    }
}

static QByteArray u32le(quint32 v)
{
    QByteArray b(4, '\0');
    std::memcpy(b.data(), &v, 4);
    return b;
}

// Build a TES3 BSA with `names` as its file table.  File data is omitted -
// the reader never touches it.  See src/bsareader.cpp for the layout.
static QByteArray buildTes3Bsa(const QStringList &names)
{
    const int N = names.size();

    // Pack null-terminated names into the name block; record each starting
    // offset (relative to the name block) as we go.
    QByteArray nameBlock;
    QList<quint32> nameOffsets;
    for (const QString &n : names) {
        nameOffsets << quint32(nameBlock.size());
        nameBlock += n.toLatin1();
        nameBlock += '\0';
    }

    // hashOffset = distance from end of 12-byte header to the hash table:
    //   8*N (file records) + 4*N (name offsets) + nameBlockSize
    const quint32 hashOffset = quint32(8 * N + 4 * N + nameBlock.size());

    QByteArray out;
    out += u32le(0x100);               // version
    out += u32le(hashOffset);
    out += u32le(quint32(N));          // fileCount

    for (int i = 0; i < N; ++i) {
        out += u32le(0);               // record size
        out += u32le(0);               // record offset
    }
    for (int i = 0; i < N; ++i)
        out += u32le(nameOffsets[i]);

    out += nameBlock;

    // Hash table: 8 × N bytes.  Content ignored by the reader.
    out.append(8 * N, '\0');

    return out;
}

static QString writeBsa(const QTemporaryDir &dir, const QString &name,
                        const QByteArray &bytes)
{
    const QString path = dir.filePath(name);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return {};
    f.write(bytes);
    f.close();
    return path;
}

// -- Tests ---

static void testHappyPath()
{
    std::cout << "testHappyPath\n";

    QTemporaryDir dir;
    const QStringList in{
        "meshes\\foo.nif",
        "textures\\BAR.DDS",
        "sound\\fx\\hit.wav"
    };
    const QString path = writeBsa(dir, "ok.bsa", buildTes3Bsa(in));
    const QStringList got = bsa::listTes3BsaFiles(path);

    check("three files returned", got.size() == 3);
    check("backslashes → forward slashes",
          got.contains("meshes/foo.nif"));
    check("case lowered",
          got.contains("textures/bar.dds"));
    check("nested separator kept",
          got.contains("sound/fx/hit.wav"));
}

static void testMissingFile()
{
    std::cout << "testMissingFile\n";

    const QStringList got = bsa::listTes3BsaFiles("/definitely/not/here.bsa");
    check("missing file returns empty, not error", got.isEmpty());
}

static void testWrongVersion()
{
    std::cout << "testWrongVersion\n";

    // Valid Morrowind-ish layout but version ≠ 0x100 - simulates a Skyrim
    // or Oblivion BSA landing in a mod folder.  Expected: silently skip.
    QTemporaryDir dir;
    QByteArray blob = buildTes3Bsa({"meshes\\foo.nif"});
    // Overwrite version field with 0x67 (TES4-era).
    QByteArray v = u32le(0x67);
    std::memcpy(blob.data(), v.constData(), 4);

    const QString path = writeBsa(dir, "skyrim-ish.bsa", blob);
    const QStringList got = bsa::listTes3BsaFiles(path);
    check("non-TES3 BSA returns empty", got.isEmpty());
}

static void testTruncated()
{
    std::cout << "testTruncated\n";

    QTemporaryDir dir;
    QByteArray blob = buildTes3Bsa({"meshes\\foo.nif", "textures\\bar.dds"});
    blob.truncate(blob.size() / 2);

    const QString path = writeBsa(dir, "truncated.bsa", blob);
    const QStringList got = bsa::listTes3BsaFiles(path);
    check("truncated file returns empty, doesn't crash", got.isEmpty());
}

static void testTooShort()
{
    std::cout << "testTooShort\n";

    QTemporaryDir dir;
    const QString path = writeBsa(dir, "tiny.bsa", QByteArray("ab", 2));
    const QStringList got = bsa::listTes3BsaFiles(path);
    check("sub-header file returns empty", got.isEmpty());
}

static void testEmptyArchive()
{
    std::cout << "testEmptyArchive\n";

    QTemporaryDir dir;
    const QString path = writeBsa(dir, "empty.bsa", buildTes3Bsa({}));
    const QStringList got = bsa::listTes3BsaFiles(path);
    // fileCount==0 is treated as empty/uninteresting.
    check("empty archive returns empty list", got.isEmpty());
}

// Single-file archive: minimum non-empty case.  The conflict scanner must
// still see it so a BSA that contains exactly one retexture doesn't get
// mistaken for an empty / broken archive.
static void testSingleFileArchive()
{
    std::cout << "testSingleFileArchive\n";

    QTemporaryDir dir;
    const QString path = writeBsa(dir, "one.bsa",
                                   buildTes3Bsa({"meshes\\lonely.nif"}));
    const QStringList got = bsa::listTes3BsaFiles(path);
    check("one entry returned", got.size() == 1);
    check("normalised separator and case",
          got.contains("meshes/lonely.nif"));
}

// Duplicate filename entries: a malformed BSA could point two separate
// entries at the same offset, or repeat a name verbatim.  The parser
// should pass them through rather than silently deduplicate - the
// conflict inspector expects one Provider entry per TOC row, and the
// QMap key deduplication happens at a higher layer.
static void testDuplicateEntries()
{
    std::cout << "testDuplicateEntries\n";

    QTemporaryDir dir;
    // buildTes3Bsa writes names sequentially, so repeated strings become
    // repeated (but non-overlapping) entries.  That's enough to confirm
    // the parser doesn't drop them.
    const QStringList in{
        "meshes\\dup.nif", "meshes\\dup.nif", "meshes\\dup.nif"
    };
    const QString path = writeBsa(dir, "dup.bsa", buildTes3Bsa(in));
    const QStringList got = bsa::listTes3BsaFiles(path);

    check("three entries returned (duplicates preserved)", got.size() == 3);
    int dupCount = 0;
    for (const QString &e : got) if (e == "meshes/dup.nif") ++dupCount;
    check("all three are the same normalised path", dupCount == 3);
}

// Name offset pointing BEYOND the name block should fail safely.  A
// malformed BSA with an out-of-range offset must not crash or read past
// the end of the buffer; it returns empty.
static void testNameOffsetOutOfRange()
{
    std::cout << "testNameOffsetOutOfRange\n";

    QTemporaryDir dir;
    QByteArray blob = buildTes3Bsa({"meshes\\foo.nif", "textures\\bar.dds"});

    // Overwrite the second name offset (at header[12] + 8*N + 4*1 = 12+16+4 = 32)
    // with a value larger than the name block size.  32 is the offset to the
    // second uint32 in the name-offsets array for N=2.
    //   base = 12 (header)
    //   file records: 2 * 8 = 16  → next region at offset 28
    //   name offsets at [28..36); second offset at [32..36)
    QByteArray huge = u32le(0xFFFFFFFF);
    std::memcpy(blob.data() + 32, huge.constData(), 4);

    const QString path = writeBsa(dir, "oor.bsa", blob);
    const QStringList got = bsa::listTes3BsaFiles(path);
    check("out-of-range offset → empty, no crash", got.isEmpty());
}

// hashOffset smaller than the space the name-offset array alone would
// occupy is nonsensical - the name block would have negative size.
// Parser should return empty rather than attempting a negative read.
static void testHashOffsetTooSmall()
{
    std::cout << "testHashOffsetTooSmall\n";

    QTemporaryDir dir;
    QByteArray blob = buildTes3Bsa({"a", "b", "c"});
    // Overwrite hashOffset (at offset 4) with a value smaller than 12*N
    // for N=3 (i.e. < 36).  Pick 12 - smaller than the mandatory sections.
    QByteArray tiny = u32le(12);
    std::memcpy(blob.data() + 4, tiny.constData(), 4);

    const QString path = writeBsa(dir, "tiny-hash.bsa", blob);
    const QStringList got = bsa::listTes3BsaFiles(path);
    check("hashOffset < space required → empty", got.isEmpty());
}

// Absurd fileCount must not allocate multi-GB vectors or read forever.
// Defensive cap is set at 1e6 in the parser.
static void testAbsurdFileCount()
{
    std::cout << "testAbsurdFileCount\n";

    QTemporaryDir dir;
    QByteArray blob;
    blob += u32le(0x100);           // version
    blob += u32le(0);               // hashOffset - irrelevant under cap
    blob += u32le(0xFFFFFFFFu);     // fileCount - deliberately huge

    const QString path = writeBsa(dir, "evil.bsa", blob);
    const QStringList got = bsa::listTes3BsaFiles(path);
    check("huge fileCount → empty, no OOM", got.isEmpty());
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    testHappyPath();
    testMissingFile();
    testWrongVersion();
    testTruncated();
    testTooShort();
    testEmptyArchive();
    testSingleFileArchive();
    testDuplicateEntries();
    testNameOffsetOutOfRange();
    testHashOffsetTooSmall();
    testAbsurdFileCount();

    std::cout << "\n"
              << s_passed << " passed, "
              << s_failed << " failed\n";
    return s_failed == 0 ? 0 : 1;
}
