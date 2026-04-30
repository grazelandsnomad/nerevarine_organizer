#include "notify_banner.h"

#include <QDesktopServices>
#include <QEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QSettings>
#include <QTimer>
#include <QUrl>
#include <QVariant>
#include <Qt>

#include "translator.h"

NotifyBanner::NotifyBanner(QWidget *parent)
    : QObject(parent)
{
    m_label = new QLabel(parent);
    m_label->setAlignment(Qt::AlignCenter);
    m_label->setContentsMargins(12, 6, 12, 6);
    m_label->setWordWrap(false);
    m_label->hide();
    m_label->installEventFilter(this);
}

void NotifyBanner::show(const QString &msg, const QString &bgColor)
{
    m_label->setText(msg);
    m_label->setStyleSheet(
        QString("background-color: %1; color: white;"
                " font-weight: bold; font-size: 10pt;"
                " padding: 6px 12px; border-radius: 0px;")
            .arg(bgColor));
    m_label->setCursor(Qt::PointingHandCursor);
    m_label->setProperty("nerev_banner_url",  QVariant());
    m_label->setProperty("nerev_banner_kind", QVariant());
    m_label->show();
    QTimer::singleShot(7000, m_label, &QLabel::hide);
}

void NotifyBanner::showWithLink(const QString &msg, const QString &bgColor,
                                 const QString &url, const QString &kind)
{
    show(msg, bgColor);
    m_label->setProperty("nerev_banner_url",  url);
    m_label->setProperty("nerev_banner_kind", kind);
}

bool NotifyBanner::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == m_label && event->type() == QEvent::MouseButtonRelease) {
        auto *me = static_cast<QMouseEvent *>(event);
        QVariant urlVar  = m_label->property("nerev_banner_url");
        QVariant kindVar = m_label->property("nerev_banner_kind");
        if (me->button() == Qt::RightButton && kindVar.toString() == "loot_missing") {
            QSettings().setValue("loot/banner_disabled", true);
            emit statusMessage(T("loot_banner_suppressed"), 4000);
        } else if (urlVar.isValid() && !urlVar.toString().isEmpty()) {
            QDesktopServices::openUrl(QUrl(urlVar.toString()));
        }
        m_label->hide();
        return true;
    }
    return QObject::eventFilter(obj, event);
}
