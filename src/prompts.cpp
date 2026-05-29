#include "prompts.h"

#include <QMessageBox>

namespace ui {

bool confirm(QWidget *parent, const QString &title, const QString &body)
{
    return QMessageBox::question(parent, title, body,
                                 QMessageBox::Yes | QMessageBox::No)
           == QMessageBox::Yes;
}

void warn(QWidget *parent, const QString &title, const QString &body)
{
    QMessageBox::warning(parent, title, body);
}

void info(QWidget *parent, const QString &title, const QString &body)
{
    QMessageBox::information(parent, title, body);
}

void critical(QWidget *parent, const QString &title, const QString &body)
{
    QMessageBox::critical(parent, title, body);
}

} // namespace ui
