#include "modrow.h"

QModelIndex ModRow::index() const
{
    const int r = row();
    return r < 0 ? QModelIndex() : m_model->index(r);
}

const ModEntry &ModRow::entry() const
{
    static const ModEntry blank;
    const int r = row();
    return r < 0 ? blank : m_model->at(r);
}

QVariant ModRow::data(int role) const
{
    const int r = row();
    return r < 0 ? QVariant() : m_model->role(r, role);
}

void ModRow::setData(int role, const QVariant &value)
{
    const int r = row();
    if (r >= 0) m_model->setRole(r, role, value);
}

Qt::CheckState ModRow::checkState() const
{
    // Like QListWidgetItem: no check state (a separator) reads as Unchecked.
    return Qt::CheckState(data(Qt::CheckStateRole).toInt());
}

Qt::ItemFlags ModRow::flags() const
{
    const int r = row();
    return r < 0 ? Qt::ItemFlags() : m_model->flags(m_model->index(r));
}

void ModRow::setFlags(Qt::ItemFlags flags)
{
    const int r = row();
    if (r >= 0) m_model->setFlags(r, flags);
}

void ModRow::setHidden(bool hidden)
{
    const int r = row();
    if (r >= 0) m_model->setHidden(r, hidden);
}
