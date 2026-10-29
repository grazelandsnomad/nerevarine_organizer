// Bethesda.net Creations: the link, the game's catalog, the decision of what
// to take over, and the move into the mod library.
#include "creations.h"
#include "modroles.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <iostream>

#include "test_harness.h"

using namespace creations;

namespace {

const char *kFever = "https://creations.bethesda.net/en/fallout4/details/"
                     "dc957ebd-da4c-40ee-ac6e-9be933d553f5/FEVer_Dreams";
const char *kPaintJobId = "098ac89d-37fd-43cf-89dd-3aeba370dff0";

bool writeFile(const QString &path, const QByteArray &bytes = "x")
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(bytes);
    return true;
}

CatalogEntry entry(const QString &id, const QString &title, const QStringList &files,
                   const QString &version = QStringLiteral("1"))
{
    CatalogEntry e;
    e.key = QStringLiteral("CSV2_") + id;
    e.id = id; e.title = title; e.files = files; e.version = version;
    return e;
}

} // namespace

static void testParseUrl()
{
    std::cout << "\n[parseCreationUrl]\n";
    auto r = parseCreationUrl(QString::fromLatin1(kFever));
    check("the FEVer Dreams page parses", r.has_value());
    check("game slug", r && r->gameSlug == "fallout4");
    check("id", r && r->id == "dc957ebd-da4c-40ee-ac6e-9be933d553f5");
    check("slug", r && r->slug == "FEVer_Dreams");

    r = parseCreationUrl("https://creations.bethesda.net/es/skyrim/details/"
                         "098AC89D-37FD-43CF-89DD-3AEBA370DFF0");
    check("another language, no slug", r.has_value());
    check("uuid lower-cased", r && r->id == kPaintJobId);
    check("slug empty", r && r->slug.isEmpty());
    check("skyrim slug", r && r->gameSlug == "skyrim");

    r = parseCreationUrl(QStringLiteral("https://creations.bethesda.net/starfield/details/%1").arg(kPaintJobId));
    check("no language segment", r && r->gameSlug == "starfield");

    r = parseCreationUrl(QString::fromLatin1(kFever) + "?utm_source=x#top");
    check("query and fragment ignored", r && r->slug == "FEVer_Dreams");

    r = parseCreationUrl(QStringLiteral("HTTPS://WWW.Creations.Bethesda.NET/en/fallout4/details/%1/X").arg(kPaintJobId));
    check("host case and www. ignored", r && r->id == kPaintJobId);

    r = parseCreationUrl(QStringLiteral("creations.bethesda.net/en/fallout4/details/%1/Slug").arg(kPaintJobId));
    check("missing scheme tolerated", r && r->slug == "Slug");

    r = parseCreationUrl(QStringLiteral("  %1  ").arg(QString::fromLatin1(kFever)));
    check("surrounding whitespace trimmed", r.has_value());

    check("a Nexus page is not a Creation",
          !parseCreationUrl("https://www.nexusmods.com/fallout4/mods/108580"));
    check("bethesda.net itself is not",
          !parseCreationUrl("https://bethesda.net/en/dashboard"));
    check("the site's other paths are not",
          !parseCreationUrl("https://creations.bethesda.net/en/fallout4/all?author=x"));
    check("a non-uuid id is refused",
          !parseCreationUrl("https://creations.bethesda.net/en/fallout4/details/not-a-uuid/Slug"));
    check("a path without details is refused",
          !parseCreationUrl("https://creations.bethesda.net/en/fallout4/mods/1"));
    check("too deep a path is refused",
          !parseCreationUrl(QStringLiteral("https://creations.bethesda.net/a/b/fallout4/details/%1").arg(kPaintJobId)));
    check("empty is refused", !parseCreationUrl(QString()));
    check("ftp is refused",
          !parseCreationUrl(QStringLiteral("ftp://creations.bethesda.net/en/fallout4/details/%1").arg(kPaintJobId)));
}

