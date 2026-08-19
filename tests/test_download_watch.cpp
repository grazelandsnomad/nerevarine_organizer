// Catching a manual download.
//
// Nexus shows no "Mod Manager Download" button for Gothic 2 (no mod-manager
// integration for that game), so nothing ever arrives by nxm:// and the only
// route in is a file landing in the browser's download folder. Two things have
// to be right for that to be usable, and both are silent when wrong: offering a
// half-written file (a corrupt-archive error the user cannot explain), and
// getting the mod id out of the file name (without it a hand-installed mod
// stops being checked for updates).

#include "download_watch.h"

#include <QCoreApplication>
#include <QString>

#include <iostream>

#include "test_harness.h"

namespace {

void testPartialNames()
{
    std::cout << "\n[a download still being written is never offered]\n";
    // Chrome really does write these, in the user's own language: there were
    // four "No confirmat 416242.crdownload" files in the folder this was
    // built against.
    check("Chrome's temporary name",
          download_watch::isPartialName("No confirmat 416242.crdownload"));
    check("Firefox's", download_watch::isPartialName("SomeMod.zip.part"));
    check("and the other spellings",
          download_watch::isPartialName("x.partial")
       && download_watch::isPartialName("x.download")
       && download_watch::isPartialName("x.tmp"));
    check("a dotfile is never a mod", download_watch::isPartialName(".hidden.zip"));
    check("case does not matter", download_watch::isPartialName("MOD.CRDOWNLOAD"));

    check("a finished archive is not partial",
          !download_watch::isPartialName("Ultimate Texture Pack-135-1-0-1699999999.7z"));
    check("nor is a plain name", !download_watch::isPartialName("mod.zip"));
}

void testNexusModIdRecovery()
{
    std::cout << "\n[the mod id a manual download carries]\n";
    check("a Nexus file name yields its mod id",
          download_watch::nexusModIdFromFileName(
              "Ultimate Texture Pack-135-1-0-1699999999.7z") == 135);
    // The old parser took the second dash-separated field, so any mod whose
    // own name contains a hyphen lost its id and its update checks with it.
    check("a hyphenated mod name still yields it",
          download_watch::nexusModIdFromFileName(
              "Ultimate-Texture-Pack-135-1-0-1699999999.7z") == 135);
    // A deliberate limitation, not an oversight: only numeric version segments
    // are walked. Allowing words there would also match "Mod-2-Pack-135-...",
    // which starts at the wrong number and would attach the wrong mod page. A
    // "1-0-beta" version yields no id, so no page is attached and the only
    // cost is that this one mod is not update-checked.
    check("a version with a word in it yields nothing rather than a guess",
          download_watch::nexusModIdFromFileName(
              "Some Mod-4321-2-1-3-beta-1712345678.zip") == -1);
    check("a numeric-only version chain is fine",
          download_watch::nexusModIdFromFileName("Some Mod-4321-2-1-3-1712345678.zip") == 4321);

    // The timestamp is the anchor. Without it this is somebody else's naming
    // scheme and guessing would attach the wrong mod page to a row.
    check("no trailing timestamp, no guess",
          download_watch::nexusModIdFromFileName("Cool Mod v2-1-0.zip") == -1);
    check("a plain name yields nothing",
          download_watch::nexusModIdFromFileName("texture_pack.7z") == -1);
    check("a WoG style name yields nothing",
          download_watch::nexusModIdFromFileName("Karibik_1.2_full.rar") == -1);
    check("an empty name yields nothing",
          download_watch::nexusModIdFromFileName("") == -1);
}

void testSettleTracker()
{
    std::cout << "\n[a file is offered only once it stops growing]\n";
    download_watch::SettleTracker t;
    const QString f = QStringLiteral("/downloads/Mod-135-1-0-1699999999.7z");

    check("first sight is never ready", !t.observe(f, 1000));
    check("still growing is not ready",  !t.observe(f, 5000));
    check("two equal samples means done", t.observe(f, 5000));
    // Reported once: a directory rescan every 1.5s would otherwise raise the
    // same banner over and over.
    check("and it is not reported twice", !t.observe(f, 5000));

    const QString g = QStringLiteral("/downloads/Other.zip");
    check("a different file tracks separately", !t.observe(g, 10));
    check("and settles on its own", !t.observe(g, 20) && t.observe(g, 20));

    // "No" has to stick for the session, or the next rescan asks again.
    const QString h = QStringLiteral("/downloads/Declined.zip");
    t.observe(h, 1);
    t.forget(h);
    check("a declined file is never offered again", !t.observe(h, 1));

    t.clear();
    check("clearing forgets everything", !t.observe(f, 5000) && t.observe(f, 5000));
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    testPartialNames();
    testNexusModIdRecovery();
    testSettleTracker();

    std::cout << "\n" << s_passed << " passed, " << s_failed << " failed\n";
    return s_failed == 0 ? 0 : 1;
}
