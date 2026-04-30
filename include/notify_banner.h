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

    // 7-second auto-dismiss. bgColor is a CSS hex like "#1a6fa8".
    void show(const QString &msg, const QString &bgColor);

    // Same as show(), but click-to-open url. If kind == "loot_missing",
    // a right-click also persistently suppresses future LOOT-missing
    // banners (QSettings key "loot/banner_disabled").
    void showWithLink(const QString &msg, const QString &bgColor,
                      const QString &url, const QString &kind = QString());

signals:
    void statusMessage(const QString &msg, int timeoutMs);

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    QLabel *m_label = nullptr;
};

#endif // NOTIFY_BANNER_H
