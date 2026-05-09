#include "modlist_io.h"

#include "safe_fs.h"

#include <QFile>
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

} // namespace modlist_io
