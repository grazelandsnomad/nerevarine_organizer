#include "game_runtime.h"

#include <QDir>
#include <QFile>

namespace game_runtime {

Probe probe(const QString &gameRoot, const QString &exeName,
            const QString &extenderPrefix)
{
    Probe out;
    if (gameRoot.isEmpty()) return out;
    const QDir root(gameRoot);

    if (!exeName.isEmpty()) {
        // Only the header end is needed, but the version resource lives late
        // in the image and its offset is not known in advance, so the file is
        // read whole. A game exe is tens of megabytes and this happens once
        // per profile, cached by the caller.
        QFile exe(root.filePath(exeName));
        if (exe.open(QIODevice::ReadOnly))
            out.game = pe_info::read(exe.readAll()).fileVersion;
    }

    if (extenderPrefix.isEmpty()) return out;
    // The loader DLL names the runtime it hooks - f4se_1_11_240.dll - and
    // pe_info::runtimeFromLoaderName already reads exactly that shape. The
    // file merely EXISTING is the presence answer; a name that does not parse
    // still counts as installed, just without a version.
    const QStringList hits = root.entryList(
        {extenderPrefix + QStringLiteral("_*.dll")}, QDir::Files, QDir::Name);
    for (const QString &f : hits) {
        const pe_info::Version v = pe_info::runtimeFromLoaderName(f);
        out.extenderPresent = true;
        if (v.valid) { out.extender = v; break; }
    }
    return out;
}

} // namespace game_runtime
