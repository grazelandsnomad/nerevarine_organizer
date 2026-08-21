#ifndef PE_INFO_H
#define PE_INFO_H

// pe_info - the few facts a Windows .exe or .dll states about itself.
//
// A mod manager on Linux still has to reason about Windows binaries: the game
// executable knows which version of the game it is, and every SKSE plugin
// carries a record saying what it was built for. Both are readable without
// running anything, and both are the difference between "your game will not
// start" and finding out from the game.
//
// Three things come out of a PE image:
//
//   FileVersion   From the VS_FIXEDFILEINFO in the version resource, which is
//                 what Windows shows in a file's Properties. SkyrimSE.exe
//                 reports 1.7.99.0 there and nothing else on disk says so.
//   Build time    The COFF header's TimeDateStamp, written by the linker. The
//                 only date about a binary that survives copying, extraction
//                 and hardlinking - a file mtime says when it was unpacked.
//   SKSEPlugin_Version
//                 A data export every modern SKSE plugin carries: its name,
//                 its author, the runtime versions it was built against, and
//                 whether it finds its addresses through the address library
//                 or by scanning for byte signatures. That last flag decides
//                 whether an address-library problem is this plugin's problem
//                 at all.
//
// Pure: an image goes in, values come out. It parses files written by other
// people, so every offset is checked against the buffer and anything
// malformed comes back empty rather than throwing or reading past the end.

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QString>

namespace pe_info {

struct Version {
    int  major = 0, minor = 0, build = 0, sub = 0;
    bool valid = false;

    QString toString() const;                    // "1.7.99.0"
    QString shortString() const;                 // "1.7.99"
    bool operator==(const Version &o) const {
        return valid && o.valid && major == o.major && minor == o.minor
            && build == o.build && sub == o.sub;
    }
    // Compares the three parts a game runtime is named by. Skyrim's fourth
    // field is always 0 and SKSE's file names carry only three.
    bool sameRuntime(const Version &o) const {
        return valid && o.valid && major == o.major && minor == o.minor
            && build == o.build;
    }
};

// The SKSEPluginVersionData record, as SKSE defines it.
struct SksePlugin {
    QString        name;
    QString        author;
    quint32        pluginVersion = 0;
    // Bit 0: finds addresses through the Address Library. Bit 1: scans for
    // byte signatures instead. Bit 2: built against post-1.6.629 structures.
    quint32        independence  = 0;
    // Runtimes the author declared. SKSE itself ignores these when any
    // independence bit is set, so a plugin can load on a runtime it does not
    // list - but the list is still the author saying what they built for.
    QList<Version> compatibleVersions;
    bool           valid = false;

    bool usesAddressLibrary()  const { return (independence & 0x1) != 0; }
    bool usesSignatureScanning() const { return (independence & 0x2) != 0; }
};

struct Info {
    Version    fileVersion;
    QDateTime  built;        // UTC; invalid when the stamp is absent or absurd
    SksePlugin skse;
};

// Read what the image states about itself. An unreadable, truncated or
// non-PE buffer yields a default Info, never a crash.
Info read(const QByteArray &image);

// SKSE packs a runtime version into one word as
// (major << 24) | (minor << 16) | (build << 4). Exposed because SKSE plugin
// records and SKSE's own naming both use it.
Version decodeSkseVersion(quint32 packed);

// The runtime out of a script-extender loader's file name:
// "skse64_1_7_99.dll" -> 1.7.99. Empty for anything else, since the loader
// naming its own runtime is the point.
Version runtimeFromLoaderName(const QString &fileName);

} // namespace pe_info

#endif // PE_INFO_H
