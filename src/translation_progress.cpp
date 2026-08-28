#include "translation_progress.h"
#include "translation_store.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace translation_progress {
namespace {

// Shared with the memory on purpose: the two files hold the same kind of key,
// and two normalisers would eventually disagree about a trailing space.
QString norm(const QString &s) { return translation_store::normalize(s); }

} // namespace

bool Progress::load(const QString &path)
{
    m_map.clear();

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return true;   // nothing saved yet

    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return false;

    const QJsonObject root = doc.object();
    m_mod      = root.value(QStringLiteral("mod")).toString();
    m_language = root.value(QStringLiteral("language")).toString();

    // Read the unreviewed list first, so the entries loop can consult it.
    // Absent from the list means reviewed: a file somebody edited by hand is
    // their own work, and defaulting the other way would quietly hold it back
    // from the memory forever.
    QSet<QString> unreviewed;
    for (const QJsonValue &v : root.value(QStringLiteral("unreviewed")).toArray())
        if (v.isString()) unreviewed.insert(norm(v.toString()));

    // Same shape as a memory file - {"entries": {"<source>": "<answer>"}} -
    // so one can be read as the other.
    const QJsonObject entries = root.value(QStringLiteral("entries")).toObject();
    for (auto it = entries.constBegin(); it != entries.constEnd(); ++it) {
        const QString translation = it.value().toString();
        if (translation.isEmpty()) continue;
        const QString key = norm(it.key());
        m_map.insert(key, Entry{translation, !unreviewed.contains(key)});
    }
    return true;
}

bool Progress::save(const QString &path) const
{
    QJsonObject entries;
    QJsonArray  unreviewed;
    for (auto it = m_map.cbegin(); it != m_map.cend(); ++it) {
        entries.insert(it.key(), it.value().translation);
        // The UNreviewed are listed, not the reviewed: in the ordinary
        // hand-translation case that array is empty, so the file stays small.
        if (!it.value().reviewed) unreviewed.append(it.key());
    }

    QJsonObject root;
    root.insert(QStringLiteral("version"),  1);
    root.insert(QStringLiteral("mod"),      m_mod);
    root.insert(QStringLiteral("language"), m_language);
    root.insert(QStringLiteral("saved"),
                QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    root.insert(QStringLiteral("entries"), entries);
    if (!unreviewed.isEmpty())
        root.insert(QStringLiteral("unreviewed"), unreviewed);

    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    // Compact: this one is machine-written and can carry twenty thousand
    // entries, where indenting doubles both the bytes and the time. The memory
    // file stays indented because a person is expected to open that one.
    const QByteArray json = QJsonDocument(root).toJson(QJsonDocument::Compact);
    if (f.write(json) != json.size()) return false;
    return f.commit();
}

Entry Progress::lookup(const QString &source) const
{
    if (source.isEmpty()) return {};
    return m_map.value(norm(source));
}

void Progress::record(const QString &source, const QString &translation,
                      bool reviewed)
{
    if (source.isEmpty()) return;
    // An empty answer is not an answer: storing one would make a blank row
    // read as done, and the counter and the filter both believe this.
    if (translation.isEmpty()) { forget(source); return; }
    m_map.insert(norm(source), Entry{translation, reviewed});
}

void Progress::forget(const QString &source)
{
    if (source.isEmpty()) return;
    m_map.remove(norm(source));
}

void Progress::setMod(const QString &modName, const QString &language)
{
    m_mod      = modName;
    m_language = language;
}

int Progress::staleAgainst(const QStringList &sources) const
{
    QSet<QString> live;
    live.reserve(sources.size());
    for (const QString &s : sources) live.insert(norm(s));

    int stale = 0;
    for (auto it = m_map.cbegin(); it != m_map.cend(); ++it)
        if (!live.contains(it.key())) ++stale;
    return stale;
}

QString fileNameFor(const QString &modName, const QString &language,
                    const QString &stableId)
{
    const QString lang = language.isEmpty() ? QStringLiteral("default")
                                            : language.toLower();

    // A mod page's own id, when there is one. Rename the mod, reinstall it
    // into a new folder - the file is still found, which is the difference
    // between resuming a month of work and starting it again.
    if (!stableId.isEmpty()) {
        QString id;
        id.reserve(stableId.size());
        for (const QChar &c : stableId)
            id.append(c.isLetterOrNumber() ? c.toLower() : QLatin1Char('-'));
        return QStringLiteral("translation_progress_%1_%2.json").arg(lang, id);
    }

    // A slug so the directory is legible, capped so a very long mod name
    // cannot push the path past what a filesystem will take.
    QString slug;
    slug.reserve(48);
    for (const QChar &c : modName) {
        if (slug.size() >= 48) break;
        slug.append(c.isLetterOrNumber() ? c.toLower() : QLatin1Char('_'));
    }
    if (slug.isEmpty()) slug = QStringLiteral("mod");

    // The hash is over the real name, so two mods whose punctuation folds to
    // the same slug still get their own file rather than silently sharing one.
    const QByteArray digest =
        QCryptographicHash::hash(modName.toUtf8(), QCryptographicHash::Sha1)
            .toHex().left(8);

    return QStringLiteral("translation_progress_%1_%2_%3.json")
        .arg(lang, slug, QString::fromLatin1(digest));
}

} // namespace translation_progress
