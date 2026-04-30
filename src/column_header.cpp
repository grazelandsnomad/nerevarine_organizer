#include "column_header.h"

#include <QAction>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollBar>
#include <QSettings>
#include <QString>
#include <QTimer>
#include <QWidget>
#include <Qt>

#include "translator.h"

ColumnHeader::ColumnHeader(QWidget *parent)
    : QObject(parent)
{
    m_bar = new QWidget(parent);
    m_layout = new QHBoxLayout(m_bar);
    m_layout->setContentsMargins(0, 3, 0, 3);
    m_layout->setSpacing(0);

    m_nameHeader        = new QLabel(T("col_mod_name"),       m_bar);
    m_statusHeader      = new QLabel(T("col_status"),         m_bar);
    m_relTimeHeader     = new QLabel(T("col_relative_time"),  m_bar);
    m_annotHeader       = new QLabel(T("col_annotation"),     m_bar);
    m_videoReviewHeader = new QLabel(T("col_video_review"),   m_bar);

    const QString headerStyle = "font-weight: bold; color: #888; font-size: 9pt;";
    m_nameHeader->setStyleSheet(headerStyle + " padding-left: 6px;");
    m_statusHeader->setStyleSheet(headerStyle);
    m_relTimeHeader->setStyleSheet(headerStyle);
    m_annotHeader->setStyleSheet(headerStyle);
    m_videoReviewHeader->setStyleSheet(headerStyle);
    m_videoReviewHeader->setAlignment(Qt::AlignCenter);

    loadVisibilityFromSettings();
    loadWidthsForCurrentState();

    m_statusHeader->setFixedWidth(m_colVis.wStatus);
    m_relTimeHeader->setFixedWidth(m_colVis.wRelTime);
    m_annotHeader->setFixedWidth(m_colVis.wAnnot);
    m_videoReviewHeader->setFixedWidth(m_colVis.wVideoReview);

    // Layout starts with name + status; the date and size sort buttons are
    // inserted later by setDate/SizeSortButton(). Insertion indices below
    // assume the final order: [name, status, date, relTime, annot, size, video].
    m_layout->addWidget(m_nameHeader, 1);
    m_layout->addWidget(m_statusHeader);
    // index 2: date button (added by setDateSortButton)
    m_layout->addWidget(m_relTimeHeader);
    m_layout->addWidget(m_annotHeader);
    // index 5 (after date insert): size button (added by setSizeSortButton)
    m_layout->addWidget(m_videoReviewHeader);

    m_bar->setMouseTracking(true);
    m_bar->installEventFilter(this);
    for (QWidget *hw : {static_cast<QWidget*>(m_statusHeader),
                         static_cast<QWidget*>(m_relTimeHeader),
                         static_cast<QWidget*>(m_annotHeader),
                         static_cast<QWidget*>(m_nameHeader)}) {
        hw->setMouseTracking(true);
        hw->installEventFilter(this);
        hw->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    }

    auto makeAct = [this](const QString &label, bool checked) {
        auto *a = new QAction(label, this);
        a->setCheckable(true);
        a->setChecked(checked);
        return a;
    };
    m_actStatus      = makeAct(T("col_status"),        m_colVis.status);
    m_actDate        = makeAct(T("col_date_added"),    m_colVis.date);
    m_actRelTime     = makeAct(T("col_relative_time"), m_colVis.relTime);
    m_actAnnot       = makeAct(T("col_annotation"),    m_colVis.annot);
    m_actSize        = makeAct(T("col_size"),          m_colVis.size);
    m_actVideoReview = makeAct(T("col_video_review"),  m_colVis.videoReview);
    connect(m_actStatus,      &QAction::toggled, this, [this](bool v){ m_colVis.status      = v; apply(); });
    connect(m_actDate,        &QAction::toggled, this, [this](bool v){ m_colVis.date        = v; apply(); });
    connect(m_actRelTime,     &QAction::toggled, this, [this](bool v){ m_colVis.relTime     = v; apply(); });
    connect(m_actAnnot,       &QAction::toggled, this, [this](bool v){ m_colVis.annot       = v; apply(); });
    connect(m_actSize,        &QAction::toggled, this, [this](bool v){ m_colVis.size        = v; apply(); });
    connect(m_actVideoReview, &QAction::toggled, this, [this](bool v){ m_colVis.videoReview = v; apply(); });
}

