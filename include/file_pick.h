#ifndef FILE_PICK_H
#define FILE_PICK_H

// file_pick - say what each file on a Nexus mod page actually is, so the
// download picker stops being a list of filenames to guess between.
//
// Rafael's Shader Pack offers three files and no explanation:
//
//   Rafael's Shader Pack 2.0e                       [v2.0e]  MAIN    22.4 MB
//   Enhanced PBR Lighting for OpenMW 0.49-0.52      [v2.0e]  MAIN     2.3 MB
//   Patch For Enhanced PBR Lighting For OpenMW 0.52 [v2.0e]  UPDATE   0.0 MB
//
// Nothing on screen says that the first is the mod, the second is a separate
// thing the page also hosts, or that the third is a patch which installs a
// fraction of a mod on its own. Worse, the picker pre-selected the second,
// because the engine scorer saw "OpenMW" in its name and nothing in the base
// file's - a name token outvoting the page's own main download.
//
// What the page actually tells us, and what each part is worth:
//
//   category_name  Nexus's own taxonomy, and the only field here with a
//                  defined meaning. MAIN is a complete download; UPDATE is
//                  explicitly a patch over one. Not a guess - it is what the
//                  author ticked when uploading.
//   is_primary     The one file the mod page's own download button hands
//                  you. The author's answer to "which one do I want?", and a
//                  far better default than anything inferred from a name.
//   description    The author's words about this specific file. Usually the
//                  whole answer, when it is filled in at all.
//   size           A tiebreaker at most. Never a claim: an add-on can be
//                  bigger than the mod it adds to.
//
// Everything below is pure: file metadata in, sentences and an index out, so
// the wording and the ranking are testable without a network or a dialog.

#include <QList>
#include <QString>

namespace file_pick {

struct FileInfo {
    QString name;
    QString version;
    QString category;        // Nexus category_name: MAIN / UPDATE / OPTIONAL / ...
    QString description;     // the author's own words; often empty
    qint64  sizeBytes = 0;
    bool    isPrimary = false;
};

// What a file is. Base and AddOn are only ever assigned when the page marks a
// primary file, because without that flag there is nothing to be the "add-on"
// to - two MAIN files are then simply two MAIN files.
enum class Kind {
    Base,       // the page's own main download
    AddOn,      // another complete download alongside it
    Main,       // a complete download, on a page that names no primary
    Patch,      // UPDATE: goes on top of another file
    Optional,
    Old,
    Other,
};

struct Note {
    Kind    kind   = Kind::Other;
    // The file this one goes on top of, or -1. An UPDATE whose name quotes a
    // MAIN file's name belongs to that file specifically, which is the
    // difference between "this is a patch" and "this is a patch for X".
    int     goesOn = -1;
    // The file name the explanation names: the page's main download for an
    // AddOn, the patched file for a Patch. Empty when the wording takes no
    // name. The sentences themselves live in the dialog, as literal T() keys
    // - a key assembled at runtime is invisible to the translation parity
    // check and would be reported dead forever.
    QString detailArg;
};

// A file description as it can be shown in a dialog.
//
// Nexus stores these as the author typed them into a web form, so they arrive
// with <br /> line breaks and HTML entities in them. Printed raw into a
// QLabel they come out as "Requires the main file.&lt;br /&gt;" or, worse, get
// interpreted as rich text and swallow whatever the author wrapped in angle
// brackets. Tags become spaces, entities are decoded, runs of whitespace
// collapse, and anything past `maxChars` is cut at a word boundary with an
// ellipsis - a picker panel is not the place for a 4 KB changelog.
QString plainDescription(const QString &raw, int maxChars = 400);

// Classify and word every file on the page. Index-parallel with `files`.
QList<Note> describe(const QList<FileInfo> &files);

// The row to select when the dialog opens, or 0 for a list that gives no
// reason to prefer any.
//
// `engineScores` is the caller's per-file view of engine fit (higher is
// better, negative meaning "built for the wrong engine"), kept outside this
// module so it stays free of game-profile knowledge. Pass an empty list for
// no opinion.
//
// A patch is never the default: it is not a whole mod, and defaulting to one
// installs a fragment. The page's primary file wins unless its engine score
// is negative, since an author's main download still loses to "this build is
// for MWSE and the profile is OpenMW".
int defaultIndex(const QList<FileInfo> &files, const QList<Note> &notes,
                 const QList<int> &engineScores);

} // namespace file_pick

#endif // FILE_PICK_H
