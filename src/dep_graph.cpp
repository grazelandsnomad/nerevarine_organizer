#include "dep_graph.h"

#include "prompts.h"
#include "settings.h"
#include "translator.h"

#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsView>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollBar>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace dep_graph {
namespace {

// Matches the list view so the canvas reads as the same app: green is "is
// depended upon", blue is "depends on" (src/modlistdelegate.cpp).
const QColor kDepGreen(30, 170, 70);
const QColor kUserBlue(70, 110, 220);

constexpr qreal kNodeW = 190.0;
constexpr qreal kNodeH = 44.0;
constexpr qreal kGapX  = 40.0;
constexpr qreal kGapY  = 90.0;

class NodeItem;

// One arrow. Recomputed whenever either end moves; owns no state beyond its
// endpoints so a moved node cannot leave a stale line behind.
class EdgeItem : public QGraphicsPathItem {
public:
    EdgeItem(NodeItem *from, NodeItem *to) : m_from(from), m_to(to)
    {
        setZValue(-1);
        setFlag(QGraphicsItem::ItemIsSelectable, true);
        setPen(QPen(kUserBlue, 2));
    }
    NodeItem *from() const { return m_from; }
    NodeItem *to()   const { return m_to; }
    void refresh();

protected:
    void paint(QPainter *p, const QStyleOptionGraphicsItem *o,
               QWidget *w) override
    {
        // Selected edges go green and thick: the only way to see which arrow
        // Delete would remove.
        QPen pen(isSelected() ? kDepGreen : kUserBlue, isSelected() ? 3.0 : 2.0);
        setPen(pen);
        QGraphicsPathItem::paint(p, o, w);
    }

private:
    NodeItem *m_from;
    NodeItem *m_to;
};

class NodeItem : public QGraphicsObject {
    Q_OBJECT
public:
    NodeItem(int nodeIdx, const deps::GraphNode &n) : m_idx(nodeIdx), m_node(n)
    {
        setFlags(QGraphicsItem::ItemIsMovable
                 | QGraphicsItem::ItemIsSelectable
                 | QGraphicsItem::ItemSendsGeometryChanges);
        setToolTip(tipFor(n));
    }

    int nodeIndex() const { return m_idx; }
    const deps::GraphNode &node() const { return m_node; }
    QList<EdgeItem *> edges;

    QRectF boundingRect() const override
    { return QRectF(0, 0, kNodeW, kNodeH); }

    void paint(QPainter *p, const QStyleOptionGraphicsItem *,
               QWidget *) override
    {
        p->setRenderHint(QPainter::Antialiasing, true);
        QPainterPath path;
        path.addRoundedRect(boundingRect().adjusted(1, 1, -1, -1), 7, 7);

        // A ghost (a dependency matching no row) and an uninstalled or disabled
        // row are three different problems, and the canvas is the place they
        // finally become visible at once.
        QColor fill = QApplication::palette().color(QPalette::Base);
        QColor line = QApplication::palette().color(QPalette::Mid);
        Qt::PenStyle style = Qt::SolidLine;
        if (m_node.ghost) {
            fill = QColor(120, 60, 60, 60);
            line = QColor(200, 90, 90);
            style = Qt::DashLine;
        } else if (!m_node.installed) {
            line = QColor(200, 150, 60);
            style = Qt::DashLine;
        } else if (!m_node.enabled) {
            fill = QApplication::palette().color(QPalette::AlternateBase);
            line = QColor(150, 150, 150);
        } else if (m_node.isUtility) {
            fill = QColor(44, 47, 58);
            line = QColor(255, 215, 0);
        }
        if (isSelected()) line = kDepGreen;

        p->setBrush(fill);
        p->setPen(QPen(line, isSelected() ? 2.5 : 1.5, style));
        p->drawPath(path);

        QColor text = QApplication::palette().color(QPalette::Text);
        if (m_node.isUtility && m_node.installed && m_node.enabled)
            text = QColor(255, 215, 0);
        p->setPen(text);
        QFont f = p->font();
        f.setBold(true);
        f.setPointSizeF(f.pointSizeF() - 0.5);
        p->setFont(f);
        const QRectF r = boundingRect().adjusted(9, 4, -9, -4);
        p->drawText(r, Qt::AlignVCenter | Qt::AlignLeft,
                    p->fontMetrics().elidedText(m_node.label, Qt::ElideRight,
                                                int(r.width())));
    }

signals:
    void wantsEdgeTo(NodeItem *target);

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant &v) override
    {
        if (change == ItemPositionHasChanged)
            for (EdgeItem *e : edges) e->refresh();
        return QGraphicsObject::itemChange(change, v);
    }

