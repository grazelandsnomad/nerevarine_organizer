#include "modlist_io.h"

#include "safe_fs.h"

#include <QFile>
#include <QSaveFile>
#include <QTextStream>

namespace modlist_io {

std::optional<QString> writeModlistFile(const QString &path,
                                          const QString &content)
{
    (void)safefs::snapshotBackup(path);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
        return f.errorString();
    QTextStream ts(&f);
    ts << content;
    return std::nullopt;
}

std::optional<QString> writeLoadOrderFile(const QString &path,
                                          const QString &content)
{
    {
        QFile current(path);
        if (current.open(QIODevice::ReadOnly | QIODevice::Text)
            && QString::fromUtf8(current.readAll()) == content)
            return std::nullopt;
    }
    (void)safefs::snapshotBackup(path);
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
        return f.errorString();
    f.write(content.toUtf8());
    if (!f.commit())
        return f.errorString();
    return std::nullopt;
}

} // namespace modlist_io
