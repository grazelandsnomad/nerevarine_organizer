#include "translation_mod.h"

#include <QDir>
#include <QFileInfo>

namespace translation_mod {

QString nameFor(const QString &sourceModName, const QString &language)
{
    // Title-case the language token for display: the rest of the app carries
    // it lowercased ("spanish"), but this string is a folder name a user reads.
    QString lang = language.trimmed();
    if (!lang.isEmpty()) lang[0] = lang[0].toUpper();

    return sourceModName.trimmed()
         + QStringLiteral(" - ") + lang
         + QStringLiteral(" (Nerevarine)");
}

Result build(const QString &sourceModPath,
             const QString &sourceModName,
             const QString &modsDir,
             const QString &language,
             const ByPlugin &replacements,
             const EncodingByPlugin &encodings)
{
    Result r;

    if (sourceModPath.isEmpty() || !QDir(sourceModPath).exists()) {
        r.error = QStringLiteral("source mod folder does not exist");
        return r;
    }
    if (modsDir.isEmpty()) {
        r.error = QStringLiteral("no mods directory configured");
        return r;
    }
    if (replacements.isEmpty()) {
        r.error = QStringLiteral("nothing to translate");
        return r;
    }

    r.modName = nameFor(sourceModName, language);
    const QString outRoot = QDir(modsDir).filePath(r.modName);
    if (!QDir().mkpath(outRoot)) {
        r.error = QStringLiteral("cannot create %1").arg(outRoot);
        return r;
    }

    for (auto it = replacements.cbegin(); it != replacements.cend(); ++it) {
        const QString rel = it.key();
        if (it.value().isEmpty()) continue;      // nothing typed for this plugin

        const QString src = QDir(sourceModPath).filePath(rel);
        if (!QFileInfo::exists(src)) {
            r.warnings << QStringLiteral("%1: no longer in the mod folder").arg(rel);
            continue;
        }

        // Mirror the source layout, so a plugin that lived under Data/ stays
        // under Data/ and the deploy step finds it where it expects.
        const QString dst = QDir(outRoot).filePath(rel);
        const QString dstDir = QFileInfo(dst).absolutePath();
        if (!QDir().mkpath(dstDir)) {
            r.warnings << QStringLiteral("%1: cannot create %2").arg(rel, dstDir);
            continue;
        }

        const auto w = plugin_writer::apply(
            src, dst, it.value(),
            encodings.value(rel, plugin_text::Encoding::Utf8));
        if (!w.ok) {
            r.warnings << QStringLiteral("%1: %2").arg(rel, w.error);
            continue;
        }
        // A key that matched nothing means the mod changed under us (an update
        // renumbered a FormID). Worth saying - the string is silently still
        // English otherwise.
        for (const QString &miss : w.missed)
            r.warnings << QStringLiteral("%1: %2 no longer exists in the plugin")
                              .arg(rel, miss);

        ++r.plugins;
        r.strings += w.applied;
    }

    if (r.plugins == 0) {
        // Nothing was produced, so leave no empty folder behind to confuse the
        // mod list. rmdir only removes it when it really is empty.
        QDir().rmdir(outRoot);
        r.error = r.warnings.isEmpty()
                    ? QStringLiteral("no plugin could be written")
                    : r.warnings.join(QStringLiteral("; "));
        return r;
    }

    r.modPath = outRoot;
    r.ok      = true;
    return r;
}

} // namespace translation_mod