    // Shift-drag starts an edge instead of moving the node. A modifier rather
    // than a handle: the boxes are small, and a handle would be a click target
    // competing with the drag the user does far more often.
    void mousePressEvent(QGraphicsSceneMouseEvent *ev) override
    {
        if (ev->modifiers() & Qt::ShiftModifier) {
            m_linking = true;
            ev->accept();
            return;
        }
        QGraphicsObject::mousePressEvent(ev);
    }
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *ev) override
    {
        if (m_linking) {
            m_linking = false;
            NodeItem *target = nullptr;
            const auto hits = scene()->items(ev->scenePos());
            for (QGraphicsItem *it : hits)
                if (auto *n = dynamic_cast<NodeItem *>(it); n && n != this) {
                    target = n;
                    break;
                }
            if (target) emit wantsEdgeTo(target);
            ev->accept();
            return;
        }
        QGraphicsObject::mouseReleaseEvent(ev);
    }

private:
    static QString tipFor(const deps::GraphNode &n)
    {
        if (n.ghost)
            return T("depgraph_tip_ghost").arg(n.ghostUrl);
        QStringList bits;
        if (!n.installed)     bits << T("depgraph_tip_not_installed");
        else if (!n.enabled)  bits << T("depgraph_tip_disabled");
        if (!n.canBeTarget)   bits << T("depgraph_tip_no_url");
        return bits.isEmpty() ? n.label
                              : n.label + QStringLiteral("\n") + bits.join('\n');
    }

    int             m_idx;
    deps::GraphNode m_node;
    bool            m_linking = false;
};

void EdgeItem::refresh()
{
    const QPointF a = m_from->pos() + QPointF(kNodeW / 2, kNodeH / 2);
    const QPointF b = m_to->pos()   + QPointF(kNodeW / 2, kNodeH / 2);

    QPainterPath p(a);
    p.lineTo(b);

    // Arrow head at the dependency end, so the line reads "needs".
    const QLineF l(a, b);
    if (l.length() > 1.0) {
        const qreal ang = std::atan2(-l.dy(), l.dx());
        // Stop short of the box so the head is not buried under it.
        const QPointF tip = b - QPointF(std::cos(ang) * 26.0, -std::sin(ang) * 26.0);
        const qreal sz = 9.0;
        const QPointF p1 = tip - QPointF(std::cos(ang - M_PI / 7) * sz,
                                         -std::sin(ang - M_PI / 7) * sz);
        const QPointF p2 = tip - QPointF(std::cos(ang + M_PI / 7) * sz,
                                         -std::sin(ang + M_PI / 7) * sz);
        p.moveTo(tip); p.lineTo(p1);
        p.moveTo(tip); p.lineTo(p2);
    }
    setPath(p);
}

// Wheel zoom, because a 30-node graph does not fit a dialog at 1:1, and
// middle-button drag to shove the canvas around.
//
// The middle button rather than the left one because the left button is
// already spoken for three times over: it drags a node to arrange it,
// shift-drags one node onto another to add a dependency, and rubber-bands a
// selection on empty space. Qt's own ScrollHandDrag would have taken the
// first of those, so the panning is done by hand.
class GraphView : public QGraphicsView {
public:
    using QGraphicsView::QGraphicsView;
protected:
    void wheelEvent(QWheelEvent *ev) override
    {
        if (ev->modifiers() & Qt::ControlModifier) {
            const qreal f = ev->angleDelta().y() > 0 ? 1.15 : 1.0 / 1.15;
            scale(f, f);
            ev->accept();
            return;
        }
        QGraphicsView::wheelEvent(ev);
    }