static void testCanonicalAndTitle()
{
    std::cout << "\n[canonicalUrl / provisionalTitle / game slugs]\n";
    const auto r = parseCreationUrl(QString::fromLatin1(kFever));
    check("canonical is the en page", r && canonicalUrl(*r) == QString::fromLatin1(kFever));
    const auto back = r ? parseCreationUrl(canonicalUrl(*r)) : std::nullopt;
    check("canonical round-trips", back && back->id == r->id && back->slug == r->slug
                                      && back->gameSlug == r->gameSlug);
    CreationRef noSlug{"skyrim", kPaintJobId, {}};
    check("canonical without a slug",
          canonicalUrl(noSlug) == QStringLiteral("https://creations.bethesda.net/en/skyrim/details/%1").arg(kPaintJobId));

    check("slug as a name", r && provisionalTitle(*r) == "FEVer Dreams");
    check("percent-decoded, collapsed",
          provisionalTitle(CreationRef{"fallout4", "id", "Some%20Mod__v2"}) == "Some Mod v2");
    check("no slug: the id", provisionalTitle(noSlug) == kPaintJobId);

    check("skyrim covers both Skyrim profiles",
          profileIdsForSlug("skyrim") == QStringList{"skyrimspecialedition", "skyrimanniversaryedition"});
    check("fallout4, any case", profileIdsForSlug("Fallout4") == QStringList{"fallout4"});
    check("no profile for morrowind", profileIdsForSlug("morrowind").isEmpty());
    check("inverse for AE", slugForProfile("skyrimanniversaryedition") == "skyrim");
    check("inverse for starfield", slugForProfile("starfield") == "starfield");
    check("no site for morrowind", slugForProfile("morrowind").isEmpty());
}

static void testCatalog()
{
    std::cout << "\n[parseContentCatalog]\n";
    const QByteArray json = R"({
  "CSV2_098ac89d-37fd-43cf-89dd-3aeba370dff0" : {
    "AchievementSafe" : true,
    "Files" : [
      "ccgcafo4003-factionws03bos - Main.ba2",
      "ccgcafo4003-factionws03bos - Textures.ba2",
      "ccgcafo4003-factionws03bos.esl"
    ],
    "FilesSize" : 78032015,
    "Timestamp" : 1788982035,
    "Title" : "Weapon Paint Job - Brotherhood of Steel",
    "Version" : "1762391102.1.0.0"
  },
  "CSV2_19361ca7-8ae7-4f67-9671-39620c50a8b4" : {
    "AchievementSafe" : true,
    "Files" : [ "ccfsvfo4007-halloween.esl" ],
    "FilesSize" : 292217,
    "Timestamp" : 1788981957,
    "Title" : "Halloween Workshop Pack",
    "Version" : "1762391049.1.0.0"
  },
  "CSV2_aaaaaaaa-0000-0000-0000-000000000000" : { "Title" : "no files" },
  "not an object" : 3
})";
    const QList<CatalogEntry> got = parseContentCatalog(json);
    check("two entries with files", got.size() == 2, QString::number(got.size()));
    if (got.size() != 2) return;
    const CatalogEntry *paint = nullptr;
    for (const CatalogEntry &e : got) if (e.id == kPaintJobId) paint = &e;
    check("the id is the uuid after the prefix, lower-cased", paint != nullptr);
    if (!paint) return;
    check("key kept", paint->key == QStringLiteral("CSV2_%1").arg(kPaintJobId));
    check("three files, as spelled", paint->files.size() == 3
          && paint->files.first() == "ccgcafo4003-factionws03bos - Main.ba2");
    check("title", paint->title == "Weapon Paint Job - Brotherhood of Steel");
    check("version string", paint->version == "1762391102.1.0.0");
    check("size", paint->size == 78032015);
    check("timestamp", paint->timestamp == 1788982035);
    check("malformed json gives nothing", parseContentCatalog("{ nope").isEmpty());
    check("an array gives nothing", parseContentCatalog("[1,2]").isEmpty());

    check("candidates in probe order",
          catalogCandidates({"/p/AppData/Local/Fallout4", ""}, {"/p/Documents/My Games/Fallout4"})
              == QStringList{"/p/AppData/Local/Fallout4/ContentCatalog.txt",
                             "/p/Documents/My Games/Fallout4/ContentCatalog.txt"});
}

