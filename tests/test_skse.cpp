// Whether the script-extender plugins on disk can run against the installed
// game.
//
// The profile this exists for, measured file by file: Skyrim reported 1.7.99,
// SKSE and the Address Library had both been updated, and the game still died
// at startup with "Unsupported address library format: 5" from a DLL it did
// not name. The address library ships one database per game version stamped
// with a format number; every one up to 1.6.1179 is format 1 or 2 and the one
// for 1.7.99 is format 5, which the plugins compiled before it refuse. Eight
// mods were in that state; a ninth, PrivateProfileRedirector, scans for byte
// signatures instead and was never involved.
//
// The fixtures below are those real values. The PE images are built here byte
// by byte rather than committed as binaries.

#include "pe_info.h"
#include "skse_check.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QString>

#include <iostream>

#include "test_harness.h"

namespace {

// -- a PE image, assembled ------------------------------------------------
//
// One section holding both the version resource and the export table, so the
// parser has to do the RVA-to-file-offset translation for real.

constexpr quint32 kSecRva = 0x1000;
constexpr quint32 kSecRaw = 0x400;

// Grows to fit rather than writing past the end: the SKSE record alone is 848
// bytes and getting the section size wrong here would be a bug in the test,
// not in the thing under test.
void put32(QByteArray &b, int at, quint32 v)
{
    if (b.size() < at + 4) b.resize(at + 4, '\0');
    for (int i = 0; i < 4; ++i) b[at + i] = char((v >> (8 * i)) & 0xff);
}
void put16(QByteArray &b, int at, quint16 v)
{
    if (b.size() < at + 2) b.resize(at + 2, '\0');
    for (int i = 0; i < 2; ++i) b[at + i] = char((v >> (8 * i)) & 0xff);
}
quint32 rva(int offsetInSection) { return kSecRva + quint32(offsetInSection); }

struct PeSpec {
    quint32 stamp        = 0;
    int     verMajor     = 0, verMinor = 0, verBuild = 0, verSub = 0;
    bool    withVersion  = true;
    bool    withExport   = false;
    QString skseName, skseAuthor;
    quint32 independence = 0;
    QList<quint32> compatible;
};

QByteArray buildPe(const PeSpec &spec)
{
    // Section payload first: every RVA below is computed from where things
    // land in here.
    QByteArray sec(0x800, '\0');   // room for the 848-byte SKSE record at 0x200

    if (spec.withVersion) {
        // Three levels of resource directory, then a data entry, then the
        // blob. Directory offsets are relative to the tree, which starts at 0.
        auto dir = [&sec](int at, quint32 id, quint32 target, bool isDir) {
            put16(sec, at + 12, 0);          // named entries
            put16(sec, at + 14, 1);          // id entries
            put32(sec, at + 16, id);
            put32(sec, at + 20, target | (isDir ? 0x80000000u : 0u));
        };
        dir(0x00, 16, 0x18, true);           // RT_VERSION -> name dir
        dir(0x18, 1,  0x30, true);           // name 1     -> language dir
        dir(0x30, 1033, 0x48, false);        // language   -> data entry

        put32(sec, 0x48, rva(0x60));         // OffsetToData
        put32(sec, 0x4c, 0x60);              // Size

        // A UTF-16 key precedes VS_FIXEDFILEINFO, which is why it is found by
        // its signature rather than at a fixed offset.
        const QByteArray key("V\0S\0_\0V\0E\0R\0S\0I\0O\0N\0", 20);
        sec.replace(0x60, key.size(), key);
        put32(sec, 0x78, 0xFEEF04BD);
        put32(sec, 0x7c, 0x00010000);
        put32(sec, 0x80, quint32(spec.verMajor) << 16 | quint32(spec.verMinor));
        put32(sec, 0x84, quint32(spec.verBuild) << 16 | quint32(spec.verSub));
    }

    if (spec.withExport) {
        put32(sec, 0x100 + 20, 1);           // NumberOfFunctions
        put32(sec, 0x100 + 24, 1);           // NumberOfNames
        put32(sec, 0x100 + 28, rva(0x140));  // AddressOfFunctions
        put32(sec, 0x100 + 32, rva(0x150));  // AddressOfNames
        put32(sec, 0x100 + 36, rva(0x160));  // AddressOfNameOrdinals

        put32(sec, 0x140, rva(0x200));       // the version data itself
        put32(sec, 0x150, rva(0x170));       // the export's name
        put16(sec, 0x160, 0);                // ordinal

        const QByteArray nm("SKSEPlugin_Version");
        sec.replace(0x170, nm.size(), nm);

        put32(sec, 0x200 + 0, 1);            // dataVersion
        put32(sec, 0x200 + 4, 0x00010000);   // pluginVersion
        const QByteArray n = spec.skseName.toLatin1();
        const QByteArray a = spec.skseAuthor.toLatin1();
        sec.replace(0x200 + 8,   n.size(), n);
        sec.replace(0x200 + 264, a.size(), a);
        put32(sec, 0x200 + 776, spec.independence);
        for (int i = 0; i < spec.compatible.size() && i < 16; ++i)
            put32(sec, 0x200 + 780 + 4 * i, spec.compatible[i]);
    }

    QByteArray img(int(kSecRaw), '\0');
    img[0] = 'M'; img[1] = 'Z';
    put32(img, 0x3c, 0x80);
    img[0x80] = 'P'; img[0x81] = 'E';
    put16(img, 0x80 + 6,  1);                // one section
    put32(img, 0x80 + 8,  spec.stamp);
    put16(img, 0x80 + 20, 240);              // SizeOfOptionalHeader (PE32+)
    put16(img, 0x98, 0x20b);                 // PE32+ magic
    put32(img, 0x98 + 112,      spec.withExport ? rva(0x100) : 0);   // export dir
    put32(img, 0x98 + 112 + 16, spec.withVersion ? rva(0x00) : 0);   // resource dir

    const int st = 0x98 + 240;               // section table
    const QByteArray sname(".rdata");
    img.replace(st, sname.size(), sname);
    put32(img, st + 8,  quint32(sec.size()));   // VirtualSize
    put32(img, st + 12, kSecRva);               // VirtualAddress
    put32(img, st + 16, quint32(sec.size()));   // SizeOfRawData
    put32(img, st + 20, kSecRaw);               // PointerToRawData

    return img + sec;
}

// -- the real headers, as measured ----------------------------------------

QByteArray dbHeader(quint32 format, quint32 major, quint32 minor,
                    quint32 build, quint32 sub = 0)
{
    QByteArray h(24, '\0');
    put32(h, 0,  format);
    put32(h, 4,  major);
    put32(h, 8,  minor);
    put32(h, 12, build);
    put32(h, 16, sub);
    put32(h, 20, 12);          // module name length, whatever follows
    return h;
}

QDateTime day(int y, int m, int d)
{
    return QDateTime(QDate(y, m, d), QTime(12, 0)).toUTC();
}

// -- pe_info ---------------------------------------------------------------

void testReadsAVersionResource()
{
    std::cout << "\n[pe_info: what an image says about itself]\n";

    PeSpec spec;
    spec.stamp    = quint32(day(2026, 8, 6).toSecsSinceEpoch());
    spec.verMajor = 1; spec.verMinor = 7; spec.verBuild = 99; spec.verSub = 0;
    const pe_info::Info info = pe_info::read(buildPe(spec));

    check("the file version is read out of the resource",
          info.fileVersion.toString() == QLatin1String("1.7.99.0"),
          info.fileVersion.toString());
    check("and the three parts a runtime is named by",
          info.fileVersion.shortString() == QLatin1String("1.7.99"));
    check("the linker stamp becomes a date",
          info.built.date() == QDate(2026, 8, 6),
          info.built.toString(Qt::ISODate));
    check("an image with no export table has no plugin record",
          !info.skse.valid);
}

void testReadsAPluginRecord()
{
    std::cout << "\n[pe_info: the SKSE plugin record]\n";

    PeSpec spec;
    spec.stamp        = quint32(day(2026, 2, 26).toSecsSinceEpoch());
    spec.verMajor     = 7; spec.verBuild = 20;
    spec.withExport   = true;
    spec.skseName     = QStringLiteral("EngineFixes");
    spec.skseAuthor   = QStringLiteral("aers");
    spec.independence = 0x5;
    spec.compatible   = { 0x01064920 };      // MAKE_EXE_VERSION(1, 6, 1170)
    const pe_info::Info info = pe_info::read(buildPe(spec));

    check("the plugin names itself", info.skse.valid
          && info.skse.name == QLatin1String("EngineFixes"), info.skse.name);
    check("and its author", info.skse.author == QLatin1String("aers"));
    check("the independence flags come through",
          info.skse.independence == 0x5);
    check("so we know it needs the address library",
          info.skse.usesAddressLibrary());
    check("the runtime it declares is decoded",
          info.skse.compatibleVersions.size() == 1
              && info.skse.compatibleVersions.first().shortString()
                     == QLatin1String("1.6.1170"),
          info.skse.compatibleVersions.isEmpty()
              ? QStringLiteral("(none)")
              : info.skse.compatibleVersions.first().shortString());

    // The one plugin on that profile that reads none of this.
    PeSpec sigs = spec;
    sigs.skseName     = QStringLiteral("PrivateProfileRedirector");
    sigs.independence = 0x2;
    const auto sigInfo = pe_info::read(buildPe(sigs));
    check("a signature-scanning plugin says so",
          sigInfo.skse.usesSignatureScanning()
              && !sigInfo.skse.usesAddressLibrary());
}

void testMalformedImagesAreAnswerNotFault()
{
    std::cout << "\n[pe_info: images that are not images]\n";

    check("empty",   !pe_info::read(QByteArray()).fileVersion.valid);
    check("garbage", !pe_info::read(QByteArray(512, '\xcd')).fileVersion.valid);

    PeSpec spec;
    spec.verMajor = 1; spec.verMinor = 7; spec.verBuild = 99;
    const QByteArray full = buildPe(spec);
    // Cut it everywhere and make sure nothing reads past the buffer. Only the
    // whole image is expected to parse.
    bool survived = true;
    for (int cut = 1; cut < full.size(); cut += 7)
        if (pe_info::read(full.left(cut)).fileVersion.toString()
                == QLatin1String("9.9.9.9"))
            survived = false;
    check("every truncation of a real image is handled", survived);
    check("a plain MZ stub with no PE header is not a PE",
          !pe_info::read(QByteArray("MZ") + QByteArray(200, '\0')).built.isValid());

    // A stamp of zero, or one from before this all existed, is not a date.
    PeSpec noStamp = spec;
    noStamp.stamp = 0;
    check("no linker stamp means no build date",
          !pe_info::read(buildPe(noStamp)).built.isValid());
    PeSpec absurd = spec;
    absurd.stamp = 5;                     // 1970, i.e. a hash, not a time
    check("an implausible stamp is refused rather than believed",
          !pe_info::read(buildPe(absurd)).built.isValid());
}

void testLoaderNamesAndPackedVersions()
{
    std::cout << "\n[pe_info: names and packed versions]\n";

    check("the extender names the runtime it hooks",
          pe_info::runtimeFromLoaderName("skse64_1_7_99.dll").shortString()
              == QLatin1String("1.7.99"));
    check("older games too",
          pe_info::runtimeFromLoaderName("skse_1_9_32.dll").shortString()
              == QLatin1String("1.9.32"));
    check("a plain dll name is not a runtime",
          !pe_info::runtimeFromLoaderName("EngineFixes.dll").valid);
    check("SKSE's packed form decodes",
          pe_info::decodeSkseVersion(0x01064920).shortString()
              == QLatin1String("1.6.1170"),
          pe_info::decodeSkseVersion(0x01064920).shortString());
    check("zero is not a version", !pe_info::decodeSkseVersion(0).valid);
}

// -- skse_check ------------------------------------------------------------

void testAddressLibraryHeaders()
{
    std::cout << "\n[skse_check: the address library's own header]\n";

    const auto se = skse_check::parseAddressLibraryHeader(dbHeader(1, 1, 5, 97));
    check("format 1 is the Special Edition database",
          se.valid() && se.format == 1
              && se.runtime.shortString() == QLatin1String("1.5.97"));

    const auto ae = skse_check::parseAddressLibraryHeader(dbHeader(2, 1, 6, 1170));
    check("format 2 is the Anniversary Edition one",
          ae.valid() && ae.format == 2
              && ae.runtime.shortString() == QLatin1String("1.6.1170"));

    const auto now = skse_check::parseAddressLibraryHeader(
        dbHeader(5, 1, 7, 99), QStringLiteral("versionlib-1-7-99-0.bin"));
    check("and format 5 is the one that started this",
          now.valid() && now.format == 5
              && now.runtime.shortString() == QLatin1String("1.7.99"));
    check("the file name is carried for the report",
          now.file == QLatin1String("versionlib-1-7-99-0.bin"));

    check("a short read is not a header",
          !skse_check::parseAddressLibraryHeader(QByteArray(8, '\0')).valid());
    check("neither are zeroes",
          !skse_check::parseAddressLibraryHeader(QByteArray(24, '\0')).valid());
}

// The profile as it actually stood, with the real build dates.
skse_check::Facts reportedProfile()
{
    skse_check::Facts f;
    f.game.major = 1; f.game.minor = 7; f.game.build = 99; f.game.valid = true;
    f.gameBuilt     = day(2026, 8, 6);
    f.loaderFile    = QStringLiteral("skse64_1_7_99.dll");
    f.loaderRuntime = f.game;
    f.databases     = {
        skse_check::parseAddressLibraryHeader(dbHeader(1, 1, 5, 97)),
        skse_check::parseAddressLibraryHeader(dbHeader(2, 1, 6, 1170)),
        skse_check::parseAddressLibraryHeader(dbHeader(2, 1, 6, 1179)),
        skse_check::parseAddressLibraryHeader(dbHeader(5, 1, 7, 99)),
    };

    auto plugin = [](const char *file, const char *mod, QDateTime built,
                     quint32 indep, quint32 compatible) {
        skse_check::Plugin p;
        p.file  = QString::fromLatin1(file);
        p.mod   = QString::fromLatin1(mod);
        p.built = built;
        p.declared.valid        = true;
        p.declared.independence = indep;
        if (compatible)
            p.declared.compatibleVersions << pe_info::decodeSkseVersion(compatible);
        return p;
    };

    f.plugins = {
        plugin("EngineFixes.dll", "SSE Engine Fixes (skse64 plugin)",
               day(2026, 2, 26), 0x5, 0x01064920),
        plugin("ContainerDistributionFramework.dll", "Container Distribution Framework",
               day(2026, 5, 20), 0x5, 0),
        plugin("ItemStackingTweaks.dll", "Use or Take SKSE",
               day(2026, 3, 8), 0x1, 0x01000000),          // placeholder 1.0.0
        plugin("MergeMapper.dll", "MergeMapper",
               day(2023, 5, 12), 0x1, 0x01000000),
        plugin("po3_KeywordItemDistributor.dll", "Keyword Item Distributor (KID)",
               day(2026, 6, 30), 0x1, 0x01064920),
        plugin("po3_SpellPerkItemDistributor.dll", "Spell Perk Item Distributor (SPID)",
               day(2026, 7, 29), 0x5, 0x01064920),
        plugin("po3_Tweaks.dll", "powerofthree's Tweaks",
               day(2026, 7, 12), 0x5, 0x01064920),
        plugin("po3_UseOrTake.dll", "Use or Take SKSE",
               day(2025, 1, 27), 0x5, 0x01064460),
        plugin("PrivateProfileRedirector.dll", "PrivateProfileRedirector AE 0.6.2",
               day(2024, 10, 1), 0x2, 0x01064920),
    };
    return f;
}

void testTheReportedProfile()
{
    std::cout << "\n[skse_check: the profile that reported this]\n";
    const auto f = skse_check::evaluate(reportedProfile());

    check("SKSE itself is not blamed", !f.loaderMismatch);
    check("nor is the address library called missing", !f.missingDatabase);
    check("the format that stops the game is named", f.databaseFormat == 5,
          QString::number(f.databaseFormat));
    check("all eight address-library plugins are listed",
          f.stale.size() == 8, QString::number(f.stale.size()));

    bool namedTheSignatureScanner = false;
    for (const auto &s : f.stale)
        if (s.file == QLatin1String("PrivateProfileRedirector.dll"))
            namedTheSignatureScanner = true;
    // It scans for byte signatures and never opens the address library, so
    // blaming it would send somebody to update the one mod that is fine.
    check("the signature-scanning plugin is left out of it",
          !namedTheSignatureScanner);

    check("each row carries the mod that deployed it",
          !f.stale.isEmpty() && !f.stale.first().mod.isEmpty(),
          f.stale.isEmpty() ? QString() : f.stale.first().mod);
    check("rows are ordered by mod so the dialog is stable",
          f.stale.first().mod
              == QLatin1String("Container Distribution Framework"),
          f.stale.first().mod);

    // The author's declared runtime, where it is a real one.
    const skse_check::Stale *fixes = nullptr;
    for (const auto &s : f.stale)
        if (s.file == QLatin1String("EngineFixes.dll")) fixes = &s;
    check("a plugin declaring a real runtime says which",
          fixes && fixes->declaredFor.shortString() == QLatin1String("1.6.1170"),
          fixes ? fixes->declaredFor.shortString() : QStringLiteral("(absent)"));

    const skse_check::Stale *merge = nullptr;
    for (const auto &s : f.stale)
        if (s.file == QLatin1String("MergeMapper.dll")) merge = &s;
    // MergeMapper parks 1.0.0 in that field once its independence flag makes
    // the list advisory. No Skyrim was ever 1.0.0, so it is not quoted.
    check("a placeholder runtime is not quoted as one",
          merge && !merge->declaredFor.valid);
}

void testWhatMustNotFire()
{
    std::cout << "\n[skse_check: when there is nothing to say]\n";

    {   // The same plugins against the game they were built for.
        auto f = reportedProfile();
        f.game.build = 1179;
        f.gameBuilt  = day(2024, 2, 14);
        check("a format 2 database says nothing about old plugins",
              skse_check::evaluate(f).stale.isEmpty());
    }
    {   // Plugins newer than the game.
        auto f = reportedProfile();
        for (auto &p : f.plugins) p.built = day(2026, 9, 1);
        check("plugins built after the game are not stale",
              skse_check::evaluate(f).stale.isEmpty());
    }
    {   // No version for the game is no verdict about anything.
        auto f = reportedProfile();
        f.game = pe_info::Version{};
        check("an unreadable game version produces no findings",
              skse_check::evaluate(f).empty());
    }
    {   // A build stamp that is not a date must not put mods on a list.
        auto f = reportedProfile();
        f.gameBuilt      = QDateTime();
        f.newestDatabase = QDateTime();
        check("with no date to compare, nothing is claimed",
              skse_check::evaluate(f).stale.isEmpty());
        f.newestDatabase = day(2026, 8, 20);
        check("the database's own date stands in for a missing one",
              skse_check::evaluate(f).stale.size() == 8);
    }
    {   // Nothing here uses the address library at all.
        auto f = reportedProfile();
        for (auto &p : f.plugins) p.declared.independence = 0x2;
        f.databases.removeLast();
        const auto out = skse_check::evaluate(f);
        check("a profile of signature scanners is not missing a database",
              !out.missingDatabase && out.stale.isEmpty());
    }
    {   // A plugin with no version record says nothing, and silence is not
        // evidence.
        auto f = reportedProfile();
        for (auto &p : f.plugins) p.declared = pe_info::SksePlugin{};
        check("plugins with no record are left alone",
              skse_check::evaluate(f).empty());
    }
}

void testTheOtherTwoFindings()
{
    std::cout << "\n[skse_check: the extender and the missing database]\n";

    {
        auto f = reportedProfile();
        f.loaderRuntime = pe_info::decodeSkseVersion(0x01064920);   // 1.6.1170
        const auto out = skse_check::evaluate(f);
        check("an extender for another runtime is reported",
              out.loaderMismatch
                  && out.loaderRuntime.shortString() == QLatin1String("1.6.1170"));
    }
    {
        auto f = reportedProfile();
        f.databases.removeLast();          // drop the 1.7.99 one
        const auto out = skse_check::evaluate(f);
        check("no database for this game version is its own finding",
              out.missingDatabase);
        check("and it does not also list every plugin",
              out.stale.isEmpty());
    }
    {
        auto f = reportedProfile();
        f.databases.clear();
        f.plugins.clear();
        check("a game with no script extender plugins is quiet",
              skse_check::evaluate(f).empty());
    }
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    testReadsAVersionResource();
    testReadsAPluginRecord();
    testMalformedImagesAreAnswerNotFault();
    testLoaderNamesAndPackedVersions();
    testAddressLibraryHeaders();
    testTheReportedProfile();
    testWhatMustNotFire();
    testTheOtherTwoFindings();

    std::cout << "\n" << s_passed << " passed, " << s_failed << " failed\n";
    return s_failed == 0 ? 0 : 1;
}
