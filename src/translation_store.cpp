#include "translation_store.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTextStream>

namespace translation_store {

QString normalize(const QString &source)
{
    return source.trimmed().toCaseFolded();
}

bool Memory::load(const QString &path)
{
    m_map.clear();

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return true;   // no memory yet, not a failure

    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return false;

    // {"entries": {"<source as typed>": "<translation>"}}. The source is stored
    // verbatim and normalised on read, so a memory file stays human-editable -
    // the user can open it and fix a translation without knowing our key rules.
    const QJsonObject entries = doc.object().value(QStringLiteral("entries")).toObject();
    for (auto it = entries.constBegin(); it != entries.constEnd(); ++it) {
        const QString translation = it.value().toString();
        if (translation.isEmpty()) continue;
        m_map.insert(normalize(it.key()), translation);
    }
    return true;
}

bool Memory::save(const QString &path) const
{
    QJsonObject entries;
    for (auto it = m_map.cbegin(); it != m_map.cend(); ++it)
        entries.insert(it.key(), it.value());

    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("entries"), entries);

    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    const QByteArray json = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (f.write(json) != json.size()) return false;
    return f.commit();
}

QString Memory::lookup(const QString &source) const
{
    if (source.isEmpty()) return {};
    return m_map.value(normalize(source));
}

void Memory::remember(const QString &source, const QString &translation)
{
    if (source.isEmpty()) return;
    const QString key = normalize(source);
    if (translation.trimmed().isEmpty()) m_map.remove(key);
    else                                 m_map.insert(key, translation);
}

Memory::ImportResult Memory::importScribeDb(const QString &path)
{
    ImportResult r;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {-1, -1};

    QTextStream in(&f);
    in.setEncoding(QStringConverter::Utf8);
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) continue;
        // First '=' splits; a translation containing '=' keeps its tail.
        const int sep = line.indexOf(QLatin1Char('='));
        if (sep <= 0) continue;
        const QString source = line.left(sep).trimmed();
        const QString target = line.mid(sep + 1).trimmed();
        if (source.isEmpty() || target.isEmpty()) continue;
        ++r.read;
        if (lookup(source).isEmpty()) {
            remember(source, target);
            ++r.added;
        }
    }
    return r;
}

QStringList Memory::sources() const
{
    QStringList out;
    out.reserve(int(m_map.size()));
    for (auto it = m_map.cbegin(); it != m_map.cend(); ++it) out << it.key();
    return out;
}

} // namespace translation_store
