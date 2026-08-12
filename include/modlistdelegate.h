#ifndef MODLISTDELEGATE_H
#define MODLISTDELEGATE_H

#include <QStyledItemDelegate>
#include "modroles.h"

class QAbstractItemView;
class QHelpEvent;

class ModListDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    explicit ModListDelegate(QObject *parent = nullptr);

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option,
                   const QModelIndex &index) const override;
    bool helpEvent(QHelpEvent *event, QAbstractItemView *view,
                   const QStyleOptionViewItem &option,
                   const QModelIndex &index) override;

    void setAnimFrame(int frame) { m_animFrame = frame; }
    void setColVisibility(const ColVisibility &v) { m_colVis = v; }
    // Conflict arrows, the "overwritten by X" caption and the partner row wash.
    // Off draws none of them and suppresses their tooltip, so a row with a
    // conflict looks like any other. Toolbar toggle, persisted in Settings.
    void setConflictNoticesVisible(bool v) { m_conflictNotices = v; }
    // "no translation" / "N of M strings untranslated" caption, from the
    // on-demand translation scan. Same deal: off draws nothing and suppresses
    // the tooltip. Toolbar toggle, persisted in Settings.
    void setUntranslatedNoticesVisible(bool v) { m_untranslatedNotices = v; }

signals:
    void separatorCollapseToggleClicked(const QModelIndex &index);
    void updateArrowClicked(const QModelIndex &index);
    void favoriteToggleClicked(const QModelIndex &index);
    void videoReviewClicked(const QString &url);

protected:
    bool editorEvent(QEvent *event, QAbstractItemModel *model,
                     const QStyleOptionViewItem &option,
                     const QModelIndex &index) override;

private:
    // Icon rects in the name zone (slots measured from the left edge of the status column).
    // Slot 0 (rightmost): update-available arrow.
    // Slot 1: conflict warning triangle.
    // Slot 2: missing-master diamond.
    // Slot 3: missing-dependency yellow circle.
    // Slot 4 (leftmost): favourite star - shown when hovered, selected, or already favourited.
    QRect updateIconRect  (const QStyleOptionViewItem &option, int statusX) const;
    QRect conflictIconRect(const QStyleOptionViewItem &option, int statusX) const;
    QRect masterIconRect  (const QStyleOptionViewItem &option, int statusX) const;
    QRect depIconRect     (const QStyleOptionViewItem &option, int statusX) const;
    QRect favoriteIconRect(const QStyleOptionViewItem &option, int statusX) const;
    // Clickable "+ / −" collapse toggle on the left of a separator row.
    QRect separatorCollapseRect(const QStyleOptionViewItem &option) const;
    // Video-review column - centred icon inside the column rect.
    QRect videoReviewIconRect(const QStyleOptionViewItem &option, int videoX,
                              int videoW) const;

    int          m_animFrame = 0;
    bool         m_conflictNotices = true;
    bool         m_untranslatedNotices = false;
    ColVisibility m_colVis;
};

#endif // MODLISTDELEGATE_H
