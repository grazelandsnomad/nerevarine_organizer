#include "vanilla_text.h"

#include "plugin_strings.h"

#include <QDir>
#include <QFileInfo>

namespace vanilla_text {

bool Table::load(const QString &dataFolder)
{
    m_map.clear();
    m_byKey.clear();
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

        // The whole-file walk plugin_strings does anyway. Only the GMST
        // settings and the display names are kept, so the large StringSet -
        // every book and every line of dialogue in the game - dies with this
        // scope rather than being carried around for the life of the dialog.
        const plugin_strings::StringSet set = plugin_strings::extract(path);

        // Both tiers. NPC_ and CREA names are SECONDARY (plugin_strings.h
        // calls them proper-noun heavy), so auxByKey is where Fargoth lives -
        // and it was being walked past.
        const QHash<QString, QString> *const tiers[] = { &set.byKey, &set.auxByKey };
        for (const QHash<QString, QString> *tier : tiers) {
            for (auto it = tier->cbegin(); it != tier->cend(); ++it) {
                // Later masters win, which is the load order the game uses.
                if (isDisplayNameKey(it.key())) m_byKey.insert(it.key(), it.value());
                const QString setting = settingOfKey(it.key());
                if (!setting.isEmpty()) m_map.insert(setting, it.value());
            }
        }
        if (i == 0) gotBase = true;
    }
    if (!gotBase) { m_map.clear(); m_byKey.clear(); }
    return gotBase;
}

bool Table::saysExactly(const QString &key, const QString &text) const
{
    const auto it = m_byKey.constFind(key);
    // constFind first: an unknown key must not compare equal to a null
    // QString, which is how an empty table would call every blank name a
    // match and answer "the base game said that" about a mod's own work.
    return it != m_byKey.constEnd() && it.value() == text;
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

bool isDisplayNameKey(const QString &key)
{
    // "TYPE:<editorid>:SUB:index", read from the right: the subrecord is the
    // second field from the end however many colons the editor id contains.
    return key.section(QLatin1Char(':'), -2, -2) == QLatin1String("FNAM");
}

} // namespace vanilla_text