QWidget *ColumnHeader::widget() const { return m_bar; }

void ColumnHeader::attachListWidget(QListWidget *list)
{
    m_list = list;
    if (m_list) m_list->verticalScrollBar()->installEventFilter(this);
}

void ColumnHeader::setDateSortButton(QPushButton *btn)
{
    m_dateBtn = btn;
    btn->setFixedWidth(m_colVis.wDate);
    btn->setMouseTracking(true);
    btn->installEventFilter(this);
    // Insert between status (index 1) and relTime (index 2).
    m_layout->insertWidget(2, btn);
}

void ColumnHeader::setSizeSortButton(QPushButton *btn)
{
    m_sizeBtn = btn;
    btn->setFixedWidth(m_colVis.wSize);
    btn->setMouseTracking(true);
    btn->installEventFilter(this);
    // Insert between annot (index 4 once date is in) and video (index 5).
    m_layout->insertWidget(5, btn);
}

void ColumnHeader::loadVisibilityFromSettings()
{
    QSettings s;
    m_colVis.status      = s.value("ui/col_status",  true).toBool();
    m_colVis.date        = s.value("ui/col_date",    true).toBool();
    m_colVis.relTime     = s.value("ui/col_reltime", true).toBool();
    m_colVis.annot       = s.value("ui/col_annot",   true).toBool();
    m_colVis.size        = s.value("ui/col_size",    true).toBool();
    m_colVis.videoReview = s.value("ui/col_video",   true).toBool();
}

void ColumnHeader::saveWidthsForCurrentState()
{
    const QString sfx = m_maximized ? QStringLiteral("_max") : QStringLiteral("");
    QSettings s;
    s.setValue("ui/col_w_status"  + sfx, m_colVis.wStatus);
    s.setValue("ui/col_w_date"    + sfx, m_colVis.wDate);
    s.setValue("ui/col_w_reltime" + sfx, m_colVis.wRelTime);
    s.setValue("ui/col_w_annot"   + sfx, m_colVis.wAnnot);
    s.setValue("ui/col_w_size"    + sfx, m_colVis.wSize);
    s.setValue("ui/col_w_video"   + sfx, m_colVis.wVideoReview);
}

void ColumnHeader::loadWidthsForCurrentState()
{
    const QString sfx = m_maximized ? QStringLiteral("_max") : QStringLiteral("");
    QSettings s;
    auto clampW = [](int v, int def) { return qBound(ColWidth::Min, v > 0 ? v : def, 2000); };
    m_colVis.wStatus      = clampW(s.value("ui/col_w_status"  + sfx, ColWidth::Status).toInt(),       ColWidth::Status);
    m_colVis.wDate        = clampW(s.value("ui/col_w_date"    + sfx, ColWidth::DateAdded).toInt(),    ColWidth::DateAdded);
    m_colVis.wRelTime     = clampW(s.value("ui/col_w_reltime" + sfx, ColWidth::RelativeTime).toInt(), ColWidth::RelativeTime);
    m_colVis.wAnnot       = clampW(s.value("ui/col_w_annot"   + sfx, ColWidth::Annotation).toInt(),   ColWidth::Annotation);
    m_colVis.wSize        = clampW(s.value("ui/col_w_size"    + sfx, ColWidth::Size).toInt(),         ColWidth::Size);
    m_colVis.wVideoReview = clampW(s.value("ui/col_w_video"   + sfx, ColWidth::VideoReview).toInt(),  ColWidth::VideoReview);
}

