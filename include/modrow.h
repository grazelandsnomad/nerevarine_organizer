#pragma once

// ModRow - a handle to one row of a ModlistModel: the model and the row's
// session id. It has the slice of QListWidgetItem's surface the window
// uses (data/setData, text, checkState, flags, hidden, toolTip), and its
// arrow operator returns the handle itself, so `row->data(ModRole::X)`
// reads as it always did - but the data lives in the model, every write
// is a model write the view hears about, and a handle never dangles: a
// row that is gone reads blank and takes no writes. Handles compare and
// hash by id, and a null handle is spelled nullptr, like the pointer it
// replaces.

#include "modentry.h"
#include "modlist_model.h"

#include <QModelIndex>
#include <QUuid>
#include <QVariant>

#include <cstddef>

class ModRow {
public:
    ModRow() = default;
    ModRow(std::nullptr_t) {}
    ModRow(ModlistModel *model, const QUuid &id) : m_model(model), m_id(id) {}

    // Still a row of the model.
    [[nodiscard]] bool isValid() const { return row() >= 0; }
    explicit operator bool() const { return isValid(); }

    [[nodiscard]] ModlistModel *model() const { return m_model; }
    [[nodiscard]] QUuid         id() const { return m_id; }
    [[nodiscard]] int           row() const { return m_model ? m_model->findByRowId(m_id) : -1; }
    [[nodiscard]] QModelIndex   index() const;
    // The row's entry, or a blank one when the row is gone.
    [[nodiscard]] const ModEntry &entry() const;

    [[nodiscard]] QVariant data(int role) const;
    void setData(int role, const QVariant &value);
    [[nodiscard]] QString text() const { return data(Qt::DisplayRole).toString(); }
    void setText(const QString &text) { setData(Qt::DisplayRole, text); }
    [[nodiscard]] Qt::CheckState checkState() const;
    void setCheckState(Qt::CheckState state) { setData(Qt::CheckStateRole, int(state)); }
    [[nodiscard]] Qt::ItemFlags flags() const;
    void setFlags(Qt::ItemFlags flags);
    [[nodiscard]] bool isHidden() const { return entry().hidden; }
    void setHidden(bool hidden);
    [[nodiscard]] QString toolTip() const { return entry().toolTip; }
    void setToolTip(const QString &tip) { setData(Qt::ToolTipRole, tip); }

    [[nodiscard]] bool isMod() const { return entry().isMod(); }
    [[nodiscard]] bool isSeparator() const { return entry().isSeparator(); }
    [[nodiscard]] bool isInstalled() const { return entry().isInstalled(); }
    [[nodiscard]] bool isInstalling() const { return entry().isInstalling(); }

    ModRow       *operator->() { return this; }
    const ModRow *operator->() const { return this; }

    friend bool operator==(const ModRow &a, const ModRow &b)
    {
        return a.m_model == b.m_model && a.m_id == b.m_id;
    }
    friend bool operator!=(const ModRow &a, const ModRow &b) { return !(a == b); }
    // "No row" - a null handle or one whose row is gone - is what nullptr means.
    friend bool operator==(const ModRow &a, std::nullptr_t) { return !a.isValid(); }
    friend bool operator!=(const ModRow &a, std::nullptr_t) { return a.isValid(); }
    friend size_t qHash(const ModRow &r, size_t seed = 0) { return qHash(r.m_id, seed); }

private:
    ModlistModel *m_model = nullptr;
    QUuid         m_id;
};