    void mousePressEvent(QMouseEvent *ev) override
    {
        if (ev->button() == Qt::MiddleButton) {
            m_panFrom = ev->position().toPoint();
            m_panning = true;
            // The cursor is the only thing that says a drag has started: no
            // node moves, no rubber band appears.
            viewport()->setCursor(Qt::ClosedHandCursor);
            ev->accept();
            return;
        }
        QGraphicsView::mousePressEvent(ev);
    }

    void mouseMoveEvent(QMouseEvent *ev) override
    {
        if (m_panning) {
            // Scroll by the delta rather than jumping to an absolute
            // position, so the point under the cursor stays under it however
            // far the drag wanders outside the viewport.
            const QPoint now   = ev->position().toPoint();
            const QPoint delta = now - m_panFrom;
            m_panFrom = now;
            horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
            verticalScrollBar()->setValue(verticalScrollBar()->value()   - delta.y());
            ev->accept();
            return;
        }
        QGraphicsView::mouseMoveEvent(ev);
    }

    void mouseReleaseEvent(QMouseEvent *ev) override
    {
        if (m_panning && ev->button() == Qt::MiddleButton) {
            m_panning = false;
            viewport()->unsetCursor();
            ev->accept();
            return;
        }
        QGraphicsView::mouseReleaseEvent(ev);
    }

    // A drag that leaves the window and has the button released out there
    // never sends a release here, and the view would stay stuck in panning.
    void leaveEvent(QEvent *ev) override
    {
        if (m_panning && !(QApplication::mouseButtons() & Qt::MiddleButton)) {
            m_panning = false;
            viewport()->unsetCursor();
        }
        QGraphicsView::leaveEvent(ev);
    }

private:
    bool   m_panning = false;
    QPoint m_panFrom;
};

} // namespace
} // namespace dep_graph

#include "dep_graph.moc"

