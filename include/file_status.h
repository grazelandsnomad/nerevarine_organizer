#pragma once

// file_status - is the file a row was installed from still what its Nexus page
// offers?
//
// Check Updates only compared the page's last update with the install date, so
// a file archived or replaced on its page read as up to date for as long as the
// page itself stayed quiet. Tamriel Data (HD) 25.05 was installed four days
// after its page moved HD to a page of its own; it stayed "current" for a month,
// until a Tamriel Rebuilt update needed the newer Lua in the moved file and the
// game started warning at the main menu. This answers from the page's file list
// instead: what Nexus says now about THAT file.
//
// Qt Core only.

#include <QList>
#include <QString>

namespace file_status {

// One file on a mod page, as the v2 modFiles query lists it.
struct PageFile {
    qint64  fileId = 0;
    QString name;
    QString version;
    // Nexus's own word: MAIN, UPDATE, OPTIONAL, MISCELLANEOUS, OLD_VERSION,
    // ARCHIVED, REMOVED.
    QString category;
    // Earlier uploads this one replaces, from the v1 list's "file_updates"
    // chain (NexusClient::FileEntry::replaces). The v2 query has no chain, so
    // it is empty there.
    QList<qint64> replaces;
};

struct Verdict {
    enum class State { Current, Superseded };
    State state = State::Current;

    // The installed file as the page lists it. `installedCategory` is
    // "UNLISTED" (and the name empty) when the page no longer lists it at all.
    QString installedName;
    QString installedVersion;
    QString installedCategory;

    // What the page offers now: its MAIN files, or its UPDATE files when it
    // has no MAIN one.
    QList<PageFile> current;

    // The current file carrying the installed one's name - the plain "newer
    // version of the same file" case. fileId 0 when there is none: the page
    // stopped offering a file by that name, as Tamriel Data (HD) did when HD
    // moved to its own page.
    PageFile replacement;
};

// Superseded when the page lists the installed file as OLD_VERSION, ARCHIVED or
// REMOVED, or no longer lists it. An empty list is never a verdict - a page
// that answered nothing says nothing - so it is Current, and a bad reply cannot
// raise a flag.
Verdict judge(qint64 installedFileId, const QList<PageFile> &pageFiles);

// What an update without a picker (Update All, Review Updates) should
// download for a row installed from `installedFileId`.
//
// That path used to take the page's default file whatever the row had: a mod
// installed from the 4K file came back as the 2K one, and the optional patch
// of a mod came back as the mod itself, landing on top of the main file's own
// row. This answers only from what the page says about THE installed file,
// and when it says nothing, says so - the row is left for the picker, never
// given a guess.
//
// In order:
//   - installed id unknown (0): the fallback, when the caller says it is
//     safe (the page's only row in the list, and one download or a fact
//     choosing it); else NeedsPick
//   - a file whose `replaces` holds the installed id: that file (NeedsPick
//     when it is itself retired)
//   - the installed file not listed and nothing linking it: NeedsPick
//   - the newest listed file carrying the installed one's name (case,
//     spacing and underscores aside): that file. While the installed file is
//     still offered, only a newer upload in its own category counts
//   - the installed file retired, nothing of its name: NeedsPick (it may have
//     moved to a page of its own, as Tamriel Data's HD file did)
//   - the installed file optional/miscellaneous, or the page's default is
//     the installed file: AlreadyNewest - nothing newer for this file
//   - otherwise NeedsPick: the page now leads with a different main file.
//
// `fallbackFileId` is the file the picker would preselect.
struct UpdateTarget {
    enum class Kind { Install, AlreadyNewest, NeedsPick };
    Kind   kind = Kind::NeedsPick;
    qint64 fileId = 0;   // Install: the file to download
};
UpdateTarget updateTarget(const QList<PageFile> &pageFiles, qint64 installedFileId,
                          qint64 fallbackFileId, bool fallbackIsSafe);

} // namespace file_status
