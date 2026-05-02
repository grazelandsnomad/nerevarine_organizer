#include "modlistdelegate.h"
#include "modroles.h"
#include "translator.h"
#include "video_reviews.h"
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

        QColor bg = index.data(ModRole::BgColor).value<QColor>();
        QColor fg = index.data(ModRole::FgColor).value<QColor>();

        // Temporary "needs attention" tint: at least one mod ANYWHERE in
        // the modlist has a pending Nexus update.  updateSectionCounts()
        // sets SepHasUpdate identically on every separator when that's
        // true so the whole scaffolding greys out - highlighting just the
        // offending section hid updates sitting in collapsed neighbours.
        // Override the user's chosen colours with a light-grey wash +
        // dark text; the stored BgColor/FgColor are NOT touched, so once
        // every offending mod is installed or removed updateSectionCounts()
        // clears the flag and the user's original palette returns.
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

        // Clickable +/- collapse toggle on the left. Drawn as a rounded square
        // with "+" (collapsed) or "−" (expanded) in the separator's fg color,
        // so it reads well on whatever bg theme the user picked.
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
        // Use plain ASCII '+' / '-' so font fallback can't ruin alignment.
        painter->drawText(btn, Qt::AlignCenter,
                          collapsed ? QStringLiteral("+") : QStringLiteral("−"));
        painter->restore();

        // Active / total mod count shown on the right.  The redundant ▲/▼
        // indicator that used to sit to its right was removed - the +/- button
        // on the left is already an unambiguous collapse affordance.
        QString countStr;
        QVariant tv = index.data(ModRole::TotalCount);
        if (tv.isValid()) {
            int active = index.data(ModRole::ActiveCount).toInt();
            int total  = tv.toInt();
            if (total > 0)
                countStr = QString("(%1/%2)").arg(active).arg(total);
        }

        // Reserve horizontal space for the count BEFORE drawing the title so
        // long separator names ("La Calle de Cozumel" was the report) don't
        // bleed into the count text on the right.  Pre-measure with the same
        // font the count will be painted with so the elision math is right.
        int countReserve = 0;
        if (!countStr.isEmpty()) {
            QFont cf = painter->font();
            cf.setBold(false);
            cf.setPointSize(qMax(cf.pointSize() - 1, 7));
            countReserve = QFontMetrics(cf).horizontalAdvance(countStr) + 16;
        }

        // Title starts after the +/- button and stops before the count zone.
        QRect textRect = option.rect.adjusted(
            btn.right() - option.rect.left() + 8, 0,
            -12 - countReserve, 0);
        // Elide overflow rather than letting Qt clip mid-glyph - the user
        // still sees the full name in the row's tooltip.
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

    // -- Mod item: dynamic right-side zones ---
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

    // 1. Paint normal item (bg, checkbox, name text) clipped to the name zone.
    //
    // Rows whose DependsOn resolves to another mod in the list get shifted
    // right by one indent step so the visual hierarchy matches the logical
    // parent-child relationship.  A "↳" glyph goes in the reclaimed gutter.
    // The checkbox moves with the rect, which is the right behaviour - a
    // toggle on a child row still toggles the child independently of its
    // parent.
    const bool indented = index.data(ModRole::HasInListDependency).toBool();
    const int indentPx  = indented ? 16 : 0;

    int installStatus = index.data(ModRole::InstallStatus).toInt();
    QStyleOptionViewItem nameOpt = option;
    if (installStatus == 2)
        nameOpt.rect.setBottom(nameOpt.rect.bottom() - 6); // leave room for bar
    if (indented)
        nameOpt.rect.setLeft(nameOpt.rect.left() + indentPx);

    // Utility-mod tint: a muted grey background that visually separates
    // framework / library mods (Skill Framework, OAAB_Data, MWSE shims)
    // from content mods.  Only applied when the row isn't selected so
    // the selection colour still dominates on the active row.
    // Paint the fill on the painter directly rather than via
    // backgroundBrush - QCommonStyle ignores that field on most platforms
    // and only the palette::Base route works reliably.
    const bool isUtility = index.data(ModRole::IsUtility).toBool();
    const bool selected  = (option.state & QStyle::State_Selected);
    const bool updateAvail = index.data(ModRole::UpdateAvailable).toBool();
    const QColor utilityTint(190, 190, 190);
    const QColor updateTint(30, 160, 30);

    painter->save();
    painter->setClipRect(QRect(option.rect.left(), option.rect.top(),
                               statusX - option.rect.left(), option.rect.height()));
    if (updateAvail && !selected) {
        // Strong green wash so rows with a pending update are impossible to
        // miss; text forced white for contrast.
        painter->fillRect(option.rect, updateTint);
        nameOpt.palette.setColor(QPalette::Base,            updateTint);
        nameOpt.palette.setColor(QPalette::AlternateBase,   updateTint);
        nameOpt.palette.setColor(QPalette::Window,          updateTint);
        nameOpt.palette.setColor(QPalette::Text,            Qt::white);
        nameOpt.palette.setColor(QPalette::WindowText,      Qt::white);
        nameOpt.palette.setColor(QPalette::HighlightedText, Qt::white);
        nameOpt.backgroundBrush = QBrush(updateTint);
    } else if (isUtility && !selected) {
        // Fill the full visible row (name zone) with the tint so the
        // underlying alternating-row colour doesn't bleed through.
        painter->fillRect(option.rect, utilityTint);
        // Also force the palette so the base delegate's own fill uses
        // the tint for non-text chrome (checkbox area padding, etc.).
        nameOpt.palette.setColor(QPalette::Base,          utilityTint);
        nameOpt.palette.setColor(QPalette::AlternateBase, utilityTint);
        nameOpt.palette.setColor(QPalette::Window,        utilityTint);
        nameOpt.backgroundBrush = QBrush(utilityTint);
    }
    QStyledItemDelegate::paint(painter, nameOpt, index);
    painter->restore();

    if (indented) {
        painter->save();
        // Dimmed arrow in the gutter; no theme-aware colour here because
        // the delegate already paints several hard-coded accents.
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

    // 1a. Dependency highlight: bold full-row tint + thick bookended stripes.
    //     1 = this item is a dependency of the selected mod  → green
    //     2 = this item uses the selected mod               → blue/purple
    //
    // The green tint in particular is now the "impossible to miss"
    // signal that surfaces required-or-used relationships - a muted
    // 35-alpha wash was too easy to scroll past.  Alpha bumped to
    // ~115 (≈45% opacity over the row); stripes widened to 6 px and
    // doubled onto both edges so the row reads as a bracketed band.
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

    // 1b. Update-available icon: a big green downward-pointing triangle
    // (slot 0, just left of the status column).  Clicking it routes to
    // MainWindow::onInstallFromNexus via ModListDelegate::updateArrowClicked.
    if (index.data(ModRole::UpdateAvailable).toBool()) {
        QRect iconRect = updateIconRect(option, statusX);
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setPen(Qt::NoPen);
        // White on the strong-green row fill so it stays visible; on a
        // selected row the selection highlight provides its own contrast.
        painter->setBrush(Qt::white);

        const int cx = iconRect.center().x();
        QPolygon tri;
        tri << QPoint(iconRect.left(),  iconRect.top())
            << QPoint(iconRect.right(), iconRect.top())
            << QPoint(cx,               iconRect.bottom());
        painter->drawPolygon(tri);
        painter->restore();
    }

    // 1c. Conflict warning icon: orange triangle with ! (slot 1, left of update slot).
    //     Only rendered when the row is currently selected - otherwise an
    //     orange triangle on every mod that happens to share a file with
    //     some other mod turns the list into a wall of warnings and
    //     distracts from everything else.  The click-to-reveal idiom
    //     matches how MO2 surfaces overwrites on demand.
    if (index.data(ModRole::HasConflict).toBool()
     && (option.state & QStyle::State_Selected)) {
        QRect iconRect = conflictIconRect(option, statusX);
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(210, 100, 0)); // orange

        int cx  = iconRect.center().x();
        int top = iconRect.top();
        int bot = iconRect.bottom();
        int w   = iconRect.width();
        QPolygon tri;
        tri << QPoint(cx, top) << QPoint(cx - w / 2, bot) << QPoint(cx + w / 2, bot);
        painter->drawPolygon(tri);

        // White exclamation mark
        painter->setPen(QPen(Qt::white, 1.5));
        painter->setFont([&]() {
            QFont f = option.font;
            f.setBold(true);
            f.setPointSize(qMax(f.pointSize() - 2, 6));
            return f;
        }());
        painter->drawText(QRect(cx - w / 2, top + w / 4, w, iconRect.height()),
                          Qt::AlignHCenter | Qt::AlignVCenter, "!");
        painter->restore();
    }

    // 1d. Missing-master icon: red diamond with ? (slot 2)
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

    // 1f. Favourite star (slot 4): gold ★ when favourited, dim ☆ on hover/select only.
    {
        const bool isFav  = index.data(ModRole::IsFavorite).toBool();
        const bool hovered = option.state & QStyle::State_MouseOver;
        if (isFav || hovered || selected) {
            QRect iconRect = favoriteIconRect(option, statusX);
            painter->save();
            painter->setRenderHint(QPainter::TextAntialiasing, true);
            QFont starFont = option.font;
            // Bump the glyph size so the star actually fills the enlarged
            // 22 px slot (previously 11 pt, which looked tiny next to the
            // larger rect).
            starFont.setPointSize(qMax(option.font.pointSize() + 4, 16));
            painter->setFont(starFont);
            if (isFav) {
                painter->setPen(QColor(220, 170, 0));   // full gold
                painter->drawText(iconRect, Qt::AlignCenter, QStringLiteral("★"));
            } else {
                // Ghost star: gold preview at moderate opacity so it's clearly
                // visible on both normal rows and selection-highlight backgrounds.
                // (alpha 55 ≈ 21% was virtually invisible, especially when selected.)
                QColor ghost(220, 170, 0, hovered ? 180 : 110);
                painter->setPen(ghost);
                painter->drawText(iconRect, Qt::AlignCenter, QStringLiteral("☆"));
            }
            painter->restore();
        }
    }

    // 1e. Missing-dependency icon: yellow circle with ! (slot 3)
    if (index.data(ModRole::HasMissingDependency).toBool()) {
        QRect iconRect = depIconRect(option, statusX);
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(230, 185, 40)); // amber/yellow
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

    // 2. Fill background for all right zones (matches selection state)
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

    // 2b. Progress bar for downloading/extracting items
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
            // Extracting - animated indeterminate stripe, phase-shifted per row
            // so concurrent installs don't all show the same position.
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

    // 3. Status column
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

    // 4. Date column
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

    // 5. Relative-time column
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

    // 6. Annotation column
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

    // 7. Size column - color-coded by footprint (>=1 GB red, >=256 MB amber, else green)
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
                color = QColor(200, 50, 50);     // red: >1 GB
            } else if (bytes >= 256 * (qint64)MB) {
                text  = QString::number(bytes / MB, 'f', 0) + " MB";
                color = QColor(200, 150, 30);    // amber: 256 MB .. 1 GB
            } else if (bytes >= (qint64)MB) {
                text  = QString::number(bytes / MB, 'f', 0) + " MB";
                color = QColor(40, 140, 40);     // green: < 256 MB
            } else {
                text  = QString::number(bytes / 1024.0, 'f', 0) + " KB";
                color = QColor(40, 140, 40);
            }
        } else if (index.data(ModRole::InstallStatus).toInt() != 1) {
            text = QString(QChar(0x2014)); // em-dash for not-installed
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

    // 8. Video-review column - paints a 📺 glyph when a review URL is known
    //    for this mod.  Lookup lives in video_reviews.h (hardcoded table).
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

// Returns the rect for the update-available icon within the name zone.
// statusX is the left edge of the status column.
// Slot 0 (rightmost): update arrow.  Wider than the other slots so the
// downward triangle reads as a proper "download" glyph and the click
// target is easier to hit.  18px is the max width that still leaves the
// rest of the icon strip (slots 1-3 at 14px each, 4px gaps) alignment-safe.
QRect ModListDelegate::updateIconRect(const QStyleOptionViewItem &option, int statusX) const
{
    const int iconW = 18;
    const int iconH = 14;
    int x = statusX - iconW - 4;
    int y = option.rect.top() + (option.rect.height() - iconH) / 2;
    return QRect(x, y, iconW, iconH);
}

// Slot 1 (one step left of slot 0): conflict warning triangle.
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

// Slot 4: favourite star.  Drawn noticeably larger than the other icons
// because it's the most visually-scanned marker in the list (users glance
// over the stars to locate their favourites), and because emoji/glyph
// stars render poorly at 14 px on most fonts.
QRect ModListDelegate::favoriteIconRect(const QStyleOptionViewItem &option, int statusX) const
{
    const int iconW = 22;
    const int iconH = 22;
    // Keep the star's right edge at the slot-4 boundary used by the other
    // icons (statusX - 4*(14+4) = statusX - 72) so it doesn't overlap slot 3.
    int x = statusX - 4 * (14 + 4) - iconW - 2;
    int y = option.rect.top() + (option.rect.height() - iconH) / 2;
    return QRect(x, y, iconW, iconH);
}

// Video-review icon lives centred inside its dedicated column.  videoX is
// the left edge, videoW the width; returns a square roughly as tall as
// the row minus 4px padding so the 📺 glyph doesn't clip.
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

        if (index.data(ModRole::HasConflict).toBool()) {
            QRect iconRect = conflictIconRect(option, statusX);
            if (iconRect.contains(event->pos())) {
                // Each entry: "DisplayName\tfile1\tfile2\t...\t[…+N more]"
                QStringList entries = index.data(ModRole::ConflictsWith).toStringList();
                QString tip = tr("File conflicts:");
                for (const QString &entry : entries) {
                    QStringList parts = entry.split('\t');
                    QString modName = parts.value(0);
                    QStringList files = parts.mid(1);
                    tip += "\n\n";
                    tip += QString("  %1  (%2 shared file(s)):").arg(modName).arg(files.size());
                    for (const QString &f : files)
                        tip += "\n    " + f;
                }
                QToolTip::showText(event->globalPos(), tip, view);
                return true;
            }
        }

        if (index.data(ModRole::HasMissingMaster).toBool()) {
            QRect iconRect = masterIconRect(option, statusX);
            if (iconRect.contains(event->pos())) {
                // Each entry: "plugin.esp\tmaster1.esm\tmaster2.esm"
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
    // Separator +/- collapse button: single-click toggles without opening edit.
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

    // Update-available down-arrow: left-click starts the update flow
    // (re-install via the existing fetchModFiles path).  Without this
    // hook the icon was purely decorative.
    if (event->type() == QEvent::MouseButtonRelease &&
        index.data(ModRole::ItemType).toString() == ItemType::Mod &&
        index.data(ModRole::UpdateAvailable).toBool())
    {
        auto *me = static_cast<QMouseEvent *>(event);
        if (me->button() == Qt::LeftButton) {
            // updateIconRect uses `statusX` (the right edge of the name zone)
            // to place the icon; recompute it the same way paint() does.
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
    // Favourite star: left-click toggles the favourite flag.
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
    // Video review icon: left-click opens the review URL in the browser.
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
        return QSize(0, h + 16); // extra room for progress bar
    return QSize(0, h + 8);
}
