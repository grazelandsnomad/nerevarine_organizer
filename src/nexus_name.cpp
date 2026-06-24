#include "nexus_name.h"

#include <QRegularExpression>
#include <QRegularExpressionMatch>

QString nexusNameStem(const QString &name)
{
    static const QRegularExpression re(QStringLiteral(R"(-\d)"));
    QRegularExpressionMatch m = re.match(name);
    if (m.hasMatch()) return name.left(m.capturedStart());
    return name;
}
