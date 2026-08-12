#ifndef SCAN_OVERLAY_H
#define SCAN_OVERLAY_H

// ScanOverlay - a progress panel floating in the middle of a view.
//
// For work that takes long enough to look like a hang but is not worth
// blocking on. A status-bar line is the wrong tool: it sits in the corner
// furthest from where the user is looking, carries no sense of how far along
// anything is, and a window that shows no other sign of life reads as frozen
// even while a worker thread is busy.
//
// So: centred over the view, with a real percentage, and NOT modal. The panel
// is a plain child widget - the list underneath stays scrollable and clickable
// throughout, and "Hide" dismisses the panel without touching the scan, which
// runs to completion either way.
//
// Owns nothing about the work itself. The caller drives it with begin() /
// setPercent() / finish() and decides what the work is.

#include <QWidget>

class QLabel;
class QProgressBar;
class QPushButton;

class ScanOverlay : public QWidget {
    Q_OBJECT
public:
    // `parent` should be the viewport the panel centres itself over; the
    // overlay installs an event filter on it to follow resizes.
    explicit ScanOverlay(QWidget *parent);

    // Show the panel with `title` above the bar, at 0%.
    void begin(const QString &title);
    // 0-100. Values outside that range are clamped. No-op once dismissed, so a
    // late poll can't resurrect a panel the user closed.
    void setPercent(int percent);
    // Hide and re-arm for the next begin().
    void finish();

    // End on an answer instead of just vanishing: drop the bar, show `text`,
    // and stay up until the user clicks OK.
    //
    // Worth the extra click because a fast scan is indistinguishable from one
    // that never ran - the panel flashes past and the list looks unchanged when
    // the honest answer is "nothing is wrong". Reserve it for a scan the user
    // actually asked for; a background re-run should use finish().
    void showResult(const QString &text);

    bool isRunning() const { return m_running; }

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    void reposition();
    void applyPalette();

    QLabel       *m_title    = nullptr;
    QProgressBar *m_bar      = nullptr;
    QPushButton  *m_hide     = nullptr;   // "Hide" while running, "OK" on a result
    bool          m_running   = false;
    // Set when the user clicks Hide. Keeps setPercent() from showing the panel
    // again for the rest of this scan; cleared by the next begin().
    bool          m_dismissed = false;
};

#endif // SCAN_OVERLAY_H
