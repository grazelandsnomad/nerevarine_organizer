#include "filter_bar.h"

#include <QAction>
#include <QColor>
#include <QFont>
#include <QIcon>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPainter>
#include <QPixmap>
#include <Qt>

#include "modroles.h"
#include "translator.h"

FilterBar::FilterBar(QListWidget *list, QObject *parent)
    : QObject(parent), m_list(list)
{
    m_edit = new QLineEdit;
    m_edit->setPlaceholderText(T("filter_placeholder"));
    m_edit->setClearButtonEnabled(true);
    m_edit->setContentsMargins(6, 4, 6, 4);

    // Leading search-glyph: prefer the system theme's "edit-find" icon so
    // the affordance matches the rest of the desktop's visual language.
    // Falls back to a Unicode magnifier glyph rendered as a pixmap when the
    // theme doesn't ship one (bare WMs, minimal icon themes).
    QIcon searchIcon = QIcon::fromTheme("edit-find");
    if (searchIcon.isNull()) {
        QPixmap pm(16, 16);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setFont(QFont(p.font().family(), 11));
        p.setPen(QColor(140, 140, 140));
        p.drawText(pm.rect(), Qt::AlignCenter, QStringLiteral("⌕"));
        searchIcon = QIcon(pm);
    }
    m_edit->addAction(searchIcon, QLineEdit::LeadingPosition);

    {
        QPixmap starPm(18, 18);
        starPm.fill(Qt::transparent);
        QPainter sp(&starPm);
        sp.setRenderHint(QPainter::TextAntialiasing, true);
        sp.setPen(QColor(220, 170, 0));
        QFont sf = sp.font();
        sf.setPointSize(12);
        sp.setFont(sf);
        sp.drawText(starPm.rect(), Qt::AlignCenter, QStringLiteral("★"));
        sp.end();
        m_favOnlyAction = m_edit->addAction(QIcon(starPm),
                                            QLineEdit::TrailingPosition);
        m_favOnlyAction->setCheckable(true);
        m_favOnlyAction->setToolTip(T("filter_favorites_only_tooltip"));
        connect(m_favOnlyAction, &QAction::toggled,
                this, [this]{ apply(); });
    }

    connect(m_edit, &QLineEdit::textChanged, this, [this]{ apply(); });
}

void FilterBar::focus()
{
    m_edit->setFocus();
    m_edit->selectAll();
}

bool FilterBar::hasText() const
{
    return !m_edit->text().trimmed().isEmpty();
}

void FilterBar::clearText()
{
    m_edit->clear();
}

void FilterBar::apply()
{
    QString q = m_edit->text().trimmed();
    bool favOnly = m_favOnlyAction && m_favOnlyAction->isChecked();

    if (q.isEmpty() && !favOnly) {
        // Restore collapsed-section state: hidden iff under a collapsed separator.
        QListWidgetItem *curSep = nullptr;
        for (int i = 0; i < m_list->count(); ++i) {
            auto *it = m_list->item(i);
            if (it->data(ModRole::ItemType).toString() == ItemType::Separator) {
                curSep = it;
                it->setHidden(false);
            } else {
                bool underCollapsed = curSep && curSep->data(ModRole::Collapsed).toBool();
                it->setHidden(underCollapsed);
            }
        }
        return;
    }

    // With an active filter: show only mods whose display text matches the
    // text query AND (if the ★ toggle is on) are flagged as favourites.
    // Separators follow their contents: visible iff at least one mod beneath
    // them survives the filter.
    QList<QListWidgetItem *> sections;
    QList<bool> sectionHasMatch;
    int currentSepIdx = -1;

    for (int i = 0; i < m_list->count(); ++i) {
        auto *it = m_list->item(i);
        if (it->data(ModRole::ItemType).toString() == ItemType::Separator) {
            currentSepIdx = sections.size();
            sections.append(it);
            sectionHasMatch.append(false);
            it->setHidden(true);
            continue;
        }
        bool textMatch = q.isEmpty() || it->text().contains(q, Qt::CaseInsensitive);
        bool favMatch  = !favOnly || it->data(ModRole::IsFavorite).toBool();
        bool match     = textMatch && favMatch;
        it->setHidden(!match);
        if (match && currentSepIdx >= 0)
            sectionHasMatch[currentSepIdx] = true;
    }
    for (int i = 0; i < sections.size(); ++i)
        if (sectionHasMatch[i]) sections[i]->setHidden(false);
}
