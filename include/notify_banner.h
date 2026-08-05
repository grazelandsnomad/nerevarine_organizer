#ifndef NOTIFY_BANNER_H
#define NOTIFY_BANNER_H

#include <QObject>
#include <QString>

class QLabel;
class QTimer;
class QWidget;

class NotifyBanner : public QObject {
    Q_OBJECT
public:
    explicit NotifyBanner(QWidget *parent);

    QLabel *widget() const { return m_label; }

    // 7s auto-dismiss. bgColor is CSS hex like "#1a6fa8".
    void show(const QString &msg, const QString &bgColor);

    // Like show() but click-to-open url. kind=="loot_missing": right-click
    // also persistently silences future LOOT-missing banners
    // (QSettings "loot/banner_disabled").
    void showWithLink(const QString &msg, const QString &bgColor,
                      const QString &url, const QString &kind = QString());

    // Persistent banner (no 7s auto-dismiss) for an ongoing state the user must
    // be able to see and undo - e.g. a temporary view sort. A left-click emits
    // stickyClicked() and hides it. Stays up until hideSticky() or a click.
    void showSticky(const QString &msg, const QString &bgColor);
    void hideSticky();

    // Hide whatever is up, sticky or not, and cancel any pending auto-dismiss.
    // For a context change that makes the message WRONG rather than merely old:
    // switching game or modlist profile leaves a banner describing a mod set
    // that is no longer on screen ("All mods are up to date." is a claim about
    // the list you just navigated away from).
    void dismiss();

signals:
    void statusMessage(const QString &msg, int timeoutMs);
    // Emitted when a sticky banner (showSticky) is clicked.
    void stickyClicked();

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    QLabel *m_label = nullptr;
    // One restartable timer, not a singleShot per show(): two banners inside
    // the dismiss window would otherwise each own a timer, and the older one
    // would hide the newer message early.
    QTimer *m_hideTimer = nullptr;
};

#endif // NOTIFY_BANNER_H