static void testCccAndDataRoot()
{
    std::cout << "\n[cccPlugins / dataRootFiles]\n";
    QTemporaryDir tmp;
    const QString root = tmp.path() + "/Fallout 4";
    writeFile(root + "/Fallout4.ccc", "ccBGSFO4001-PipBoy(Black).esl\r\nccOTMFO4001-Remnants.esl\r\n\r\nccVLTFO4001-Homes.esm\n");
    writeFile(root + "/Fallout4IDs.ccc", "2401320\n2401321\n");
    writeFile(root + "/readme.txt", "ccfake.esl\n");
    const QSet<QString> cc = cccPlugins(root);
    check("three plugins, lower-cased", cc.size() == 3 && cc.contains("ccbgsfo4001-pipboy(black).esl")
          && cc.contains("ccvltfo4001-homes.esm"));
    check("numbers and other files ignored", !cc.contains("2401320") && !cc.contains("ccfake.esl"));
    check("no root: none", cccPlugins(QString()).isEmpty());

    const QString data = root + "/Data";
    writeFile(data + "/Fallout4.esm");
    writeFile(data + "/ccgcafo4003-factionws03bos.esl");
    writeFile(data + "/textures/nested.dds");
    QFile::link(data + "/Fallout4.esm", data + "/link.esl");
    const QHash<QString, DataFile> files = dataRootFiles(data);
    check("root files only, keyed lower-cased", files.size() == 3 && files.contains("fallout4.esm")
          && !files.contains("nested.dds"));
    check("spelling kept", files.value("fallout4.esm").spelling == "Fallout4.esm");
    check("a regular file is not a link", !files.value("fallout4.esm").symlink);
    check("a symlink is flagged", files.value("link.esl").symlink);
}