void ColumnHeader::apply()
{
    m_statusHeader->setFixedWidth(m_colVis.wStatus);
    if (m_dateBtn) m_dateBtn->setFixedWidth(m_colVis.wDate);
    m_relTimeHeader->setFixedWidth(m_colVis.wRelTime);
    m_annotHeader->setFixedWidth(m_colVis.wAnnot);
    if (m_sizeBtn) m_sizeBtn->setFixedWidth(m_colVis.wSize);
    if (m_videoReviewHeader) m_videoReviewHeader->setFixedWidth(m_colVis.wVideoReview);

    m_statusHeader->setVisible(m_colVis.status);
    if (m_dateBtn) m_dateBtn->setVisible(m_colVis.date);
    m_relTimeHeader->setVisible(m_colVis.relTime);
    m_annotHeader->setVisible(m_colVis.annot);
    if (m_sizeBtn) m_sizeBtn->setVisible(m_colVis.size);
    if (m_videoReviewHeader) m_videoReviewHeader->setVisible(m_colVis.videoReview);

    QSettings s;
    s.setValue("ui/col_status",  m_colVis.status);
    s.setValue("ui/col_date",    m_colVis.date);
    s.setValue("ui/col_reltime", m_colVis.relTime);
    s.setValue("ui/col_annot",   m_colVis.annot);
    s.setValue("ui/col_size",    m_colVis.size);
    s.setValue("ui/col_video",   m_colVis.videoReview);
    saveWidthsForCurrentState();

    emit visibilityChanged(m_colVis);
}

void ColumnHeader::onWindowStateChanged(bool maximized)
{
    if (maximized == m_maximized) return;
    saveWidthsForCurrentState();
    m_maximized = maximized;
    loadWidthsForCurrentState();
    apply();
    // changeEvent fires BEFORE Qt has finished re-laying out the viewport for
    // the maximise/restore transition, so reading viewport()->geometry() now
    // would give stale dimensions. Defer the realignment until after the
    // pending layout pass.
    updateScrollMargin();
    QTimer::singleShot(0, this, &ColumnHeader::updateScrollMargin);
}

void ColumnHeader::updateScrollMargin()
{
    if (!m_bar || !m_list) return;
    auto *layout = qobject_cast<QHBoxLayout*>(m_bar->layout());
    if (!layout) return;
    const QRect vp = m_list->viewport()->geometry();
    const int leftMargin  = vp.left();
    const int rightMargin = qMax(0, m_list->width() - vp.right() - 1);
    auto margins = layout->contentsMargins();
    margins.setLeft(leftMargin);
    margins.setRight(rightMargin);
    layout->setContentsMargins(margins);
}

int ColumnHeader::separatorAt(int x) const
{
    const int hitZone = 5;
    int rm  = m_bar->layout()->contentsMargins().right();
    int pos = m_bar->width() - rm;
    if (m_colVis.size)    { pos -= m_colVis.wSize; }
    if (m_colVis.annot)   { pos -= m_colVis.wAnnot;   if (qAbs(x - pos) <= hitZone) return 3; }
    if (m_colVis.relTime) { pos -= m_colVis.wRelTime; if (qAbs(x - pos) <= hitZone) return 2; }
    if (m_colVis.date)    { pos -= m_colVis.wDate;    if (qAbs(x - pos) <= hitZone) return 1; }
    if (m_colVis.status)  { pos -= m_colVis.wStatus;  if (qAbs(x - pos) <= hitZone) return 0; }
    return -1;
}

