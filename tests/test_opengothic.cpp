// OpenGothic (Gothic II) engine facts: what makes a Gothic II install, where
// the engine is, how an archive's priority is written, and what has to be in
// the generated mod ini.
//
// Every one of these fails silently in the game when it is wrong - an unlisted
// .mod is dropped with no error, a stale header stamp loses a conflict without
// a word - so they are pinned here against the engine source they were read
// from (game/commandline.cpp, game/gothic.cpp, game/resources.cpp,
// lib/ZenKit/src/Vfs.cc).

#include "opengothic.h"
#include "game_adapter.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTimeZone>

#include <unistd.h>

#include <iostream>

#include "test_harness.h"

namespace {

// A minimal VDFS archive: 256 bytes of comment, the signature, counts, and the
// timestamp at offset 280. Enough for everything this module reads or writes.
QByteArray archiveBytes(quint32 stamp)
{
    QByteArray b(300, '\0');
    b.replace(256, 16, QByteArray("PSVDSC_V2.00\n\r\n\r", 16));
    for (int i = 0; i < 4; ++i)
        b[280 + i] = char((stamp >> (8 * i)) & 0xff);
    return b;
}

void writeFile(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (f.open(QIODevice::WriteOnly)) { f.write(bytes); f.close(); }
}

// A Gothic II root the way GOG unpacks one.
void makeGameRoot(const QString &root)
{
    QDir().mkpath(root + "/Data");
    QDir().mkpath(root + "/system");
    QDir().mkpath(root + "/_work/Data/Scripts/_compiled");
}

// -- the install ------------------------------------------------------

void testGameRootDetection()
{
    std::cout << "\n[what counts as a Gothic II install]\n";
    QTemporaryDir tmp;
    const QString root = tmp.filePath("Gothic 2 Gold");
    makeGameRoot(root);

    check("a full install validates", opengothic::isGameRoot(root));
    check("system/ alone is not a game root",
          !opengothic::isGameRoot(root + "/system"));
    check("Data/ alone is not a game root",
          !opengothic::isGameRoot(root + "/Data"));
    check("an empty folder is not a game root",
          !opengothic::isGameRoot(tmp.filePath("nothing")));
    check("nor is nothing at all", !opengothic::isGameRoot(QString()));

    // Half an install is the interesting failure: Data/ is there, the compiled
    // scripts are not, and the engine would refuse to start with "invalid
    // gothic path" rather than say what is missing.
    const QString half = tmp.filePath("half");
    QDir().mkpath(half + "/Data");
    QDir().mkpath(half + "/_work/Data");
    check("an install with no compiled scripts is rejected",
          !opengothic::isGameRoot(half));
}

void testGameRootIsCaseInsensitive()
{
    std::cout << "\n[a folder cased differently is still the install]\n";
    // The engine resolves each path segment case-insensitively
    // (FileUtil::caseInsensitiveSegment), so an install unpacked on Linux with
    // lowercase folders runs fine and has to be recognised here too.
    QTemporaryDir tmp;
    const QString root = tmp.filePath("gothic2");
    QDir().mkpath(root + "/data");
    QDir().mkpath(root + "/_WORK/data/scripts/_COMPILED");
    check("lowercase data/ and uppercase _WORK/ still validate",
          opengothic::isGameRoot(root));
}

void testFindsTheGameNearAWrongPick()
{
    std::cout << "\n[the game is found from wherever the user was looking]\n";
    // The mistake to expect: the folder picker gets pointed at the ENGINE,
    // because that is the thing just downloaded and what the profile is named
    // after. The game is normally a folder or two away from it.
    QTemporaryDir tmp;
    const QString disk = tmp.filePath("disk");
    QDir().mkpath(disk + "/OpenGothic/build/opengothic");
    makeGameRoot(disk + "/Games/Gothic 2 Gold");

    const auto fromDisk = opengothic::findGameRoots(disk);
    check("found two levels down", fromDisk.size() == 1
       && fromDisk.first().endsWith(QLatin1String("Gothic 2 Gold")),
          fromDisk.join('|'));
    check("a folder with no install under it finds nothing",
          opengothic::findGameRoots(disk + "/OpenGothic").isEmpty());
    check("depth is respected",
          opengothic::findGameRoots(disk, /*maxDepth=*/1).isEmpty());
    // The cap is what keeps a pick near the root of a big disk from turning
    // into a full-disk walk.
    check("the directory budget is honoured",
          opengothic::findGameRoots(disk, 2, /*maxDirs=*/1).isEmpty());
    check("a file is not scanned",
          opengothic::findGameRoots(disk + "/OpenGothic/build").isEmpty());
}

void testGameRootFromTheExePath()
{
    std::cout << "\n[the root is found from what the locators return]\n";
    QTemporaryDir tmp;
    const QString root = tmp.filePath("Gothic II");
    makeGameRoot(root);
    writeFile(root + "/system/Gothic2.exe", "MZ");

    // The storefront locators hand back <root>/system/Gothic2.exe; the folder
    // that matters is the one above it.
    check("climbs out of system/ to the root",
          opengothic::gameRootFor(root + "/system/Gothic2.exe")
              == QDir::cleanPath(root));
    check("a root passed in is returned as is",
          opengothic::gameRootFor(root) == QDir::cleanPath(root));
    check("somewhere else yields nothing",
          opengothic::gameRootFor(tmp.filePath("elsewhere")).isEmpty());
}

// -- archives ---------------------------------------------------------

void testDosStampMatchesARealArchive()
{
    std::cout << "\n[the header timestamp decodes like the game's own]\n";
    // Read out of Anims.vdf in a Gothic II Gold install: 0x2d65bbb3.
    const QDateTime t = opengothic::fromDosStamp(0x2d65bbb3u);
    check("Anims.vdf reads as 2002-11-05 23:29:38",
          t.date() == QDate(2002, 11, 5) && t.time() == QTime(23, 29, 38),
          t.toString(Qt::ISODate));
    check("and packs back to the same bytes",
          opengothic::toDosStamp(t) == 0x2d65bbb3u);

    // The addon's archives are dated later than the base game's, which is
    // exactly how Night of the Raven overrides it. If that comparison ever
    // stopped holding, load order here would mean nothing.
    check("the addon (2003-07) outranks the base game (2002-11)",
          0x2efc8e0eu > 0x2d65bbb3u);
}

void testStampsAreOrderedAndBeatTheBaseGame()
{
    std::cout << "\n[position in the list is what the stamp encodes]\n";
    check("later in the list gets a higher stamp",
          opengothic::stampForIndex(1) > opengothic::stampForIndex(0)
       && opengothic::stampForIndex(9) > opengothic::stampForIndex(8));
    check("even the first one beats the addon's archives",
          opengothic::stampForIndex(0) > 0x2efc8e0eu);
    // Two-second DOS resolution: adjacent positions must not collapse into one
    // value, or the two archives tie and mount order decides instead.
    check("adjacent positions stay distinct",
          opengothic::stampForIndex(0) != opengothic::stampForIndex(1));
}

void testArchiveHeaderRoundTrip()
{
    std::cout << "\n[reading and rewriting an archive header]\n";
    QTemporaryDir tmp;
    const QString vdf = tmp.filePath("Data/Textures.vdf");
    writeFile(vdf, archiveBytes(0x2d65bbb3u));

    check("recognised by signature", opengothic::isArchive(vdf));
    check("its stamp reads back", opengothic::readStamp(vdf) == 0x2d65bbb3);
    check("rewriting it sticks",
          opengothic::writeStamp(vdf, 0x40000000u)
       && opengothic::readStamp(vdf) == 0x40000000);

    const QString notAnArchive = tmp.filePath("Data/readme.vdf");
    writeFile(notAnArchive, QByteArray(400, 'x'));
    check("a file merely named .vdf is not an archive",
          !opengothic::isArchive(notAnArchive));
    check("and is never written to", !opengothic::writeStamp(notAnArchive, 1u));
    check("a missing file reads as -1", opengothic::readStamp(tmp.filePath("no.vdf")) == -1);

    // Only the four bytes of the stamp may change: the rest of the header is
    // the catalog the game needs to read anything at all.
    QFile f(vdf);
    const bool opened = f.open(QIODevice::ReadOnly);
    const QByteArray after = opened ? f.readAll() : QByteArray();
    f.close();
    QByteArray expect = archiveBytes(0x40000000u);
    check("nothing else in the file moved", after == expect,
          QString::number(after.size()), QString::number(expect.size()));
}

void testApplyOrderStampsInListOrder()
{
    std::cout << "\n[applyOrder writes the load order into the archives]\n";
    QTemporaryDir tmp;
    const QString a = tmp.filePath("Data/A.mod");
    const QString b = tmp.filePath("Data/B.mod");
    const QString c = tmp.filePath("Data/C.vdf");
    writeFile(a, archiveBytes(0x2d65bbb3u));
    writeFile(b, archiveBytes(0x2d65bbb3u));
    writeFile(c, archiveBytes(0x2d65bbb3u));

    const auto r = opengothic::applyOrder({a, b, c});
    check("all three stamped", r.stamped == 3 && r.errors.isEmpty(),
          QString::number(r.stamped) + " " + r.errors.join(';'));
    check("lower in the list wins",
          opengothic::readStamp(a) < opengothic::readStamp(b)
       && opengothic::readStamp(b) < opengothic::readStamp(c));

    // Deterministic by position, so a re-deploy of an unchanged list rewrites
    // nothing. A stamp taken from the clock would rewrite every archive every
    // time, and each rewrite has to copy the file to break its hardlink.
    const auto again = opengothic::applyOrder({a, b, c});
    check("re-running writes nothing", again.stamped == 0 && again.alreadyRight == 3,
          QString::number(again.stamped) + "/" + QString::number(again.alreadyRight));

    const auto bad = opengothic::applyOrder({tmp.filePath("Data/missing.mod")});
    check("a missing archive is reported, not skipped silently",
          bad.stamped == 0 && bad.errors.size() == 1);
}

void testStampingDoesNotEditTheModStore()
{
    std::cout << "\n[stamping never reaches back into the mod folder]\n";
    // Deploy hardlinks the game's copy to the one in the mod store, so both
    // names are the same bytes. Writing a header through one of them would
    // silently alter the user's stored archive.
    QTemporaryDir tmp;
    const QString stored   = tmp.filePath("mods/Karibik/Data/Karibik.mod");
    const QString deployed = tmp.filePath("game/Data/Karibik.mod");
    writeFile(stored, archiveBytes(0x2d65bbb3u));
    QDir().mkpath(QFileInfo(deployed).absolutePath());
    // ::link, not QFile::link: the latter makes a symlink on Unix, and the
    // shared-inode case is the whole point of this test.
    check("hard link created",
          ::link(QFile::encodeName(stored).constData(),
                 QFile::encodeName(deployed).constData()) == 0);   // <unistd.h>

    const auto r = opengothic::applyOrder({deployed});
    check("the deployed copy is stamped", r.stamped == 1, r.errors.join(';'));
    check("the stored archive is untouched",
          opengothic::readStamp(stored) == 0x2d65bbb3,
          QString::number(opengothic::readStamp(stored)));
    check("and the two are no longer the same file",
          opengothic::readStamp(deployed) != opengothic::readStamp(stored));
}

// -- mod inis ---------------------------------------------------------

void testParsesARealModIni()
{
    std::cout << "\n[reading a GothicStarter mod ini]\n";
    const QString text =
        "[INFO]\r\n"
        "Title=Karibik\r\n"
        "Description=An island\r\n"
        "\r\n"
        "[FILES]\r\n"
        "VDF=Karibik.mod Karibik_Speech.mod ; the archives\r\n"
        "GAME=KaribikGothic\r\n"
        "OUTPUTUNITS=OU_Karibik\r\n"
        "\r\n"
        "[SETTINGS]\r\n"
        "World=Karibik\\Karibik.zen\r\n"
        "PLAYER=PC_HERO\r\n";
    const auto ini = opengothic::parseModIni(text);

    check("both archives read",
          ini.vdf == QStringList({"Karibik.mod", "Karibik_Speech.mod"}),
          ini.vdf.join('|'));
    check("the trailing ; comment is not part of the value",
          !ini.vdf.join('|').contains(QLatin1Char(';')));
    check("GAME, without the .DAT the engine appends itself",
          ini.gameDat == QLatin1String("KaribikGothic"));
    check("OUTPUTUNITS", ini.outputUnits == QLatin1String("OU_Karibik"));
    // Key case varies from mod to mod ("World" here, "WORLD" elsewhere); the
    // engine compares case-insensitively and so must this.
    check("WORLD regardless of how it was cased",
          ini.world == QLatin1String("Karibik\\Karibik.zen"));
    check("PLAYER", ini.player == QLatin1String("PC_HERO"));
    check("Title, for reporting", ini.title == QLatin1String("Karibik"));
    check("an empty ini is empty", opengothic::parseModIni(QString()).isEmpty());
}

void testMergeIsAUnionWithLastWriterWinning()
{
    std::cout << "\n[merging what several enabled mods declare]\n";
    opengothic::ModIni a;
    a.vdf = {"Textures_HD.vdf"};
    a.player = QStringLiteral("PC_HERO");
    opengothic::ModIni b;
    b.vdf = {"Karibik.mod", "Textures_HD.vdf"};
    b.gameDat = QStringLiteral("KaribikGothic");
    b.world   = QStringLiteral("Karibik\\Karibik.zen");
    opengothic::ModIni c;
    c.world = QStringLiteral("Later\\Later.zen");

    // "Bare.mod" is what most Gothic mods really are: an archive with no ini
    // at all. Unlisted, the engine throws it away, so the merge has to add it.
    const auto m = opengothic::mergeModInis({a, b, c}, {"Karibik.mod", "Bare.mod"});

    check("every archive appears exactly once",
          m.vdf == QStringList({"Textures_HD.vdf", "Karibik.mod", "Bare.mod"}),
          m.vdf.join('|'));
    check("a total conversion's GAME survives",
          m.gameDat == QLatin1String("KaribikGothic"));
    check("the last mod to declare a world wins",
          m.world == QLatin1String("Later\\Later.zen"));
    check("a key only the first mod set is kept",
          m.player == QLatin1String("PC_HERO"));
}

void testBuiltIniIsReadableBack()
{
    std::cout << "\n[what gets written is what gets read]\n";
    opengothic::ModIni ini;
    ini.vdf     = {"Karibik.mod", "Textures_HD.vdf"};
    ini.gameDat = QStringLiteral("KaribikGothic");
    ini.world   = QStringLiteral("Karibik\\Karibik.zen");
    ini.player  = QStringLiteral("PC_HERO");

    const QString text = opengothic::buildModIni(ini);
    check("sections are spelled the way the engine looks them up",
          text.contains(QLatin1String("[FILES]")) && text.contains(QLatin1String("[SETTINGS]")));
    check("the archive list is space separated, as the engine splits it",
          text.contains(QLatin1String("VDF=Karibik.mod Textures_HD.vdf")));

    const auto back = opengothic::parseModIni(text);
    check("round trips", back.vdf == ini.vdf && back.gameDat == ini.gameDat
                      && back.world == ini.world && back.player == ini.player);

    // An empty VDF list is still a VDF key, and that is deliberate: the key's
    // presence is what turns the engine's filter on, and nothing to list means
    // nothing should load.
    check("an empty list still writes the key",
          opengothic::buildModIni({}).contains(QLatin1String("VDF=")));
}

void testModFolderShapes()
{
    std::cout << "\n[the two shapes a Gothic mod comes in]\n";
    QTemporaryDir tmp;

    // Packaged the game's way: copy it in unchanged.
    const QString packaged = tmp.filePath("Karibik");
    writeFile(packaged + "/Data/Karibik.mod", archiveBytes(0x2d65bbb3u));
    writeFile(packaged + "/system/Karibik.ini", "[FILES]\r\nVDF=Karibik.mod\r\n");
    const auto a = opengothic::mapModFolder(packaged);
    check("a mod with Data/ overlays as-is", a.overlay && a.archives.isEmpty());

    // Lowercase from a Windows archive is the same shape.
    const QString lower = tmp.filePath("Lower");
    writeFile(lower + "/data/thing.mod", archiveBytes(0x2d65bbb3u));
    check("data/ counts too", opengothic::mapModFolder(lower).overlay);

    // A bare archive: the shape half of Nexus ships. Nothing scans the game
    // root for archives, so this has to be routed into Data/.
    const QString bare = tmp.filePath("Bare");
    writeFile(bare + "/Karibik.mod", archiveBytes(0x2d65bbb3u));
    writeFile(bare + "/Karibik.ini", "[FILES]\r\nVDF=Karibik.mod\r\n");
    writeFile(bare + "/readme.txt", "hello");
    writeFile(bare + "/notes.vdf", QByteArray(400, 'x'));   // not an archive
    const auto b = opengothic::mapModFolder(bare);
    check("a bare archive is not an overlay", !b.overlay);
    check("the archive is routed to Data/",
          b.archives == QStringList({"Karibik.mod"}), b.archives.join('|'));
    check("its ini goes to system/", b.inis == QStringList({"Karibik.ini"}),
          b.inis.join('|'));
    check("a .vdf that is not an archive is left alone",
          !b.archives.contains(QLatin1String("notes.vdf")));

    // An ini with no archive is a settings snippet, not a mod: offering it as
    // a -game: target would start the engine on a mod that loads nothing.
    const QString iniOnly = tmp.filePath("IniOnly");
    writeFile(iniOnly + "/tweak.ini", "[SETTINGS]\r\n");
    const auto c = opengothic::mapModFolder(iniOnly);
    check("an ini on its own maps to nothing", c.isEmpty(), c.inis.join('|'));
    check("a missing folder maps to nothing",
          opengothic::mapModFolder(tmp.filePath("nope")).isEmpty());
}

void testPlanActivationReadsADeployment()
{
    std::cout << "\n[what a deployment turns into]\n";
    QTemporaryDir tmp;
    const QString root = tmp.filePath("Gothic II");
    makeGameRoot(root);

    writeFile(root + "/Data/Karibik.mod",   archiveBytes(0x2d65bbb3u));
    writeFile(root + "/Data/Textures.vdf",  archiveBytes(0x2d65bbb3u));
    writeFile(root + "/Data/With Space.mod", archiveBytes(0x2d65bbb3u));
    writeFile(root + "/Data/readme.vdf",    QByteArray(400, 'x'));   // not an archive
    writeFile(root + "/system/Karibik.ini",
              "[FILES]\r\nVDF=Karibik.mod\r\nGAME=KaribikGothic\r\n");
    writeFile(root + "/system/Nerevarine.ini",
              "[FILES]\r\nVDF=Stale.mod\r\n");                     // our own, from before
    writeFile(root + "/_work/Data/Textures/loose.tex", "x");

    const QStringList rels = {
        "Data/Karibik.mod", "Data/Textures.vdf", "Data/With Space.mod",
        "Data/readme.vdf", "system/Karibik.ini", "system/Nerevarine.ini",
        "_work/Data/Textures/loose.tex",
    };
    const auto plan = opengothic::planActivation(root, rels);

    check("archives are found in deploy order",
          plan.archiveNames == QStringList({"Karibik.mod", "Textures.vdf"}),
          plan.archiveNames.join('|'));
    check("paths are absolute, under the game root",
          plan.archivePaths.first() == QDir(root).filePath("Data/Karibik.mod"),
          plan.archivePaths.value(0));
    // A .vdf that is not an archive would be listed as one and then fail to
    // mount; the signature check is what keeps a readme out of the list.
    check("a file merely named .vdf is left out",
          !plan.archiveNames.contains(QLatin1String("readme.vdf")));
    check("a name with a space is reported as unusable",
          plan.unusable == QStringList({"With Space.mod"}), plan.unusable.join('|'));
    check("the mod's own ini is read", plan.modInis.size() == 1
       && plan.modInis.first().gameDat == QLatin1String("KaribikGothic"),
          QString::number(plan.modInis.size()));
    // Reading our own generated ini back in would carry last deploy's archive
    // list forward forever, including mods that have since been removed.
    check("our own generated ini is never read back",
          !plan.modInis.isEmpty() && !plan.modInis.first().vdf.contains(QLatin1String("Stale.mod")));

    check("a deployment with nothing to load plans nothing",
          opengothic::planActivation(root, {"_work/Data/Textures/loose.tex"}).isEmpty());
}

// -- launching --------------------------------------------------------

void testLaunchArgs()
{
    std::cout << "\n[the command line the engine is started with]\n";
    const auto plain = opengothic::launchArgs("/games/Gothic II");
    check("-g takes the root as its own argument",
          plain == QStringList({"-g", "/games/Gothic II"}), plain.join('|'));

    const auto modded = opengothic::launchArgs("/games/Gothic II", "Nerevarine.ini");
    // One token, no space: the engine matches the "-game:" prefix on a single
    // argument and takes the remainder as the file name.
    check("-game: is one token",
          modded.last() == QLatin1String("-game:Nerevarine.ini"), modded.join('|'));
    check("and the ini is a bare name, resolved under system/ by the engine",
          !modded.last().contains(QLatin1Char('/')));
}

// -- the adapter ------------------------------------------------------

void testAdapterWiring()
{
    std::cout << "\n[the Gothic II profile is wired for OpenGothic]\n";
    const GameAdapter *a = GameAdapterRegistry::find("gothic2");
    check("the profile exists", a != nullptr);
    if (!a) return;

    check("it is an OpenGothic profile", a->isOpenGothic());
    check("it is in the game menu", a->pinned());
    // The exe really is one level down, next to the mod inis. A root-level
    // spelling matched nothing and the game was never detected.
    check("the exe is system/Gothic2.exe",
          a->steamLayout().exe == QLatin1String("system/Gothic2.exe"));
    for (const auto &g : a->gogLayouts())
        check("every GOG layout agrees", g.exe == QLatin1String("system/Gothic2.exe"),
              g.folder + ": " + g.exe);
    // Deploy target is the folder ABOVE the exe: a mod is Data/ plus system/,
    // so it overlays the game root. Deploying into system/ would bury both.
    check("mods deploy into the game root", a->dataSubdir() == QLatin1String(".."));
    check("and are overlaid as packaged", a->overlayDeploy());
    check("no plugin list is written for it",
          a->loadOrderStyle() == LoadOrderStyle::Unknown);
    check("no launcher button", !a->hasLauncher());

    // Nothing else may claim to be OpenGothic, or the Gothic-only deploy step
    // would run against an engine that has never heard of a VDF list.
    int gothicProfiles = 0;
    for (const GameAdapter *other : GameAdapterRegistry::all())
        if (other->isOpenGothic()) ++gothicProfiles;
    check("exactly one profile is OpenGothic", gothicProfiles == 1,
          QString::number(gothicProfiles));
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    testGameRootDetection();
    testGameRootIsCaseInsensitive();
    testGameRootFromTheExePath();
    testFindsTheGameNearAWrongPick();
    testDosStampMatchesARealArchive();
    testStampsAreOrderedAndBeatTheBaseGame();
    testArchiveHeaderRoundTrip();
    testApplyOrderStampsInListOrder();
    testStampingDoesNotEditTheModStore();
    testParsesARealModIni();
    testMergeIsAUnionWithLastWriterWinning();
    testBuiltIniIsReadableBack();
    testModFolderShapes();
    testPlanActivationReadsADeployment();
    testLaunchArgs();
    testAdapterWiring();

    std::cout << "\n" << s_passed << " passed, " << s_failed << " failed\n";
    return s_failed == 0 ? 0 : 1;
}