static void testPlan()
{
    std::cout << "\n[planAdoption]\n";
    QHash<QString, DataFile> data;
    auto present = [&](const QString &name, bool link = false) { data.insert(name.toLower(), DataFile{name, link}); };
    present("a.esl"); present("a - main.ba2");
    present("cc1.esl");
    present("c.esl");                      // c - main.ba2 missing
    present("d.esl");
    present("e.esl"); present("e - main.ba2");
    present("f.esl");
    present("g.esl", true);
    present("h.esl");
    present("i.esl");
    // j.esl missing

    const QList<CatalogEntry> catalog = {
        entry("a", "A waits", {"A.esl", "A - Main.ba2"}),
        entry("b", "B is Creation Club", {"cc1.esl"}),
        entry("c", "C half there", {"c.esl", "c - main.ba2"}),
        entry("d", "D clashes", {"d.esl"}),
        entry("e", "E re-downloaded", {"e.esl", "e - main.ba2"}, "2"),
        entry("f", "F ours", {"f.esl"}, "1"),
        entry("g", "G linked", {"g.esl"}, "2"),
        entry("h", "H ignored", {"h.esl"}),
        entry("i", "I unrequested", {"i.esl"}),
        entry("j", "J not downloaded", {"j.esl"}),
    };
    QHash<QString, RowInfo> rows;
    rows.insert("a", RowInfo{QUuid::createUuid(), false, {}, {}, "A waits"});
    rows.insert("c", RowInfo{QUuid::createUuid(), false, {}, {}, "C half there"});
    rows.insert("d", RowInfo{QUuid::createUuid(), false, {}, {}, "D clashes"});
    rows.insert("e", RowInfo{QUuid::createUuid(), true, "1", "/mods/E", "E re-downloaded"});
    rows.insert("f", RowInfo{QUuid::createUuid(), true, "1", "/mods/F", "F ours"});
    rows.insert("g", RowInfo{QUuid::createUuid(), true, "1", "/mods/G", "G linked"});
    QHash<QString, QString> deployed;
    deployed.insert("d.esl", "Some Nexus mod");
    deployed.insert("e.esl", "E re-downloaded");
    deployed.insert("e - main.ba2", "E re-downloaded");
    deployed.insert("f.esl", "F ours");
    deployed.insert("g.esl", "G linked");
    const QSet<QString> ignored = {"h"};

    const Plan plan = planAdoption(catalog, rows, data, {"cc1.esl"}, deployed, ignored);
    auto adoptOf = [&](const QString &id) -> const Adoption * {
        for (const Adoption &a : plan.adopt) if (a.entry.id == id) return &a;
        return nullptr;
    };
    check("a waiting row whose files are all there is adopted",
          adoptOf("a") && adoptOf("a")->rowId == rows.value("a").rowId && !adoptOf("a")->reinstall);
    check("case differences do not matter", adoptOf("a") != nullptr);
    check("Creation Club is the game's", !adoptOf("b") && plan.unrequested.isEmpty() ? true
          : std::none_of(plan.unrequested.cbegin(), plan.unrequested.cend(),
                         [](const CatalogEntry &e) { return e.id == "b"; }));
    check("a file still missing: not yet",
          plan.notYet.size() == 1 && plan.notYet.first().entry.id == "c");
    check("a name another mod deploys is skipped, with the culprit",
          plan.skipped.size() == 1 && plan.skipped.first().title == "D clashes"
          && plan.skipped.first().file == "d.esl" && plan.skipped.first().label == "Some Nexus mod");
    check("a re-download (new version, regular files) is adopted again",
          adoptOf("e") && adoptOf("e")->reinstall && adoptOf("e")->rowId == rows.value("e").rowId);
    check("our own deployed files, same version: nothing", !adoptOf("f"));
    check("our own links, even with a new version: nothing", !adoptOf("g"));
    check("an ignored download stays out",
          std::none_of(plan.unrequested.cbegin(), plan.unrequested.cend(),
                       [](const CatalogEntry &e) { return e.id == "h"; }));
    check("a download with no row is unrequested",
          plan.unrequested.size() == 1 && plan.unrequested.first().id == "i");
    check("a catalog entry whose files are not there yet is nothing",
          !adoptOf("j") && plan.notYet.size() == 1);
    check("exactly the two adoptions", plan.adopt.size() == 2, QString::number(plan.adopt.size()));
}