namespace dep_graph {

Result show(QWidget *parent,
            const deps::Graph &graph,
            const QHash<int, QString> &rowUrl,
            const QString &layoutKey)
{
    Result result;

    // Working copy of the edges; edits mutate this and are only turned back
    // into DependsOn lists on accept.
    deps::Graph g = graph;

    QDialog dlg(parent);
    dlg.setWindowTitle(T("depgraph_title"));
    dlg.resize(1000, 680);

    auto *outer = new QVBoxLayout(&dlg);
    auto *hint = new QLabel(T("depgraph_hint"), &dlg);
    hint->setWordWrap(true);
    outer->addWidget(hint);

    auto *row = new QHBoxLayout;
    outer->addLayout(row, 1);

    auto *scene = new QGraphicsScene(&dlg);
    auto *view  = new GraphView(scene, &dlg);
    view->setRenderHint(QPainter::Antialiasing, true);
    view->setDragMode(QGraphicsView::RubberBandDrag);
    row->addWidget(view, 1);

    // Side panel: which section of the list to draw.
    //
    // This replaced a list of "mods not shown". As a modlist grows the whole
    // web stops being readable - and a separator is the grouping the user
    // already maintains by hand, so it is the one the canvas scopes by.
    auto *side = new QVBoxLayout;
    side->addWidget(new QLabel(T("depgraph_section"), &dlg));
    auto *sectionBox = new QComboBox(&dlg);
    sectionBox->setMinimumWidth(210);
    sectionBox->setMaximumWidth(230);
    side->addWidget(sectionBox);
    auto *legend = new QLabel(&dlg);
    legend->setWordWrap(true);
    legend->setMaximumWidth(230);
    side->addWidget(legend);
    side->addStretch(1);
    row->addLayout(side);

    auto *status = new QLabel(&dlg);
    status->setWordWrap(true);
    outer->addWidget(status);

    QList<NodeItem *> items;          // index-parallel with g.nodes; null = off canvas
    items.resize(g.nodes.size());
    QList<EdgeItem *> edgeItems;

    // -- section list ---------------------------------------------------
    //
    // In first-appearance order, which is list order: the combo then reads
    // top-to-bottom like the modlist itself.
    QStringList sections;
    QHash<QString, int> sectionCount;
    for (const deps::GraphNode &n : g.nodes) {
        if (n.ghost) continue;
        if (!sectionCount.contains(n.section)) sections << n.section;
        sectionCount[n.section] += 1;
    }


    // -- saved positions -------------------------------------------------
    //
    // Keyed by SECTION plus node, not by node alone: the same mod sits in a
    // different place in the whole-list diagram than in its own section's, and
    // sharing one position between the two views drags each out of shape.
    // Rows key on modPath (unique, unlike nexusUrl), ghosts on their URL.
    QHash<QString, QPointF> saved;
    QString startSection;
    const QString gameId = layoutKey.section(QLatin1Char('\x1f'), 0, 0);
    const QString prof   = layoutKey.section(QLatin1Char('\x1f'), 1, 1);
    if (!layoutKey.isEmpty()) {
        const QJsonObject o = QJsonDocument::fromJson(
            Settings::depGraphLayout(gameId, prof).toUtf8()).object();
        for (auto it = o.constBegin(); it != o.constEnd(); ++it) {
            if (it.key() == QLatin1String("__section")) {
                startSection = it.value().toString();
                continue;
            }
            const QJsonObject p = it.value().toObject();
            saved.insert(it.key(), QPointF(p.value("x").toDouble(),
                                           p.value("y").toDouble()));
        }
    }
    // "all sections" and the unnamed section above the first separator are
    // both spelled with no name, so the view they belong to is tagged rather
    // than concatenated raw - otherwise their layouts would share a key.
    auto sectTag = [](const QString &sect) {
        return sect.isNull() ? QStringLiteral("*all*")
                             : QStringLiteral("s:") + sect;
    };
    // Every entry carries its tag, so "all sections" and the unnamed section
    // above the first separator stay distinguishable in the combo too.
    sectionBox->addItem(T("depgraph_section_all"), sectTag(QString()));
    for (const QString &s : sections) {
        const QString label = s.isEmpty() ? T("depgraph_section_none") : s;
        sectionBox->addItem(QStringLiteral("%1  (%2)")
                                .arg(label, QString::number(sectionCount.value(s))),
                            sectTag(s));
    }

    auto keyOf = [&](const QString &sect, const deps::GraphNode &n) {
        return sectTag(sect) + QLatin1Char('\x1e')
             + (n.ghost ? QStringLiteral("url:") + n.ghostUrl
                        : QStringLiteral("path:") + n.modPath);
    };

    // Which section the canvas currently shows; empty means all of them.
    QString curSection;
    // What the graph actually occupies, kept apart from the scene rect that
    // surrounds it - see the rebuild lambda.
    QRectF  contentRect;

    auto rememberPositions = [&] {
        for (NodeItem *n : items)
            if (n) saved.insert(keyOf(curSection, n->node()), n->pos());
    };

    // -- edge creation ---------------------------------------------------
    auto addEdgeItem = [&](int from, int to) {
        NodeItem *a = items[from];
        NodeItem *b = items[to];
        if (!a || !b) return;                 // not both on this view
        auto *e = new EdgeItem(a, b);
        a->edges.append(e);
        b->edges.append(e);
        scene->addItem(e);
        e->refresh();
        edgeItems.append(e);
    };

    // linkNodes is called from a node's signal, which is connected during
    // rebuild - so it has to exist first, and rebuild has to be able to hook
    // nodes to it.
    auto linkNodes = [&](NodeItem *a, NodeItem *b) {
        const int from = a->nodeIndex(), to = b->nodeIndex();
        if (!g.nodes[to].canBeTarget) {
            status->setText(T("depgraph_err_no_url").arg(g.nodes[to].label));
            return;
        }
        for (const deps::GraphEdge &e : g.edges)
            if (e.from == from && e.to == to) {
                status->setText(T("depgraph_err_exists")
                                    .arg(g.nodes[from].label, g.nodes[to].label));
                return;
            }
        if (deps::wouldCycle(g, from, to)) {
            status->setText(T("depgraph_err_cycle")
                                .arg(g.nodes[from].label, g.nodes[to].label));
            return;
        }
        g.edges.append({from, to});
        addEdgeItem(from, to);
        status->setText(T("depgraph_added")
                            .arg(g.nodes[from].label, g.nodes[to].label));
    };

    // -- building one view -----------------------------------------------
    //
    // What a section shows is NOT just its own mods. Measured on the author's
    // lists, 10 of 19 dependency arrows on Skyrim AE and 37 of 38 on Morrowind
    // cross a separator boundary - a mod needs a framework, and the frameworks
    // live in their own section. Drawing only the section's own mods would cut
    // every one of those arrows in half, including the KID -> MergeMapper ->
    // Address Library -> skse64 chain this canvas exists to show. So the
    // section's mods are drawn solid, and whatever they point at (or is pointed
    // at by them) is pulled in from its own section and drawn faded.
    auto rebuild = [&](const QString &sect) {
        rememberPositions();
        scene->clear();               // deletes every node and edge item
        items.fill(nullptr);
        edgeItems.clear();
        curSection = sect;

        QSet<int> core;               // solid: what the selection asked for
        if (sect.isNull()) {
            // Whole list: only the mods actually in a dependency. Of 382 mods
            // on the author's Morrowind list that is 30; drawing all of them
            // would be useless.
            for (const deps::GraphEdge &e : g.edges) { core.insert(e.from); core.insert(e.to); }
        } else {
            // Every mod of the section, edges or not, so one with no
            // dependency yet can still be shift-dragged onto another.
            for (int i = 0; i < g.nodes.size(); ++i)
                if (!g.nodes[i].ghost && g.nodes[i].section == sect) core.insert(i);
        }

        QSet<int> visible = core;
        for (const deps::GraphEdge &e : g.edges) {
            if (core.contains(e.from)) visible.insert(e.to);
            if (core.contains(e.to))   visible.insert(e.from);
        }

        // Layered layout over what is visible. Two things matter at real sizes:
        //
        //   * A layer can be WIDE. On the author's Morrowind list 24 mods all
        //     depend on OAAB Data, and one row of 24 boxes is 5500px across -
        //     the diagram became a fan of arrows arriving from off-screen. Wide
        //     layers wrap into several rows inside their own band.
        //
        //   * Within a layer, order by the average column of what each node
        //     points at (one barycentre pass), so arrows run as straight as
        //     they can instead of crossing the whole canvas.
        constexpr int kPerRow = 6;
        const QList<int> layer = deps::layerOf(g);
        QHash<int, QPointF> placed;
        {
            QHash<int, QList<int>> out;
            for (const deps::GraphEdge &e : g.edges)
                if (visible.contains(e.from) && visible.contains(e.to))
                    out[e.from].append(e.to);

            int maxLayer = 0;
            for (int i : visible) maxLayer = qMax(maxLayer, layer.value(i, 0));

            QHash<int, int> column;      // for the barycentre of the layer above
            qreal y = 0.0;               // grows negative going up
            for (int L = 0; L <= maxLayer; ++L) {
                QList<int> here;
                for (int i : visible) if (layer.value(i, 0) == L) here << i;
                if (here.isEmpty()) continue;

                if (L > 0) {
                    std::sort(here.begin(), here.end(), [&](int a, int b) {
                        auto bary = [&](int n) {
                            double sum = 0; int cnt = 0;
                            for (int m : out.value(n))
                                if (column.contains(m)) { sum += column.value(m); ++cnt; }
                            return cnt ? sum / cnt : 1e9;   // unanchored goes last
                        };
                        const double ba = bary(a), bb = bary(b);
                        if (ba != bb) return ba < bb;
                        return a < b;                        // stable tie-break
                    });
                } else {
                    std::sort(here.begin(), here.end());
                }

                const int rows = (here.size() + kPerRow - 1) / kPerRow;
                for (int k = 0; k < here.size(); ++k) {
                    const int col = k % kPerRow;
                    const int sub = k / kPerRow;
                    column.insert(here[k], col);
                    placed.insert(here[k],
                                  QPointF(col * (kNodeW + kGapX),
                                          y - sub * (kNodeH + 18.0)));
                }
                // Band height for this layer, then the gap to the next one up.
                y -= (rows - 1) * (kNodeH + 18.0) + kNodeH + kGapY;
            }
        }

        QList<int> ordered(visible.constBegin(), visible.constEnd());
        std::sort(ordered.begin(), ordered.end());
        for (int i : ordered) {
            auto *n = new NodeItem(i, g.nodes[i]);
            const QString k = keyOf(sect, g.nodes[i]);
            n->setPos(saved.contains(k) ? saved.value(k)
                                        : placed.value(i, QPointF(0, 0)));
            // Faded means "not in this section, shown because an arrow reaches
            // it". Drawn rather than hidden so the chain stays whole.
            if (!core.contains(i)) n->setOpacity(0.45);
            scene->addItem(n);
            items[i] = n;
            QObject::connect(n, &NodeItem::wantsEdgeTo, &dlg,
                             [&, n](NodeItem *target) { linkNodes(n, target); });
        }

        for (const deps::GraphEdge &e : g.edges)
            if (visible.contains(e.from) && visible.contains(e.to))
                addEdgeItem(e.from, e.to);

        legend->setText(sect.isNull()
            ? T("depgraph_shown_all").arg(QString::number(core.size()),
                                          QString::number(edgeItems.size()))
            : T("depgraph_shown").arg(QString::number(core.size()),
                                      QString::number(visible.size() - core.size()),
                                      QString::number(edgeItems.size())));

        // Two rectangles, deliberately: the content is what the view is fitted
        // to, and the scene is that plus room to drag it around. Without the
        // slack a fitted graph has no scroll range at all, so a middle-button
        // pan would move nothing until the user had zoomed in - which is
        // indistinguishable from the pan not working.
        contentRect = scene->itemsBoundingRect().adjusted(-80, -80, 80, 80);
        const qreal padX = qBound(400.0, contentRect.width(),  2500.0);
        const qreal padY = qBound(400.0, contentRect.height(), 2500.0);
        view->setSceneRect(contentRect.adjusted(-padX, -padY, padX, padY));
    };

    // Fitting has to wait for the view to have its real size: fitInView before
    // exec() scales against the default sizeHint, not the 1000px the dialog is
    // about to become, which shrank a 30-node graph to a smudge.
    auto fitNow = [view, &contentRect] {
        // The content, not the scene: the scene is padded so the canvas can be
        // dragged, and fitting THAT would shrink the graph into the middle of
        // a lot of empty space.
        const QRectF r = contentRect;
        if (r.isEmpty()) return;
        view->resetTransform();
        view->fitInView(r, Qt::KeepAspectRatio);
        // Never magnify: a two-node graph blown up to fill the dialog looks
        // broken, and Ctrl+wheel is there for a closer look.
        if (view->transform().m11() > 1.0) view->resetTransform();
    };

    // A section whose name is empty ("above the first separator") is a real
    // choice, so the sentinel for "all sections" is a NULL string, not an
    // empty one, and the tag is what travels through the combo.
    auto untag = [](const QString &tag) {
        return tag.startsWith(QLatin1String("s:")) ? tag.mid(2) : QString();
    };
    QObject::connect(sectionBox, &QComboBox::currentIndexChanged, &dlg, [&](int i) {
        rebuild(untag(sectionBox->itemData(i).toString()));
        fitNow();
        status->clear();
    });

    // Reopen on the section last looked at rather than always the whole list.
    int startIdx = 0;
    if (!startSection.isEmpty())
        startIdx = qMax(0, sectionBox->findData(startSection));
    if (startIdx != 0) sectionBox->setCurrentIndex(startIdx);   // rebuilds
    else               rebuild(QString());

    // Delete removes the selected arrow. Nodes are never deleted here - a node
    // is a mod, and this canvas edits dependencies, not the modlist.
    auto *delBtn = new QPushButton(T("depgraph_delete_edge"), &dlg);
    side->insertWidget(side->count() - 1, delBtn);
    QObject::connect(delBtn, &QPushButton::clicked, &dlg, [&] {
        int removed = 0;
        for (EdgeItem *e : QList<EdgeItem *>(edgeItems)) {
            if (!e->isSelected()) continue;
            const int from = e->from()->nodeIndex(), to = e->to()->nodeIndex();
            for (int i = 0; i < g.edges.size(); ++i)
                if (g.edges[i].from == from && g.edges[i].to == to) {
                    g.edges.removeAt(i);
                    break;
                }
            e->from()->edges.removeAll(e);
            e->to()->edges.removeAll(e);
            edgeItems.removeAll(e);
            scene->removeItem(e);
            delete e;
            ++removed;
        }
        status->setText(removed ? T("depgraph_removed").arg(removed)
                                : T("depgraph_select_edge"));
    });

    auto *box = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel,
                                     &dlg);
    box->button(QDialogButtonBox::Save)->setText(T("depgraph_apply"));
    QObject::connect(box, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    outer->addWidget(box);

    QTimer::singleShot(0, view, fitNow);

    const int rc = dlg.exec();

    // Positions are remembered whichever way the dialog closed: where the user
    // dragged a box is not a pending edit, it is how they want to see it.
    if (!layoutKey.isEmpty()) {
        rememberPositions();
        QJsonObject o;
        for (auto it = saved.constBegin(); it != saved.constEnd(); ++it) {
            QJsonObject p;
            p.insert("x", it.value().x());
            p.insert("y", it.value().y());
            o.insert(it.key(), p);
        }
        o.insert("__section", sectTag(curSection));
        Settings::setDepGraphLayout(gameId, prof,
                                    QString::fromUtf8(
                                        QJsonDocument(o).toJson(QJsonDocument::Compact)));
    }

    if (rc != QDialog::Accepted) return result;

    // Turn the edited edges back into DependsOn lists. Only rows whose set
    // actually differs are reported, so the caller writes as little as
    // possible - and a run with no changes pushes no undo step.
    QHash<int, QStringList> built;
    for (const deps::GraphEdge &e : g.edges) {
        const int fromRow = g.nodes[e.from].idx;
        if (fromRow < 0) continue;                   // a ghost cannot declare
        const QString url = g.nodes[e.to].ghost ? g.nodes[e.to].ghostUrl
                                                : rowUrl.value(g.nodes[e.to].idx);
        if (url.isEmpty()) continue;
        auto &list = built[fromRow];
        if (!list.contains(url)) list << url;
    }
    // Every row that had edges before must appear, even if it now has none, or
    // a removal would look like "no change".
    for (const deps::GraphEdge &e : graph.edges) {
        const int fromRow = graph.nodes[e.from].idx;
        if (fromRow >= 0 && !built.contains(fromRow)) built.insert(fromRow, {});
    }

    for (auto it = built.constBegin(); it != built.constEnd(); ++it) {
        QStringList before;
        for (const deps::GraphEdge &e : graph.edges) {
            if (graph.nodes[e.from].idx != it.key()) continue;
            const QString u = graph.nodes[e.to].ghost
                                  ? graph.nodes[e.to].ghostUrl
                                  : rowUrl.value(graph.nodes[e.to].idx);
            if (!u.isEmpty() && !before.contains(u)) before << u;
        }
        QStringList after = it.value();
        before.sort();
        QStringList sortedAfter = after;
        sortedAfter.sort();
        if (before != sortedAfter) {
            result.dependsOn.insert(it.key(), after);
            result.changed = true;
        }
    }
    return result;
}

} // namespace dep_graph
