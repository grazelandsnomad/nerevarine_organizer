#include "modlistdelegate.h"
#include "modroles.h"
#include "translator.h"
#include "video_reviews.h"
#include "separator_theme.h"
#include <QPainter>
#include <QApplication>
#include <QAbstractItemView>
#include <QDateTime>
#include <QEvent>
#include <QFont>
#include <QHelpEvent>
#include <QMouseEvent>
#include <QPen>
#include <QPolygon>
#include <QStyle>
#include <QToolTip>

static QString relativeTimeStr(const QDateTime &dt)
{
    if (!dt.isValid()) return {};
    qint64 s = dt.secsTo(QDateTime::currentDateTime());
    if (s < 0) s = 0;

    if (s < 60)           return T("rel_just_now");
    if (s < 5*60)         return T("rel_lt_5min");
    if (s < 15*60)        return T("rel_lt_15min");
    if (s < 30*60)        return T("rel_lt_30min");
    if (s < 3600)         return T("rel_lt_1h");
    if (s < 2*3600)       return T("rel_lt_2h");
    if (s < 6*3600)       return T("rel_lt_6h");
    if (s < 12*3600)      return T("rel_lt_12h");
    if (s < 24*3600)      return T("rel_lt_24h");
    if (s < 2*86400)      return T("rel_lt_2d");
    if (s < 7*86400)      return T("rel_lt_1w");
    if (s < 14*86400)     return T("rel_lt_2w");
    if (s < 30*86400)     return T("rel_lt_1mo");
    if (s < 90*86400)     return T("rel_lt_3mo");
    if (s < 183*86400)    return T("rel_lt_6mo");
    if (s < 365*86400)    return T("rel_lt_1y");
    qint64 years = qRound(s / (365.25 * 86400));
    return T("rel_n_years").arg(years);
}

ModListDelegate::ModListDelegate(QObject *parent)
    : QStyledItemDelegate(parent) {}

void ModListDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                            const QModelIndex &index) const
{
    if (index.data(ModRole::ItemType).toString() == ItemType::Separator) {
        painter->save();

        // Separator colours are authored for light mode and glare in dark mode.
        // resolve() darkens the bg toward black (keeping hue) and keeps the
        // user's label colour unless it stops reading. Live palette only, no
        // stored data touched; light mode unchanged.
        const bool darkUi = option.palette.color(QPalette::Window).lightness() < 128;
        const auto themed = separator_theme::resolve(
            index.data(ModRole::BgColor).value<QColor>(),
            index.data(ModRole::FgColor).value<QColor>(),
            darkUi);
        QColor bg = themed.bg;
        QColor fg = themed.fg;

        // Grey-out tint when some mod has a pending update. SepHasUpdate is set
        // on EVERY separator, not just the offending one, else updates in
        // collapsed neighbours stay hidden. Clears once all are handled.
        if (index.data(ModRole::SepHasUpdate).toBool()) {
            bg = QColor(210, 210, 210);
            fg = QColor(40, 40, 40);
        }

        painter->fillRect(option.rect, bg);
        painter->setPen(QPen(bg.darker(130), 1));
        painter->drawLine(option.rect.bottomLeft(), option.rect.bottomRight());

        QFont font = option.font;
        font.setBold(true);
        font.setPointSize(font.pointSize() + 1);
        painter->setFont(font);
        painter->setPen(fg);

        bool collapsed = index.data(ModRole::Collapsed).toBool();

        // Collapse toggle: rounded square, "+" collapsed / "−" expanded, in
        // the separator fg so it reads on any bg.
        QRect btn = separatorCollapseRect(option);
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        QColor btnBg = bg.lighter(115);
        if (btnBg == bg) btnBg = bg.darker(115);
        painter->setPen(QPen(fg, 1));
        painter->setBrush(btnBg);
        painter->drawRoundedRect(btn, 3, 3);
        QFont bf = option.font;
        bf.setBold(true);
        bf.setPointSize(qMax(bf.pointSize(), 10));
        painter->setFont(bf);
        painter->setPen(fg);
        // ASCII +/- so font fallback can't wreck alignment.
        painter->drawText(btn, Qt::AlignCenter,
                          collapsed ? QStringLiteral("+") : QStringLiteral("−"));
        painter->restore();

        // Active/total count on the right.
        QString countStr;
        QVariant tv = index.data(ModRole::TotalCount);
        if (tv.isValid()) {
            int active = index.data(ModRole::ActiveCount).toInt();
            int total  = tv.toInt();
            if (total > 0)
                countStr = QString("(%1/%2)").arg(active).arg(total);
        }

        // Reserve the count's space before the title, else long names bleed
        // into it. Measure with the count font so elision is right.
        int countReserve = 0;
        if (!countStr.isEmpty()) {
            QFont cf = painter->font();
            cf.setBold(false);
            cf.setPointSize(qMax(cf.pointSize() - 1, 7));
            countReserve = QFontMetrics(cf).horizontalAdvance(countStr) + 16;
        }

        // Title spans from the +/- button to the count zone.
        QRect textRect = option.rect.adjusted(
            btn.right() - option.rect.left() + 8, 0,
            -12 - countReserve, 0);
        // Elide, don't clip mid-glyph; full name is in the tooltip.
        const QString rawTitle = index.data(Qt::DisplayRole).toString();
        const QString elided   = painter->fontMetrics().elidedText(
            rawTitle, Qt::ElideRight, textRect.width());
        painter->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, elided);

        if (!countStr.isEmpty()) {
            QFont cf = painter->font();
            cf.setBold(false);
            cf.setPointSize(qMax(cf.pointSize() - 1, 7));
            QFont saved = painter->font();
            painter->setFont(cf);
            QColor dim = fg;
            dim.setAlphaF(0.75);
            painter->setPen(dim);
            QRect countRect = option.rect.adjusted(0, 0, -8, 0);
            painter->drawText(countRect, Qt::AlignVCenter | Qt::AlignRight, countStr);
            painter->setFont(saved);
            painter->setPen(fg);
        }

        painter->restore();
        return;
    }

    // Mod item: right-side zones.
    const int videoW    = m_colVis.videoReview ? m_colVis.wVideoReview : 0;
    const int sizeW     = m_colVis.size    ? m_colVis.wSize    : 0;
    const int annotW    = m_colVis.annot   ? m_colVis.wAnnot   : 0;
    const int relTimeW  = m_colVis.relTime ? m_colVis.wRelTime : 0;
    const int dateW     = m_colVis.date    ? m_colVis.wDate    : 0;
    const int statusW   = m_colVis.status  ? m_colVis.wStatus  : 0;
    const int totalRight = statusW + dateW + relTimeW + annotW + sizeW + videoW;

    const int videoX   = option.rect.right() - videoW;
    const int sizeX    = videoX   - sizeW;
    const int annotX   = sizeX    - annotW;
    const int relTimeX = annotX   - relTimeW;
    const int dateX    = relTimeX - dateW;
    const int statusX  = dateX    - statusW;

    // Paint normal item (bg, checkbox, name) clipped to the name zone.
    // Rows depending on another in-list mod indent one step to show the
    // parent-child relation, with a "↳" in the gutter. Checkbox moves with
    // the rect; child still toggles independently of parent.
    const bool indented = index.data(ModRole::HasInListDependency).toBool();
    const int indentPx  = indented ? 16 : 0;

    int installStatus = index.data(ModRole::InstallStatus).toInt();
    QStyleOptionViewItem nameOpt = option;
    if (installStatus == 2)
        nameOpt.rect.setBottom(nameOpt.rect.bottom() - 6); // leave room for bar
    if (indented)
        nameOpt.rect.setLeft(nameOpt.rect.left() + indentPx);

    // Utility-mod tint: muted grey to separate framework/library mods from
    // content mods. Skipped on selected rows so selection dominates. Fill via
    // the painter directly; QCommonStyle ignores backgroundBrush on most
    // platforms, only the palette::Base route works.
    const bool isUtility = index.data(ModRole::IsUtility).toBool();
    const bool selected  = (option.state & QStyle::State_Selected);
    const bool updateAvail = index.data(ModRole::UpdateAvailable).toBool();
    const QColor utilityTint(190, 190, 190);
    const QColor updateTint(30, 160, 30);

    // Conflict caption: "overwritten by <mod>" / "overwrites <mod>", right
    // aligned in the name zone. The arrows say a row wins or loses; only the
    // name says against whom, and hovering every row of a 380-mod list to find
    // out is not an answer. Reserved out of the name's width below so a long
    // mod name elides instead of running underneath it.
    // Two halves, each in its own direction's colour: green for what this mod
    // wins, orange for what it loses. Anchored right, "overwritten by" last so
    // that when space runs out it is the "overwrites" half that elides - being
    // overwritten is the half the user has to act on.
    QString capWin, capLose;
    // A record clash is a third kind: the mods share no file at all, so there
    // is no direction to draw and this list's order does not settle it. Shown
    // only when there is no file conflict competing for the space, which is the
    // usual case - a translation ships its own filename.
    bool capLoseIsRecords = false;
    if (m_conflictNotices) {
        const QStringList over  = index.data(ModRole::ConflictOverwrites).toStringList();
        const QStringList under = index.data(ModRole::ConflictOverwrittenBy).toStringList();
        auto part = [](const QString &verb, const QStringList &entries) {
            if (entries.isEmpty()) return QString();
            QString s = verb + QLatin1Char(' ') + entries.first().section('\t', 0, 0);
            if (entries.size() > 1)
                s += QStringLiteral(" +%1").arg(entries.size() - 1);
            return s;
        };
        capWin  = part(tr("overwrites"),     over);
        capLose = part(tr("overwritten by"), under);

        if (capWin.isEmpty() && capLose.isEmpty()) {
            const QStringList same =
                index.data(ModRole::ConflictSameRecords).toStringList();
            if (!same.isEmpty()) {
                const QStringList f = same.first().split('\t');
                capLose = tr("same %1 records as %2")
                              .arg(f.value(1), f.value(0));
                if (same.size() > 1)
                    capLose += QStringLiteral(" +%1").arg(same.size() - 1);
                capLoseIsRecords = true;
            }
        }
    }
    // Gap, not a separator glyph: eliding the win half would swallow a trailing
    // "·" and run the two halves together.
    const int capGap = (!capWin.isEmpty() && !capLose.isEmpty()) ? 12 : 0;
    // Deep tones on a light row, bright ones on anything dark - the selection
    // fill and dark mode both swallow the deep pair, and the conflict wash is
    // painted in these same hues, so a fixed colour goes near-invisible on the
    // row it matters most on.
    const bool darkRow = selected
        || option.palette.color(QPalette::Base).lightness() < 128;
    const QColor capWinColor  = darkRow ? QColor(120, 225, 145) : QColor( 38, 135, 58);
    // Violet for the record clash: a hue nothing else in the row uses, because
    // it is a different axis from the win/lose pair, not a third degree of it.
    const QColor capLoseColor = capLoseIsRecords
        ? (darkRow ? QColor(190, 155, 250) : QColor(110,  70, 185))
        : (darkRow ? QColor(250, 175,  90) : QColor(185,  88,   0));

    QFont captionFont = option.font;
    captionFont.setPointSize(qMax(option.font.pointSize() - 1, 7));
    const QFontMetrics captionFm(captionFont);

    // Stop the name (and the caption) before whatever icons this row actually
    // paints, so a narrow window elides instead of running the title under
    // them. Slots 0-3 only: the favourite star comes and goes with hover, and
    // re-eliding the name under the cursor would just twitch.
    int iconLeft = statusX;
    auto claimSlot = [&](const QRect &r) { iconLeft = qMin(iconLeft, r.left()); };
    if (index.data(ModRole::UpdateAvailable).toBool())
        claimSlot(updateIconRect(option, statusX));
    if (m_conflictNotices && index.data(ModRole::HasConflict).toBool())
        claimSlot(conflictIconRect(option, statusX));   // no icon, no reservation
    if (index.data(ModRole::HasMissingMaster).toBool())
        claimSlot(masterIconRect(option, statusX));
    if (index.data(ModRole::HasMissingDependency).toBool())
        claimSlot(depIconRect(option, statusX));
    // The star's slot is claimed whenever there is a caption to protect, not
    // when the star happens to be showing - it appears on hover, and a caption
    // that reflowed under the cursor would be worse than a little lost width.
    if (!capWin.isEmpty() || !capLose.isEmpty())
        claimSlot(favoriteIconRect(option, statusX));
    if (iconLeft < statusX)
        nameOpt.rect.setRight(qMax(nameOpt.rect.left(), iconLeft - 6));
    const int captionRight = iconLeft - 6;
    int capWinW  = captionFm.horizontalAdvance(capWin);
    int capLoseW = captionFm.horizontalAdvance(capLose);
    if (capWinW + capLoseW > 0) {
        // Never eat the whole name: the caption gets whatever is left once the
        // name has ~120px, and drops entirely below a readable width.
        const int avail = captionRight - 12 - capGap - (nameOpt.rect.left() + 120);
        if (capWinW + capLoseW > avail) {
            capWinW = qMax(0, avail - capLoseW);   // the win half yields first
            if (capWinW < 40) { capWinW = 0; capWin.clear(); }
            if (capLoseW > avail) capLoseW = qMax(0, avail);
            if (capLoseW < 40) { capLoseW = 0; capLose.clear(); }
        }
        if (capWinW + capLoseW > 0)
            nameOpt.rect.setRight(captionRight - 12 - capGap - capWinW - capLoseW);
    }

    painter->save();
    painter->setClipRect(QRect(option.rect.left(), option.rect.top(),
                               statusX - option.rect.left(), option.rect.height()));
    if (updateAvail && !selected) {
        // Green wash so pending-update rows can't be missed; white text.
        painter->fillRect(option.rect, updateTint);
        nameOpt.palette.setColor(QPalette::Base,            updateTint);
        nameOpt.palette.setColor(QPalette::AlternateBase,   updateTint);
        nameOpt.palette.setColor(QPalette::Window,          updateTint);
        nameOpt.palette.setColor(QPalette::Text,            Qt::white);
        nameOpt.palette.setColor(QPalette::WindowText,      Qt::white);
        nameOpt.palette.setColor(QPalette::HighlightedText, Qt::white);
        nameOpt.backgroundBrush = QBrush(updateTint);
    } else if (isUtility && !selected) {
        // Fill the name zone so the alternating-row colour doesn't bleed.
        painter->fillRect(option.rect, utilityTint);
        // Force the palette so the base delegate's fill picks up the tint.
        nameOpt.palette.setColor(QPalette::Base,          utilityTint);
        nameOpt.palette.setColor(QPalette::AlternateBase, utilityTint);
        nameOpt.palette.setColor(QPalette::Window,        utilityTint);
        nameOpt.backgroundBrush = QBrush(utilityTint);
    }
    QStyledItemDelegate::paint(painter, nameOpt, index);

    if (capWinW + capLoseW > 0) {
        painter->setFont(captionFont);
        auto drawHalf = [&](const QString &text, int w, int right, const QColor &c) {
            if (w <= 0 || text.isEmpty()) return;
            painter->setPen(c);
            const QRect r(right - w, option.rect.top(), w, option.rect.height());
            painter->drawText(r, Qt::AlignVCenter | Qt::AlignRight,
                              captionFm.elidedText(text, Qt::ElideRight, w));
        };
        const int right = captionRight - 8;
        drawHalf(capLose, capLoseW, right,                     capLoseColor);
        drawHalf(capWin,  capWinW,  right - capLoseW - capGap, capWinColor);
    }
    painter->restore();

    if (indented) {
        painter->save();
        // Dimmed gutter arrow; hard-coded colour like the other accents.
        QColor arrowColor = option.palette.color(QPalette::Mid);
        if (!arrowColor.isValid() || arrowColor.alpha() == 0)
            arrowColor = QColor(140, 140, 140);
        painter->setPen(arrowColor);
        QRect gutter(option.rect.left() + 2, option.rect.top(),
                     indentPx - 2, option.rect.height());
        painter->drawText(gutter,
                          Qt::AlignVCenter | Qt::AlignLeft,
                          QStringLiteral("↳"));
        painter->restore();
    }

    // Dependency highlight: full-row tint + bookended stripes.
    //   1 = dependency of the selected mod  → green
    //   2 = uses the selected mod           → blue/purple
    // A 35-alpha wash scrolled past too easily; bumped to ~115 with 6px edge
    // stripes so it reads as a bracketed band.
    int hlRole = index.data(ModRole::HighlightRole).toInt();
    if (hlRole > 0) {
        painter->save();
        bool isDep = (hlRole == 1);
        QColor tint   = isDep ? QColor(60, 200, 100, 115) : QColor(100, 140, 240, 90);
        QColor stripe = isDep ? QColor(30, 170,  70)      : QColor(70, 110, 220);
        painter->fillRect(option.rect, tint);
        const int stripeW = 6;
        painter->fillRect(QRect(option.rect.left(),                 option.rect.top(),
                                stripeW, option.rect.height()),     stripe);
        painter->fillRect(QRect(option.rect.right() - stripeW + 1,  option.rect.top(),
                                stripeW, option.rect.height()),     stripe);
        painter->restore();
    }

    // Conflict highlight: while a mod is selected, its conflict partners are
    // washed in the colour of who wins, so the pair reads off the list without
    // opening anything.
    //   1 = the selected mod overwrites this row  → orange (this row loses)
    //   2 = this row overwrites the selected mod  → green  (this row wins)
    // Same colours as the row's own arrows. Only when the dependency highlight
    // isn't already claiming the row; a mod can be both and two washes would
    // just muddy each other.
    const int conflictHl = m_conflictNotices
        ? index.data(ModRole::ConflictHighlight).toInt() : 0;
    if (hlRole == 0 && conflictHl > 0) {
        painter->save();
        const bool rowWins = (conflictHl == 2);
        QColor tint   = rowWins ? QColor( 46, 160, 67, 105) : QColor(210, 120, 20, 105);
        QColor stripe = rowWins ? QColor( 46, 160, 67)      : QColor(210, 100,  0);
        painter->fillRect(option.rect, tint);
        const int stripeW = 6;
        painter->fillRect(QRect(option.rect.left(),                option.rect.top(),
                                stripeW, option.rect.height()),    stripe);
        painter->fillRect(QRect(option.rect.right() - stripeW + 1, option.rect.top(),
                                stripeW, option.rect.height()),    stripe);
        painter->restore();
    }

    // Update-available icon: green down-triangle (slot 0, left of status).
    // Click routes to MainWindow::onInstallFromNexus via updateArrowClicked.
    if (index.data(ModRole::UpdateAvailable).toBool()) {
        QRect iconRect = updateIconRect(option, statusX);
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setPen(Qt::NoPen);
        // White on the green fill; selected rows get contrast from highlight.
        painter->setBrush(Qt::white);

        const int cx = iconRect.center().x();
        QPolygon tri;
        tri << QPoint(iconRect.left(),  iconRect.top())
            << QPoint(iconRect.right(), iconRect.top())
            << QPoint(cx,               iconRect.bottom());
        painter->drawPolygon(tri);
        painter->restore();
    }

    // Conflict direction arrows (slot 1), on every conflicting row.
    //   green ▲ - this mod overwrites one above it (its copy is the live one)
    //   orange ▼ - a mod below overwrites this one (its copy is dead)
    // Both when the mod sits mid-stack, winning against some and losing to
    // others. The arrow points at the row it acts on, which is the whole
    // question a translation raises: it only applies if it sits BELOW the mod
    // it translates. This used to be one orange "!" shown on the selected row
    // only, which flagged that a conflict existed and never said who won.
    if (m_conflictNotices) {
        const bool over  = !index.data(ModRole::ConflictOverwrites).toStringList().isEmpty();
        const bool under = !index.data(ModRole::ConflictOverwrittenBy).toStringList().isEmpty();
        const bool sameRecords =
            !index.data(ModRole::ConflictSameRecords).toStringList().isEmpty();
        if (!over && !under && sameRecords) {
            // No arrow: nothing about this list's order decides a record clash.
            // Two violet bars, an "=" for "these plugins write the same thing".
            const QRect slot = conflictIconRect(option, statusX);
            painter->save();
            painter->setPen(Qt::NoPen);
            painter->setBrush(QColor(130, 90, 200));
            const int w = slot.width() - 4, x = slot.left() + 2;
            const int cy = slot.center().y();
            painter->drawRect(QRect(x, cy - 4, w, 3));
            painter->drawRect(QRect(x, cy + 1, w, 3));
            painter->restore();
        }
        if (over || under) {
            const QRect slot = conflictIconRect(option, statusX);
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing, true);
            painter->setPen(Qt::NoPen);

            // One arrow centred, two side by side sharing the slot.
            const int aw  = (over && under) ? (slot.width() - 2) / 2 : slot.width() - 4;
            const int top = slot.top() + 1;
            const int bot = slot.bottom() - 1;
            int x = (over && under) ? slot.left()
                                    : slot.left() + (slot.width() - aw) / 2;

            auto arrow = [&](bool up, const QColor &c) {
                painter->setBrush(c);
                const int cx = x + aw / 2;
                QPolygon tri;
                if (up) tri << QPoint(cx, top) << QPoint(x, bot) << QPoint(x + aw, bot);
                else    tri << QPoint(x, top) << QPoint(x + aw, top) << QPoint(cx, bot);
                painter->drawPolygon(tri);
                x += aw + 2;
            };

            if (over)  arrow(true,  QColor( 46, 160,  67));
            if (under) arrow(false, QColor(210, 100,   0));
            painter->restore();
        }
    }

    // Missing-master icon: red diamond with ? (slot 2)
    if (index.data(ModRole::HasMissingMaster).toBool()) {
        QRect iconRect = masterIconRect(option, statusX);
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(200, 50, 50));

        int cx  = iconRect.center().x();
        int cy  = iconRect.center().y();
        int w   = iconRect.width();
        int h   = iconRect.height();
        QPolygon diamond;
        diamond << QPoint(cx, cy - h / 2)
                << QPoint(cx + w / 2, cy)
                << QPoint(cx, cy + h / 2)
                << QPoint(cx - w / 2, cy);
        painter->drawPolygon(diamond);

        painter->setPen(QPen(Qt::white, 1.5));
        painter->setFont([&]() {
            QFont f = option.font;
            f.setBold(true);
            f.setPointSize(qMax(f.pointSize() - 2, 6));
            return f;
        }());
        painter->drawText(iconRect, Qt::AlignHCenter | Qt::AlignVCenter, "?");
        painter->restore();
    }

    // Favourite star (slot 4): gold ★ when favourited, dim ☆ on hover/select.
    {
        const bool isFav  = index.data(ModRole::IsFavorite).toBool();
        const bool hovered = option.state & QStyle::State_MouseOver;
        if (isFav || hovered || selected) {
            QRect iconRect = favoriteIconRect(option, statusX);
            painter->save();
            painter->setRenderHint(QPainter::TextAntialiasing, true);
            QFont starFont = option.font;
            // Bump glyph size to fill the 22px slot (11pt looked tiny in it).
            starFont.setPointSize(qMax(option.font.pointSize() + 4, 16));
            painter->setFont(starFont);
            if (isFav) {
                painter->setPen(QColor(220, 170, 0));
                painter->drawText(iconRect, Qt::AlignCenter, QStringLiteral("★"));
            } else {
                // Ghost star: gold preview at mid opacity so it stays visible
                // on normal and selection-highlight backgrounds. alpha 55 (~21%)
                // was near-invisible, especially when selected.
                QColor ghost(220, 170, 0, hovered ? 180 : 110);
                painter->setPen(ghost);
                painter->drawText(iconRect, Qt::AlignCenter, QStringLiteral("☆"));
            }
            painter->restore();
        }
    }

    // Missing-dependency icon: yellow circle with ! (slot 3)
    if (index.data(ModRole::HasMissingDependency).toBool()) {
        QRect iconRect = depIconRect(option, statusX);
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(230, 185, 40));
        painter->drawEllipse(iconRect);

        painter->setPen(QPen(Qt::black, 1.5));
        painter->setFont([&]() {
            QFont f = option.font;
            f.setBold(true);
            f.setPointSize(qMax(f.pointSize() - 2, 6));
            return f;
        }());
        painter->drawText(iconRect, Qt::AlignHCenter | Qt::AlignVCenter, "!");
        painter->restore();
    }

    if (totalRight == 0) return;

    // Fill background for all right zones (matches selection state).
    auto paintZoneBg = [&](QRect r) {
        QStyleOptionViewItem bg = option;
        bg.rect = r;
        bg.text.clear();
        bg.features &= ~QStyleOptionViewItem::HasCheckIndicator;
        bg.features &= ~QStyleOptionViewItem::HasDecoration;
        const QWidget *w = option.widget;
        QStyle *style = w ? w->style() : QApplication::style();
        style->drawPrimitive(QStyle::PE_PanelItemViewItem, &bg, painter, w);
    };
    paintZoneBg(QRect(statusX, option.rect.top(), totalRight, option.rect.height()));

    bool sel = option.state & QStyle::State_Selected;
    const bool updateRow = updateAvail && !sel;
    if (updateRow) {
        painter->fillRect(QRect(statusX, option.rect.top(),
                                totalRight, option.rect.height()),
                          updateTint);
    }
    QPen dividerPen(sel ? QColor(255, 255, 255, 60)
                        : (updateRow ? QColor(255, 255, 255, 90)
                                     : QColor(190, 190, 205)), 1);

    // Progress bar for downloading/extracting items.
    if (installStatus == 2) {
        QVariant pv = index.data(ModRole::DownloadProgress);
        int nameZoneRight = statusX - 1;
        int barH  = 4;
        int barY  = option.rect.bottom() - barH - 1;
        int barX  = option.rect.left() + 4;
        int barW  = nameZoneRight - barX - 4;

        // Track background
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, false);
        painter->fillRect(barX, barY, barW, barH, QColor(200, 200, 210));

        if (!pv.isValid() || pv.toInt() < 0) {
            // Extracting: indeterminate stripe, phase-shifted per row so
            // concurrent installs don't animate in lockstep.
            int phase  = (index.row() * 137) % (barW + 24);
            int stripe = (m_animFrame * 12 + phase) % (barW + 24);
            painter->setClipRect(barX, barY, barW, barH);
            painter->fillRect(barX + stripe - 24, barY, 24, barH, QColor(210, 130, 0));
        } else {
            int filled = barW * pv.toInt() / 100;
            painter->fillRect(barX, barY, filled, barH, QColor(210, 130, 0));
        }
        painter->restore();
    }

    // Status column
    if (m_colVis.status) {
        painter->setPen(dividerPen);
        painter->drawLine(statusX, option.rect.top() + 3, statusX, option.rect.bottom() - 3);

        static const char *kSpinner[] = {"⠋","⠙","⠹","⠸","⠼","⠴","⠦","⠧","⠇","⠏"};
        int installStatus = index.data(ModRole::InstallStatus).toInt();
        QString statusText;
        QColor  statusColor;
        if (installStatus == 2) {
            statusText  = QString("%1 installing…").arg(kSpinner[m_animFrame % 10]);
            statusColor = sel ? option.palette.color(QPalette::HighlightedText)
                              : (updateRow ? QColor(Qt::white) : QColor(210, 130, 0));
        } else if (installStatus == 1) {
            statusText  = "● installed";
            statusColor = sel ? option.palette.color(QPalette::HighlightedText)
                              : (updateRow ? QColor(Qt::white) : QColor(40, 160, 40));
        } else {
            statusText  = "○ not installed";
            statusColor = sel ? option.palette.color(QPalette::HighlightedText)
                              : (updateRow ? QColor(Qt::white) : QColor(150, 150, 150));
        }
        painter->save();
        QFont sf = option.font;
        sf.setPointSize(qMax(sf.pointSize() - 1, 7));
        painter->setFont(sf);
        painter->setPen(statusColor);
        painter->drawText(QRect(statusX + 5, option.rect.top(),
                                statusW - 8, option.rect.height()),
                          Qt::AlignVCenter | Qt::AlignLeft | Qt::TextSingleLine,
                          statusText);
        painter->restore();
    }

    // Date column
    if (m_colVis.date) {
        painter->setPen(dividerPen);
        painter->drawLine(dateX, option.rect.top() + 3, dateX, option.rect.bottom() - 3);

        QDateTime dt = index.data(ModRole::DateAdded).toDateTime();
        if (dt.isValid()) {
            painter->save();
            QFont df = option.font;
            df.setPointSize(qMax(df.pointSize() - 1, 7));
            painter->setFont(df);
            painter->setPen(sel ? option.palette.color(QPalette::HighlightedText)
                                : (updateRow ? QColor(Qt::white) : QColor(120, 120, 140)));
            painter->drawText(QRect(dateX + 5, option.rect.top(),
                                    dateW - 8, option.rect.height()),
                              Qt::AlignVCenter | Qt::AlignLeft | Qt::TextSingleLine,
                              dt.toString("MMM d yyyy"));
            painter->restore();
        }
    }

    // Relative-time column
    if (m_colVis.relTime) {
        painter->setPen(dividerPen);
        painter->drawLine(relTimeX, option.rect.top() + 3, relTimeX, option.rect.bottom() - 3);

        QString rel = relativeTimeStr(index.data(ModRole::DateAdded).toDateTime());
        if (!rel.isEmpty()) {
            painter->save();
            QFont rf = option.font;
            rf.setPointSize(qMax(rf.pointSize() - 1, 7));
            painter->setFont(rf);
            painter->setPen(sel ? option.palette.color(QPalette::HighlightedText)
                                : (updateRow ? QColor(Qt::white) : QColor(100, 130, 160)));
            painter->drawText(QRect(relTimeX + 5, option.rect.top(),
                                    relTimeW - 8, option.rect.height()),
                              Qt::AlignVCenter | Qt::AlignLeft | Qt::TextSingleLine,
                              rel);
            painter->restore();
        }
    }

    // Annotation column
    if (m_colVis.annot) {
        painter->setPen(dividerPen);
        painter->drawLine(annotX, option.rect.top() + 3, annotX, option.rect.bottom() - 3);

        QString annotation = index.data(ModRole::Annotation).toString();
        if (!annotation.isEmpty()) {
            painter->save();
            QFont f = option.font;
            f.setItalic(true);
            painter->setFont(f);
            painter->setPen(sel ? option.palette.color(QPalette::HighlightedText)
                                : (updateRow ? QColor(Qt::white) : QColor(100, 100, 130)));
            painter->drawText(QRect(annotX + 8, option.rect.top(),
                                    annotW - 12, option.rect.height()),
                              Qt::AlignVCenter | Qt::AlignLeft | Qt::TextSingleLine,
                              annotation);
            painter->restore();
        }
    }

    // Size column, colour-coded by footprint (>=1 GB red, >=256 MB amber, else green)
    if (m_colVis.size) {
        painter->setPen(dividerPen);
        painter->drawLine(sizeX, option.rect.top() + 3, sizeX, option.rect.bottom() - 3);

        QVariant sv = index.data(ModRole::ModSize);
        QString text;
        QColor  color = QColor(150, 150, 150);
        if (sv.isValid() && sv.toLongLong() > 0) {
            qint64 bytes = sv.toLongLong();
            const double MB = 1024.0 * 1024.0;
            const double GB = MB * 1024.0;
            if (bytes >= GB) {
                text  = QString::number(bytes / GB, 'f', 2) + " GB";
                color = QColor(200, 50, 50);     // >1 GB
            } else if (bytes >= 256 * (qint64)MB) {
                text  = QString::number(bytes / MB, 'f', 0) + " MB";
                color = QColor(200, 150, 30);    // 256 MB .. 1 GB
            } else if (bytes >= (qint64)MB) {
                text  = QString::number(bytes / MB, 'f', 0) + " MB";
                color = QColor(40, 140, 40);     // < 256 MB
            } else {
                text  = QString::number(bytes / 1024.0, 'f', 0) + " KB";
                color = QColor(40, 140, 40);
            }
        } else if (index.data(ModRole::InstallStatus).toInt() != 1) {
            text = QString(QChar(0x2014)); // em-dash = not installed
        }
        if (!text.isEmpty()) {
            painter->save();
            QFont sf = option.font;
            sf.setPointSize(qMax(sf.pointSize() - 1, 7));
            painter->setFont(sf);
            painter->setPen(sel ? option.palette.color(QPalette::HighlightedText)
                                : (updateRow ? QColor(Qt::white) : color));
            painter->drawText(QRect(sizeX + 5, option.rect.top(),
                                    sizeW - 10, option.rect.height()),
                              Qt::AlignVCenter | Qt::AlignRight | Qt::TextSingleLine,
                              text);
            painter->restore();
        }
    }

    // Video-review column: glyph when a review URL is known for this mod.
    // Lookup lives in video_reviews.h (hardcoded table).
    if (m_colVis.videoReview) {
        painter->setPen(dividerPen);
        painter->drawLine(videoX, option.rect.top() + 3, videoX, option.rect.bottom() - 3);

        QString displayName = index.data(ModRole::CustomName).toString();
        if (displayName.isEmpty()) displayName = index.data(Qt::DisplayRole).toString();
        QString url = index.data(ModRole::VideoUrl).toString();
        if (url.isEmpty()) url = video_reviews::urlFor(displayName);
        if (!url.isEmpty()) {
            painter->save();
            QFont vf = option.font;
            vf.setPointSize(qMax(vf.pointSize() + 2, 13));
            painter->setFont(vf);
            painter->drawText(videoReviewIconRect(option, videoX, videoW),
                              Qt::AlignCenter, QStringLiteral("▶"));
            painter->restore();
        }
    }
}