bool ColumnHeader::eventFilter(QObject *obj, QEvent *event)
{
    // Vertical scrollbar appearing/disappearing changes the viewport width, so
    // update the header bar's right margin to keep column positions aligned.
    if (m_list && obj == m_list->verticalScrollBar()) {
        if (event->type() == QEvent::Show || event->type() == QEvent::Hide)
            updateScrollMargin();
        return false;
    }

    auto isHeaderObj = [&]() {
        return m_bar && (obj == m_bar ||
               obj == m_statusHeader || obj == m_dateBtn ||
               obj == m_relTimeHeader || obj == m_annotHeader ||
               obj == m_sizeBtn);
    };

    if (!isHeaderObj()) return QObject::eventFilter(obj, event);

    auto headerPos = [&](QMouseEvent *me) -> QPoint {
        if (obj == m_bar) return me->pos();
        return static_cast<QWidget*>(obj)->mapTo(m_bar, me->pos());
    };

    if (event->type() == QEvent::MouseMove) {
        auto *me = static_cast<QMouseEvent*>(event);
        int   hx = headerPos(me).x();

        if (m_resizeSep >= 0) {
            int delta = me->globalPosition().toPoint().x() - m_resizePressX;
            int newW  = qMax(ColWidth::Min, m_resizePressW - delta);
            switch (m_resizeSep) {
                case 0: {
                    auto *lay = m_bar->layout();
                    int margins = lay->contentsMargins().left() + lay->contentsMargins().right();
                    int rightCols = (m_colVis.date    ? m_colVis.wDate    : 0)
                                  + (m_colVis.relTime ? m_colVis.wRelTime : 0)
                                  + (m_colVis.annot   ? m_colVis.wAnnot   : 0)
                                  + (m_colVis.size    ? m_colVis.wSize    : 0);
                    int maxStatus = m_bar->width() - margins - ColWidth::NameMin - rightCols;
                    m_colVis.wStatus = qMin(newW, qMax(ColWidth::Min, maxStatus));
                    break;
                }
                case 1: {
                    int leftW = qMax(ColWidth::Min, m_resizePressW2 + delta);
                    newW = qMax(ColWidth::Min, m_resizePressW2 + m_resizePressW - leftW);
                    m_colVis.wStatus = leftW;
                    m_colVis.wDate   = newW;
                    break;
                }
                case 2: {
                    int leftW = qMax(ColWidth::Min, m_resizePressW2 + delta);
                    newW = qMax(ColWidth::Min, m_resizePressW2 + m_resizePressW - leftW);
                    m_colVis.wDate    = leftW;
                    m_colVis.wRelTime = newW;
                    break;
                }
                case 3: {
                    int leftW = qMax(ColWidth::Min, m_resizePressW2 + delta);
                    newW = qMax(ColWidth::Min, m_resizePressW2 + m_resizePressW - leftW);
                    m_colVis.wRelTime = leftW;
                    m_colVis.wAnnot   = newW;
                    break;
                }
            }
            apply();
            return true;
        }

        if (separatorAt(hx) >= 0)
            m_bar->setCursor(Qt::SizeHorCursor);
        else
            m_bar->unsetCursor();
        return false;
    }

    if (event->type() == QEvent::MouseButtonPress) {
        auto *me = static_cast<QMouseEvent*>(event);

        if (me->button() == Qt::RightButton) {
            auto *menu = new QMenu(m_bar);
            menu->setTitle(T("menu_columns"));
            menu->addAction(m_actStatus);
            menu->addAction(m_actDate);
            menu->addAction(m_actRelTime);
            menu->addAction(m_actAnnot);
            menu->addAction(m_actSize);
            menu->addAction(m_actVideoReview);
            menu->exec(me->globalPosition().toPoint());
            menu->deleteLater();
            return true;
        }

        if (me->button() == Qt::LeftButton) {
            int hx  = headerPos(me).x();
            int sep = separatorAt(hx);
            if (sep >= 0) {
                m_resizeSep    = sep;
                m_resizePressX = me->globalPosition().toPoint().x();
                switch (sep) {
                    case 0: m_resizePressW = m_colVis.wStatus;  m_resizePressW2 = 0;                break;
                    case 1: m_resizePressW = m_colVis.wDate;    m_resizePressW2 = m_colVis.wStatus; break;
                    case 2: m_resizePressW = m_colVis.wRelTime; m_resizePressW2 = m_colVis.wDate;   break;
                    case 3: m_resizePressW = m_colVis.wAnnot;   m_resizePressW2 = m_colVis.wRelTime;break;
                }
                return true;
            }
        }
        return false;
    }

    if (event->type() == QEvent::MouseButtonRelease) {
        if (m_resizeSep >= 0) {
            m_resizeSep = -1;
            apply();
            m_bar->unsetCursor();
            return true;
        }
        return false;
    }

    if (event->type() == QEvent::Leave && m_resizeSep < 0) {
        m_bar->unsetCursor();
    }

    return QObject::eventFilter(obj, event);
}