static void testMove()
{
    std::cout << "\n[destFolderFor / moveCreationFiles]\n";
    QTemporaryDir tmp;
    const QString mods = tmp.path() + "/mods";
    QDir().mkpath(mods);
    check("folder from the title", destFolderFor(mods, "FEVer Dreams") == mods + "/FEVer Dreams");
    QDir().mkpath(mods + "/FEVer Dreams");
    check("taken: suffixed", destFolderFor(mods, "FEVer Dreams") == mods + "/FEVer Dreams_2");
    check("unsafe characters dropped", destFolderFor(mods, "a/b: c?") == mods + "/ab c");
    check("nothing left: a default", destFolderFor(mods, "///") == mods + "/Creation");

    const QString data = tmp.path() + "/Data";
    writeFile(data + "/fever.esl", "plugin");
    writeFile(data + "/fever - main.ba2", QByteArray(3 << 20, 'b'));   // 3 MiB: several reads
    writeFile(data + "/other.esl", "keep");
    qint64 lastDone = -1, lastTotal = -1;
    const QString dest = mods + "/FEVer";
    MoveResult r = moveCreationFiles(data, {"Fever.esl", "Fever - Main.ba2"}, dest,
                                     [&](qint64 d, qint64 t) { lastDone = d; lastTotal = t; });
    check("all moved", r.failed.isEmpty(), r.failed + ": " + r.error);
    check("Data's spelling kept", r.moved == QStringList{"fever.esl", "fever - main.ba2"});
    check("files in the library", QFileInfo(dest + "/fever.esl").size() == 6
          && QFileInfo(dest + "/fever - main.ba2").size() == (3 << 20));
    check("gone from Data", !QFile::exists(data + "/fever.esl") && !QFile::exists(data + "/fever - main.ba2"));
    check("other files untouched", QFile::exists(data + "/other.esl"));
    check("progress reached the total", lastTotal == (3 << 20) + 6 && lastDone == lastTotal);

    // A re-adoption overwrites what the folder holds.
    writeFile(data + "/fever.esl", "newer plugin");
    r = moveCreationFiles(data, {"fever.esl"}, dest);
    check("re-adoption replaces the old file",
          r.failed.isEmpty() && QFileInfo(dest + "/fever.esl").size() == 12);

    // A file the game has not written yet: nothing moves.
    writeFile(data + "/x.esl", "x");
    r = moveCreationFiles(data, {"x.esl", "missing.ba2"}, mods + "/X");
    check("a missing file fails by name", r.failed == "missing.ba2" && r.moved.isEmpty());
    check("nothing moved", QFile::exists(data + "/x.esl") && !QFileInfo::exists(mods + "/X"));

    // A link is ours (or someone's), not a download.
    QFile::link(data + "/other.esl", data + "/linked.esl");
    r = moveCreationFiles(data, {"linked.esl"}, mods + "/L");
    check("a symlink source is refused", r.failed == "linked.esl" && !QFileInfo::exists(mods + "/L"));
    check("the link stays", QFileInfo(data + "/linked.esl").isSymLink());

    // A folder that cannot be made.
    writeFile(mods + "/afile", "f");
    r = moveCreationFiles(data, {"x.esl"}, mods + "/afile/sub");
    check("an unmakeable folder fails without moving",
          !r.failed.isEmpty() && QFile::exists(data + "/x.esl"));
}

static void testRows()
{
    std::cout << "\n[waitingRow / applyAdoption]\n";
    const auto ref = parseCreationUrl(QString::fromLatin1(kFever));
    if (!ref) { check("ref parses", false); return; }
    ModEntry e = waitingRow(*ref, "tip");
    check("named from the slug", e.displayName == "FEVer Dreams" && e.customName == "FEVer Dreams");
    check("not installed, ticked", e.installStatus == int(InstallStatus::NotInstalled) && e.checked);
    check("the canonical page as its URL", e.nexusUrl == QString::fromLatin1(kFever));
    check("tooltip and date", e.toolTip == "tip" && e.dateAdded.isValid());
    check("no version yet", e.creationVersion.isEmpty());

    const CatalogEntry c = entry(ref->id, "FEVer Dreams (catalog)", {"fever.esl"}, "1762391102.1.0.0");
    applyAdoption(e, c, "/mods/FEVer Dreams", "ignored: has a url");
    check("installed at the folder",
          e.installStatus == int(InstallStatus::Installed) && e.modPath == "/mods/FEVer Dreams");
    check("named from the catalog", e.customName == "FEVer Dreams (catalog)"
          && e.displayName == "FEVer Dreams (catalog)");
    check("version recorded", e.creationVersion == "1762391102.1.0.0");
    check("url kept", e.nexusUrl == QString::fromLatin1(kFever));
    check("still ticked", e.checked);

    ModEntry fresh = ModEntry::named("x");
    applyAdoption(fresh, c, "/mods/F", "https://creations.bethesda.net/en/fallout4/details/x");
    check("a row born from the download gets the url",
          fresh.nexusUrl == "https://creations.bethesda.net/en/fallout4/details/x");
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    std::cout << "=== Nerevarine Organizer - Creations tests ===\n";
    testParseUrl();
    testCanonicalAndTitle();
    testCatalog();
    testCccAndDataRoot();
    testPlan();
    testMove();
    testRows();
    std::cout << "\n" << s_passed << " passed, " << s_failed << " failed\n";
    return s_failed == 0 ? 0 : 1;
}