// Update-available icon rect. statusX = left edge of the status column.
// Slot 0 (rightmost), the update arrow. Wider than other slots so the
// down-triangle reads as a "download" glyph and is easier to click. 18px is
// the max that keeps the rest of the strip (slots 1-3 at 14px, 4px gaps)
// aligned.
QRect ModListDelegate::updateIconRect(const QStyleOptionViewItem &option, int statusX) const
{
    const int iconW = 18;
    const int iconH = 14;
    int x = statusX - iconW - 4;
    int y = option.rect.top() + (option.rect.height() - iconH) / 2;
    return QRect(x, y, iconW, iconH);
}

// Slot 1: conflict warning triangle.
QRect ModListDelegate::conflictIconRect(const QStyleOptionViewItem &option, int statusX) const
{
    const int iconW = 14;
    const int iconH = 14;
    int x = statusX - 2 * (iconW + 4);
    int y = option.rect.top() + (option.rect.height() - iconH) / 2;
    return QRect(x, y, iconW, iconH);
}

// Slot 2: missing-master diamond.
QRect ModListDelegate::masterIconRect(const QStyleOptionViewItem &option, int statusX) const
{
    const int iconW = 14;
    const int iconH = 14;
    int x = statusX - 3 * (iconW + 4);
    int y = option.rect.top() + (option.rect.height() - iconH) / 2;
    return QRect(x, y, iconW, iconH);
}

