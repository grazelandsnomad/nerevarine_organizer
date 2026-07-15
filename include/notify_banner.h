#ifndef NOTIFY_BANNER_H
#define NOTIFY_BANNER_H

#include <QObject>
#include <QString>

class QLabel;
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

signals:
    void statusMessage(const QString &msg, int timeoutMs);
    // Emitted when a sticky banner (showSticky) is clicked.
    void stickyClicked();

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    QLabel *m_label = nullptr;
};

#endif // NOTIFY_BANNER_H
