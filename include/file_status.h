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

} // namespace file_status