// Slot 3: missing-dependency yellow circle.
QRect ModListDelegate::depIconRect(const QStyleOptionViewItem &option, int statusX) const
{
    const int iconW = 14;
    const int iconH = 14;
    int x = statusX - 4 * (iconW + 4);
    int y = option.rect.top() + (option.rect.height() - iconH) / 2;
    return QRect(x, y, iconW, iconH);
}

// Slot 4: favourite star. Drawn larger than the other icons - it's the most
// scanned marker (users eyeball the stars to find favourites) and glyph stars
// render poorly at 14px on most fonts.
QRect ModListDelegate::favoriteIconRect(const QStyleOptionViewItem &option, int statusX) const
{
    const int iconW = 22;
    const int iconH = 22;
    // Keep the star's right edge at the slot-4 boundary (statusX - 4*(14+4) =
    // statusX - 72) so it doesn't overlap slot 3.
    int x = statusX - 4 * (14 + 4) - iconW - 2;
    int y = option.rect.top() + (option.rect.height() - iconH) / 2;
    return QRect(x, y, iconW, iconH);
}

// Video-review icon, centred in its column. videoX = left edge, videoW =
// width; returns a square ~row-height minus 4px padding so the glyph doesn't
// clip.
QRect ModListDelegate::videoReviewIconRect(const QStyleOptionViewItem &option,
                                           int videoX, int videoW) const
{
    const int side = qMin(videoW - 4, option.rect.height() - 4);
    const int x = videoX + (videoW - side) / 2;
    const int y = option.rect.top() + (option.rect.height() - side) / 2;
    return QRect(x, y, side, side);
}

