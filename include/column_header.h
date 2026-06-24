#ifndef COLUMN_HEADER_H
#define COLUMN_HEADER_H

#include <QObject>

#include "modroles.h"

class QAction;
class QHBoxLayout;
class QLabel;
class QListWidget;
class QPushButton;
class QWidget;

class ColumnHeader : public QObject {
    Q_OBJECT
public:
    explicit ColumnHeader(QWidget *parent);

    QWidget *widget() const;

    // Late-bound: populates the m_list pointer + installs the scrollbar
    // visibility filter. Call once from setupCentralWidget after m_modList
    // exists.
    void attachListWidget(QListWidget *list);

    // The two sort buttons live in MainWindow because their clicked signals
    // route to MainWindow sort slots and their text is updated by MainWindow
    // when the sort direction toggles. We accept the QPushButton pointers and
    // splice them into the bar layout in the right position.
    void setDateSortButton(QPushButton *btn);
    void setSizeSortButton(QPushButton *btn);

    QAction *colStatusAction()      const { return m_actStatus; }
    QAction *colDateAction()        const { return m_actDate; }
    QAction *colRelTimeAction()     const { return m_actRelTime; }
    QAction *colAnnotAction()       const { return m_actAnnot; }
    QAction *colSizeAction()        const { return m_actSize; }
    QAction *colVideoReviewAction() const { return m_actVideoReview; }

    const ColVisibility &visibility() const { return m_colVis; }

    // Save the current widths under the old state's keys, swap to the new
    // state, load that state's widths, apply, and re-align margins (deferred).
    void onWindowStateChanged(bool maximized);

    // Re-align the header right margin so its column edges line up with the
    // list viewport (which shifts as the vertical scrollbar shows/hides).
    void updateScrollMargin();

    // Push m_colVis to the labels/buttons/delegate and persist to QSettings.
    // Public so MainWindow's first-time setup can call it after the bar is
    // wired into the central widget.
    void apply();

signals:
    void visibilityChanged(const ColVisibility &cv);

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    void loadVisibilityFromSettings();
    void loadWidthsForCurrentState();
    void saveWidthsForCurrentState();
    int  separatorAt(int x) const;

    QListWidget *m_list = nullptr;
    QWidget     *m_bar  = nullptr;
    QHBoxLayout *m_layout = nullptr;
    QLabel      *m_nameHeader = nullptr;
    QLabel      *m_statusHeader = nullptr;
    QLabel      *m_relTimeHeader = nullptr;
    QLabel      *m_annotHeader = nullptr;
    QLabel      *m_videoReviewHeader = nullptr;
    QPushButton *m_dateBtn = nullptr;
    QPushButton *m_sizeBtn = nullptr;

    QAction *m_actStatus      = nullptr;
    QAction *m_actDate        = nullptr;
    QAction *m_actRelTime     = nullptr;
    QAction *m_actAnnot       = nullptr;
    QAction *m_actSize        = nullptr;
    QAction *m_actVideoReview = nullptr;

    ColVisibility m_colVis;
    bool m_maximized      = false;
    int  m_resizeSep      = -1;
    int  m_resizePressX   = 0;
    int  m_resizePressW   = 0;
    int  m_resizePressW2  = 0;
};

#endif // COLUMN_HEADER_H
