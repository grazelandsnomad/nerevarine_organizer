#include "vanilla_gmst.h"

#include "plugin_strings.h"

#include <QDir>
#include <QFileInfo>

namespace vanilla_gmst {

bool Table::load(const QString &dataFolder)
{
    m_map.clear();
    if (dataFolder.isEmpty()) return false;

    const QDir dir(dataFolder);
    // Morrowind first and required: without it there is no base game to
    // compare against and a half-filled table is worse than none, because
    // every setting it happens to miss would read as "the mod changed this".
    static const char *const kMasters[] = {
        "Morrowind.esm", "Tribunal.esm", "Bloodmoon.esm",
    };

    bool gotBase = false;
    for (int i = 0; i < 3; ++i) {
        const QString path = dir.filePath(QString::fromLatin1(kMasters[i]));
        if (!QFileInfo::exists(path)) continue;

        // The whole-file walk plugin_strings does anyway. Only the GMST keys
        // are kept, so the large StringSet dies with this scope rather than
        // being carried around for the life of the dialog.
        const plugin_strings::StringSet set = plugin_strings::extract(path);
        for (auto it = set.byKey.cbegin(); it != set.byKey.cend(); ++it) {
            const QString setting = settingOfKey(it.key());
            // Later masters win, which is the load order the game uses.
            if (!setting.isEmpty()) m_map.insert(setting, it.value());
        }
        if (i == 0) gotBase = true;
    }
    if (!gotBase) m_map.clear();
    return gotBase;
}

QString Table::value(const QString &setting) const
{
    const auto it = m_map.constFind(setting);
    return it == m_map.constEnd() ? QString() : it.value();
}

bool isDirty(const QString &setting, const QString &value, const Table &vanilla)
{
    // Nothing to translate. Also the shape a mod leaves behind when it blanks
    // a message it did not want shown.
    if (value.trimmed().isEmpty()) return true;

    // The setting's own name as its value. Bethesda ships some of these as
    // placeholders, and an editor opened without the expansion that defines
    // them writes the rest. Prose never looks like this.
    if (value == setting) return true;

    // The game already says exactly this, so the mod re-saved it untouched.
    // contains() first: a setting the table does not know is not a match
    // against a null QString.
    if (vanilla.contains(setting) && vanilla.value(setting) == value) return true;

    return false;
}

bool holdsObjectId(const QString &setting)
{
    // Case-sensitive: the suffix is Bethesda's own convention, and a setting
    // ending in a lowercase "id" is not part of it.
    return setting.endsWith(QLatin1String("ID"), Qt::CaseSensitive);
}

QString settingOfKey(const QString &key)
{
    // "GMST:<setting>:STRV:<index>". Split rather than sliced so a setting
    // name is never cut short, and checked at both ends so a key of another
    // shape cannot answer.
    const QStringList parts = key.split(QLatin1Char(':'));
    if (parts.size() < 4) return {};
    if (parts[0] != QLatin1String("GMST")) return {};
    if (parts[2] != QLatin1String("STRV")) return {};
    return parts[1];
}

} // namespace vanilla_gmst