QRect ModListDelegate::separatorCollapseRect(const QStyleOptionViewItem &option) const
{
    const int pad = 4;
    int side = option.rect.height() - 2 * pad;
    side = qMax(14, qMin(side, 22));
    int x = option.rect.left() + 6;
    int y = option.rect.top() + (option.rect.height() - side) / 2;
    return QRect(x, y, side, side);
}

bool ModListDelegate::helpEvent(QHelpEvent *event, QAbstractItemView *view,
                                const QStyleOptionViewItem &option,
                                const QModelIndex &index)
{
    if (event->type() == QEvent::ToolTip &&
        index.data(ModRole::ItemType).toString() == ItemType::Mod)
    {
        const int videoW   = m_colVis.videoReview ? m_colVis.wVideoReview : 0;
        const int sizeW    = m_colVis.size    ? m_colVis.wSize    : 0;
        const int annotW   = m_colVis.annot   ? m_colVis.wAnnot   : 0;
        const int relTimeW = m_colVis.relTime ? m_colVis.wRelTime : 0;
        const int dateW    = m_colVis.date    ? m_colVis.wDate    : 0;
        const int statusW  = m_colVis.status  ? m_colVis.wStatus  : 0;
        const int statusX  = option.rect.right() - videoW - sizeW - annotW - relTimeW - dateW - statusW;

        if (index.data(ModRole::UpdateAvailable).toBool()) {
            QRect iconRect = updateIconRect(option, statusX);
            if (iconRect.contains(event->pos())) {
                QToolTip::showText(event->globalPos(), tr("Download update"), view);
                return true;
            }
        }

        if (m_conflictNotices && index.data(ModRole::HasConflict).toBool()) {
            QRect iconRect = conflictIconRect(option, statusX);
            if (iconRect.contains(event->pos())) {
                // Entry format: "DisplayName\tfile1\tfile2\t...\t[+N more]"
                const QStringList over  =
                    index.data(ModRole::ConflictOverwrites).toStringList();
                const QStringList under =
                    index.data(ModRole::ConflictOverwrittenBy).toStringList();

                QString tip;
                auto section = [&](const QString &heading, const QStringList &entries) {
                    if (entries.isEmpty()) return;
                    if (!tip.isEmpty()) tip += "\n\n";
                    tip += heading;
                    for (const QString &entry : entries) {
                        const QStringList parts = entry.split('\t');
                        const QStringList files = parts.mid(1);
                        tip += "\n\n";
                        tip += QString("  %1  (%2 shared file(s)):")
                                   .arg(parts.value(0)).arg(files.size());
                        for (const QString &f : files)
                            tip += "\n    " + f;
                    }
                };
                // Named for what the user has to decide, not for the mechanism:
                // "my files win" is the thing they came to check.
                section(tr("Overwrites (this mod's files load):"),      over);
                section(tr("Overwritten by (their files load instead):"), under);

                // Record clashes list a count, not filenames, and carry the
                // caveat that the plugin load order settles them - not this
                // list, which is what the arrows above answer to.
                const QStringList same =
                    index.data(ModRole::ConflictSameRecords).toStringList();
                if (!same.isEmpty()) {
                    if (!tip.isEmpty()) tip += "\n\n";
                    tip += tr("Rewrites the same records as (no shared file; the "
                              "plugin load order decides, not this list):");
                    for (const QString &entry : same) {
                        const QStringList f = entry.split('\t');
                        tip += "\n\n  " + f.value(0)
                             + tr("  (%1 shared record(s))").arg(f.value(1));
                        for (const QString &p : f.mid(2)) tip += "\n    " + p;
                    }
                }

                QToolTip::showText(event->globalPos(), tip, view);
                return true;
            }
        }

        if (index.data(ModRole::HasMissingMaster).toBool()) {
            QRect iconRect = masterIconRect(option, statusX);
            if (iconRect.contains(event->pos())) {
                // Entry format: "plugin.esp\tmaster1.esm\tmaster2.esm"
                QStringList entries = index.data(ModRole::MissingMasters).toStringList();
                QString tip = tr("Missing masters:");
                for (const QString &entry : entries) {
                    QStringList parts = entry.split('\t');
                    QString plugin = parts.value(0);
                    QStringList masters = parts.mid(1);
                    tip += "\n\n  " + plugin + " requires:";
                    for (const QString &m : masters)
                        tip += "\n    " + m;
                }
                QToolTip::showText(event->globalPos(), tip, view);
                return true;
            }
        }

        if (index.data(ModRole::HasMissingDependency).toBool()) {
            QRect iconRect = depIconRect(option, statusX);
            if (iconRect.contains(event->pos())) {
                QStringList entries = index.data(ModRole::MissingDependencies).toStringList();
                QString tip = tr("Missing dependencies:");
                for (const QString &entry : entries)
                    tip += "\n  • " + entry;
                QToolTip::showText(event->globalPos(), tip, view);
                return true;
            }
        }

        {
            const bool isFav  = index.data(ModRole::IsFavorite).toBool();
            const bool hovered = option.state & QStyle::State_MouseOver;
            const bool sel    = option.state & QStyle::State_Selected;
            if (isFav || hovered || sel) {
                QRect iconRect = favoriteIconRect(option, statusX);
                if (iconRect.contains(event->pos())) {
                    QString tip = isFav
                        ? tr("★ Favourite mod - this one is special to you.\nClick to remove from favourites.")
                        : tr("☆ Mark as favourite - flag this mod as specially dear to you.\nClick to add to favourites.");
                    QToolTip::showText(event->globalPos(), tip, view);
                    return true;
                }
            }
        }

    }
    return QStyledItemDelegate::helpEvent(event, view, option, index);
}

