#include "scan_overlay.h"

#include "translator.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>

ScanOverlay::ScanOverlay(QWidget *parent)
    : QWidget(parent)
{
    // No background of our own - paintEvent draws the rounded card, and a plain
    // rectangular fill behind it would show as corners.
    setAttribute(Qt::WA_NoSystemBackground);
    // The panel reports progress; it is not a control surface. Clicks that
    // aren't on the Hide button belong to the list underneath.
    setAttribute(Qt::WA_TransparentForMouseEvents, false);
    setFocusPolicy(Qt::NoFocus);
    hide();

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(18, 14, 18, 14);
    outer->setSpacing(10);

    m_title = new QLabel(this);
    m_title->setAlignment(Qt::AlignCenter);
    QFont tf = m_title->font();
    tf.setBold(true);
    m_title->setFont(tf);
    outer->addWidget(m_title);

    m_title->setMaximumWidth(420);   // so a wrapped result doesn't run wide

    m_bar = new QProgressBar(this);
    m_bar->setRange(0, 100);
    m_bar->setValue(0);
    m_bar->setTextVisible(true);
    m_bar->setMinimumWidth(280);
    m_bar->setFixedHeight(18);
    outer->addWidget(m_bar);

    auto *row = new QHBoxLayout;
    row->addStretch();
    m_hide = new QPushButton(T("scan_overlay_hide"), this);
    m_hide->setFocusPolicy(Qt::NoFocus);
    m_hide->setCursor(Qt::PointingHandCursor);
    connect(m_hide, &QPushButton::clicked, this, [this] {
        // Dismiss the panel, not the scan. The results still land when the
        // worker finishes; the user just stops watching it happen. On a result
        // panel this is the OK that acknowledges the answer.
        m_dismissed = true;
        m_running   = false;
        hide();
    });
    row->addWidget(m_hide);
    row->addStretch();
    outer->addLayout(row);

    applyPalette();
    if (parent) parent->installEventFilter(this);
    adjustSize();
}

void ScanOverlay::begin(const QString &title)
{
    m_running   = true;
    m_dismissed = false;
    m_title->setTextFormat(Qt::PlainText);
    QFont f = m_title->font();
    f.setBold(true);
    m_title->setFont(f);
    m_title->setText(title);
    m_title->setWordWrap(false);
    m_title->setMinimumWidth(0);   // the bar's 280 minimum sets the width here
    m_bar->setValue(0);
    m_bar->show();
    m_hide->setText(T("scan_overlay_hide"));
    applyPalette();      // theme may have changed since the last run
    adjustSize();
    reposition();
    show();
    raise();
}

void ScanOverlay::showResult(const QString &text)
{
    // No longer "running" for progress purposes, but the panel stays up: a
    // stray late poll must not turn the answer back into a progress bar.
    m_running   = false;
    m_dismissed = false;
    m_title->setWordWrap(true);
    // A wrapped label reports whatever width its own wrapping happens to pick,
    // which collapsed the card to a narrow column and broke the headline across
    // two lines. Pin a band (with the 420 maximum set in the ctor) so the text
    // wraps to the card instead of the card to the text.
    m_title->setMinimumWidth(360);
    // Headline bold, explanation not. A result is two sentences - the count and
    // what to do about it - and setting the whole block bold (as the progress
    // title is) makes it a wall that reads as one shouted line.
    {
        QFont f = m_title->font();
        f.setBold(false);
        m_title->setFont(f);
        m_title->setTextFormat(Qt::RichText);
        const int split = text.indexOf(QStringLiteral("\n\n"));
        const QString head = (split > 0 ? text.left(split) : text).toHtmlEscaped();
        const QString rest = (split > 0 ? text.mid(split + 2) : QString()).toHtmlEscaped();
        QString html = QStringLiteral("<div align='center'><b>") + head + "</b>";
        if (!rest.isEmpty())
            html += QStringLiteral("<br><br>") + QString(rest).replace('\n', "<br>");
        html += QStringLiteral("</div>");
        m_title->setText(html);
    }
    m_bar->hide();
    m_hide->setText(T("scan_overlay_ok"));
    applyPalette();
    adjustSize();
    reposition();
    show();
    raise();
}

void ScanOverlay::setPercent(int percent)
{
    if (!m_running) return;
    m_bar->setValue(qBound(0, percent, 100));
    if (!m_dismissed && !isVisible()) { reposition(); show(); raise(); }
}

void ScanOverlay::finish()
{
    m_running   = false;
    m_dismissed = false;
    hide();
}

// Track the viewport: a resize while the panel is up must keep it centred, and
// a scroll must not drag it along (it is a child of the viewport, so it does
// not scroll with the contents anyway).
bool ScanOverlay::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == parent() && event->type() == QEvent::Resize && isVisible())
        reposition();
    return QWidget::eventFilter(obj, event);
}

void ScanOverlay::reposition()
{
    auto *p = qobject_cast<QWidget *>(parent());
    if (!p) return;
    const QSize s = sizeHint();
    resize(s);
    move((p->width() - s.width()) / 2, (p->height() - s.height()) / 2);
}

// Read the card colours off the live palette rather than hardcoding them, so
// the dark-mode toggle carries the panel with it.
void ScanOverlay::applyPalette()
{
    const bool dark = palette().color(QPalette::Window).lightness() < 128;
    const QColor fg = dark ? QColor(235, 235, 235) : QColor(30, 30, 30);
    m_title->setStyleSheet(QString("color: %1;").arg(fg.name()));
}

void ScanOverlay::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const bool dark = palette().color(QPalette::Window).lightness() < 128;
    // Near-opaque: the panel has to read as a thing in front of the list, and a
    // translucent card over a list of mod names is just hard to read.
    const QColor card   = dark ? QColor(46, 46, 50, 246) : QColor(252, 252, 253, 246);
    const QColor border = dark ? QColor(96, 96, 104)     : QColor(178, 178, 186);

    QPainterPath path;
    path.addRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 8, 8);
    // Soft drop shadow so the card separates from the rows behind it without
    // needing a graphics effect (which would force a raster repaint of the view).
    for (int i = 4; i >= 1; --i) {
        QPainterPath halo;
        halo.addRoundedRect(QRectF(rect()).adjusted(-i + 0.5, -i + 0.5, i - 0.5, i - 0.5),
                            8 + i, 8 + i);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 10));
        p.drawPath(halo);
    }
    p.setPen(QPen(border, 1));
    p.setBrush(card);
    p.drawPath(path);
}