bool ModListDelegate::editorEvent(QEvent *event, QAbstractItemModel *model,
                                  const QStyleOptionViewItem &option,
                                  const QModelIndex &index)
{
    // Separator +/- button: single-click toggles without opening edit.
    if (event->type() == QEvent::MouseButtonRelease &&
        index.data(ModRole::ItemType).toString() == ItemType::Separator)
    {
        auto *me = static_cast<QMouseEvent *>(event);
        if (me->button() == Qt::LeftButton &&
            separatorCollapseRect(option).contains(me->pos()))
        {
            emit separatorCollapseToggleClicked(index);
            return true;
        }
    }

    // Update-available down-arrow: left-click starts the update (re-install
    // via fetchModFiles). Without this hook the icon was just decorative.
    if (event->type() == QEvent::MouseButtonRelease &&
        index.data(ModRole::ItemType).toString() == ItemType::Mod &&
        index.data(ModRole::UpdateAvailable).toBool())
    {
        auto *me = static_cast<QMouseEvent *>(event);
        if (me->button() == Qt::LeftButton) {
            // Recompute statusX (right edge of name zone) the same way paint()
            // does, since updateIconRect places the icon off it.
            const int videoW    = m_colVis.videoReview ? m_colVis.wVideoReview : 0;
            const int sizeW     = m_colVis.size    ? m_colVis.wSize    : 0;
            const int annotW    = m_colVis.annot   ? m_colVis.wAnnot   : 0;
            const int relTimeW  = m_colVis.relTime ? m_colVis.wRelTime : 0;
            const int dateW     = m_colVis.date    ? m_colVis.wDate    : 0;
            const int statusW   = m_colVis.status  ? m_colVis.wStatus  : 0;
            const int statusX   = option.rect.right() - videoW - sizeW - annotW - relTimeW - dateW - statusW;
            if (updateIconRect(option, statusX).contains(me->pos())) {
                emit updateArrowClicked(index);
                return true;
            }
        }
    }
    // Favourite star: left-click toggles the flag.
    if (event->type() == QEvent::MouseButtonRelease &&
        index.data(ModRole::ItemType).toString() == ItemType::Mod)
    {
        auto *me = static_cast<QMouseEvent *>(event);
        if (me->button() == Qt::LeftButton) {
            const int videoW    = m_colVis.videoReview ? m_colVis.wVideoReview : 0;
            const int sizeW     = m_colVis.size    ? m_colVis.wSize    : 0;
            const int annotW    = m_colVis.annot   ? m_colVis.wAnnot   : 0;
            const int relTimeW  = m_colVis.relTime ? m_colVis.wRelTime : 0;
            const int dateW     = m_colVis.date    ? m_colVis.wDate    : 0;
            const int statusW   = m_colVis.status  ? m_colVis.wStatus  : 0;
            const int statusX   = option.rect.right() - videoW - sizeW - annotW - relTimeW - dateW - statusW;
            if (favoriteIconRect(option, statusX).contains(me->pos())) {
                emit favoriteToggleClicked(index);
                return true;
            }
        }
    }
    // Video review icon: left-click opens the URL in the browser.
    if (event->type() == QEvent::MouseButtonRelease &&
        index.data(ModRole::ItemType).toString() == ItemType::Mod &&
        m_colVis.videoReview)
    {
        auto *me = static_cast<QMouseEvent *>(event);
        if (me->button() == Qt::LeftButton) {
            const int videoW = m_colVis.wVideoReview;
            const int videoX = option.rect.right() - videoW;
            if (videoReviewIconRect(option, videoX, videoW).contains(me->pos())) {
                QString displayName =
                    index.data(ModRole::CustomName).toString();
                if (displayName.isEmpty())
                    displayName = index.data(Qt::DisplayRole).toString();
                QString url = index.data(ModRole::VideoUrl).toString();
                if (url.isEmpty()) url = video_reviews::urlFor(displayName);
                if (!url.isEmpty()) {
                    emit videoReviewClicked(url);
                    return true;
                }
            }
        }
    }
    return QStyledItemDelegate::editorEvent(event, model, option, index);
}

QSize ModListDelegate::sizeHint(const QStyleOptionViewItem &option,
                                const QModelIndex &index) const
{
    int h = option.fontMetrics.height();
    if (index.data(ModRole::ItemType).toString() == ItemType::Separator)
        return QSize(0, h + 14);
    if (index.data(ModRole::InstallStatus).toInt() == 2)
        return QSize(0, h + 16); // room for progress bar
    return QSize(0, h + 8);
}
