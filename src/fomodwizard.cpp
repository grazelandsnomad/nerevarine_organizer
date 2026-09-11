#include "fomodwizard.h"
#include "fomod_copy.h"
#include "fomod_hint.h"
#include "mod_aliases.h"
#include "mod_match.h"
#include "fomod_path.h"
#include "fomod_scripts.h"
#include "translator.h"

#include <QAbstractButton>
#include <QBoxLayout>
#include <QButtonGroup>
#include <QCheckBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGroupBox>
#include <QHash>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QScrollBar>
#include <QApplication>
#include <QSet>
#include <QStackedWidget>
#include <QEvent>
#include <QGuiApplication>
#include <QScreen>
#include <QMouseEvent>
#include <QXmlStreamReader>

QString FomodWizard::findModuleConfig(const QString &archiveRoot)
{
    QDir root(archiveRoot);
    for (const QString &d : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (d.compare("fomod", Qt::CaseInsensitive) != 0) continue;
        QDir fomodDir(root.filePath(d));
        for (const QString &f : fomodDir.entryList(QDir::Files)) {
            if (f.compare("ModuleConfig.xml", Qt::CaseInsensitive) == 0)
                return fomodDir.filePath(f);
        }
    }
    return {};
}

// Shallowest dir at/under archiveRoot holding fomod/ModuleConfig.xml. The dive
// heuristic misses it when a Nexus wrapper folder (or a stray sibling file that
// suppresses the dive) buries fomod/ a level or two down. BFS so multi-mod packs
// pick the topmost FOMOD. Returns "" if there's no installer in the tree.
QString FomodWizard::findFomodRoot(const QString &archiveRoot)
{
    constexpr int kMaxDirs = 8192;   // runaway guard
    QStringList queue{archiveRoot};
    int visited = 0;
    while (!queue.isEmpty() && visited < kMaxDirs) {
        ++visited;
        const QString dir = queue.takeFirst();
        if (!findModuleConfig(dir).isEmpty())
            return dir;
        QDir d(dir);
        for (const QString &s : d.entryList(QDir::Dirs | QDir::NoDotAndDotDot
                                            | QDir::NoSymLinks))
            queue.append(d.filePath(s));
    }
    // Cap hit: may not have reached fomod/. Log it so a "no wizard spawned"
    // report is diagnosable instead of silent.
    if (visited >= kMaxDirs)
        qWarning("FomodWizard::findFomodRoot: scanned %d dirs under '%s' without "
                 "finding fomod/ModuleConfig.xml; giving up (archive too deep?)",
                 visited, qUtf8Printable(archiveRoot));
    return {};
}

bool FomodWizard::hasFomod(const QString &archiveRoot)
{
    return !findFomodRoot(archiveRoot).isEmpty();
}

// Installed Nexus URLs -> "game/modId" keys, the form fomod::citedMods
// results are looked up in. Unparseable URLs are dropped rather than stored
// raw: a key nothing can ever match is worse than no key.
static QSet<QString> nexusKeys(const QStringList &urls)
{
    QSet<QString> keys;
    for (const QString &u : urls) {
        const auto ref = parseNexusModUrl(u);
        if (ref) keys.insert(ref->game.toLower() + u'/' + QString::number(ref->modId));
    }
    return keys;
}

std::expected<QString, QString>
FomodWizard::run(const QString &archiveRoot,
                 const QString &priorChoices,
                 QString *outChoices,
                 QWidget *parent,
                 const QStringList &installedModNames,
                 const QString &gameId,
                 const QStringList &installedNexusUrls)
{
    FomodWizard dlg(archiveRoot, parent);
    dlg.m_priorChoices      = priorChoices;
    dlg.m_installedModNames = installedModNames;
    dlg.m_gameId            = gameId;
    dlg.m_installedNexusKeys = nexusKeys(installedNexusUrls);

    if (!dlg.parse()) {
        // Bad XML: offer a raw install
        auto ans = QMessageBox::warning(
            parent,
            T("fomod_parse_error_title"),
            T("fomod_parse_error_body"),
            QMessageBox::Ok | QMessageBox::Cancel,
            QMessageBox::Ok);
        if (ans == QMessageBox::Ok) return archiveRoot;
        return std::unexpected(QStringLiteral("cancelled"));
    }

    // No optional steps: install required files silently
    if (dlg.m_steps.isEmpty()) {
        if (dlg.m_requiredFiles.isEmpty() && dlg.m_requiredFolders.isEmpty())
            return archiveRoot; // nothing to do, normal install
        return dlg.applySelections();
    }

    dlg.buildUi();
    if (dlg.exec() != QDialog::Accepted)
        return std::unexpected(QStringLiteral("cancelled"));

    QString path = dlg.applySelections();
    if (outChoices) *outChoices = dlg.collectChoices();
    return path;
}

void FomodWizard::showAsync(
    const QString &archiveRoot,
    const QString &priorChoices,
    QWidget *parent,
    const QStringList &installedModNames,
    std::function<void(const QString &, const QString &)> onDone,
    const QString &gameId,
    const QStringList &installedNexusUrls)
{
    auto *dlg = new FomodWizard(archiveRoot, parent);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->m_priorChoices      = priorChoices;
    dlg->m_installedModNames = installedModNames;
    dlg->m_gameId            = gameId;
    dlg->m_installedNexusKeys = nexusKeys(installedNexusUrls);

    if (!dlg->parse()) {
        auto ans = QMessageBox::warning(
            parent,
            T("fomod_parse_error_title"),
            T("fomod_parse_error_body"),
            QMessageBox::Ok | QMessageBox::Cancel,
            QMessageBox::Ok);
        delete dlg;
        onDone(ans == QMessageBox::Ok ? archiveRoot : QString(), {});
        return;
    }

    if (dlg->m_steps.isEmpty()) {
        const QString result = dlg->applySelections();
        delete dlg;
        onDone(result.isEmpty() ? archiveRoot : result, {});
        return;
    }

    dlg->buildUi();
    // Top-level non-modal so multiple wizards can be open at once.
    dlg->setWindowModality(Qt::NonModal);
    dlg->setWindowFlag(Qt::Window, true);

    QObject::connect(dlg, &QDialog::accepted, dlg,
                     [dlg, onDone]() {
        const QString choices = dlg->collectChoices();
        const QString path    = dlg->applySelections();
        onDone(path, choices);
    });
    QObject::connect(dlg, &QDialog::rejected, dlg,
                     [onDone]() { onDone({}, {}); });

    dlg->show();
    dlg->raise();
    dlg->activateWindow();
}

FomodWizard::FomodWizard(const QString &archiveRoot, QWidget *parent)
    : QDialog(parent), m_archiveRoot(archiveRoot)
{
    // Caller passes the mod root, but fomod/ModuleConfig.xml may sit below it
    // (Nexus wrapper folder). Rebase onto the dir that actually holds the FOMOD
    // so source paths resolve and staging lands right. No-op if fomod/ is here
    // already or absent.
    const QString root = findFomodRoot(archiveRoot);
    if (!root.isEmpty())
        m_archiveRoot = root;

    setModal(true);
    setMinimumSize(540, 420);
}

static FomodFile parseFileAttrs(const QXmlStreamReader &xml)
{
    FomodFile f;
    f.source      = xml.attributes().value("source").toString();
    f.destination = xml.attributes().value("destination").toString();
    f.priority    = xml.attributes().value("priority").toInt();
    return f;
}

namespace {

// The full-size image window's contents: fitted to the window by default,
// the file's own pixels on click, and panned by dragging.
//
// Its own class because the gesture is stateful across three events, and the
// wizard's eventFilter already had three unrelated jobs. Nothing here is a
// QObject subclass needing moc - no signals, no slots, just an event filter
// on the one label it owns.
class ZoomView : public QScrollArea {
public:
    ZoomView(const QPixmap &full, QSize fitted, QWidget *parent)
        : QScrollArea(parent), m_full(full), m_fitted(fitted)
    {
        m_label = new QLabel(this);
        m_label->setAlignment(Qt::AlignCenter);
        setWidget(m_label);
        // Explicit geometry. widgetResizable(true) stretches the label to the
        // viewport, which fights every resize() below and leaves panning with
        // nothing to scroll.
        setWidgetResizable(false);
        setAlignment(Qt::AlignCenter);
        m_label->installEventFilter(this);

        // Only worth a zoom when fitting actually cost pixels: a small image
        // already blown up to the 2x cap has none left to reveal, and a
        // toggle that changes nothing is worse than no toggle.
        m_canZoom = fitted.width() < full.width();
        apply(false, QPoint());
    }

protected:
    bool eventFilter(QObject *obj, QEvent *ev) override
    {
        if (obj != m_label) return QScrollArea::eventFilter(obj, ev);
        auto *me = (ev->type() == QEvent::MouseButtonPress
                 || ev->type() == QEvent::MouseMove
                 || ev->type() == QEvent::MouseButtonRelease)
                   ? static_cast<QMouseEvent *>(ev) : nullptr;

        if (me && ev->type() == QEvent::MouseButtonPress
            && me->button() == Qt::LeftButton) {
            m_pressPos    = me->pos();
            m_pressScroll = QPoint(horizontalScrollBar()->value(),
                                   verticalScrollBar()->value());
            m_dragged = false;
            m_label->setCursor(Qt::ClosedHandCursor);
            return true;
        }
        if (me && ev->type() == QEvent::MouseMove
            && (me->buttons() & Qt::LeftButton)) {
            const QPoint d = me->pos() - m_pressPos;
            // A hand never holds perfectly still, so a few pixels are still a
            // click. Qt's own threshold, the one every drag in the toolkit
            // uses.
            if (!m_dragged
                && d.manhattanLength() >= QApplication::startDragDistance())
                m_dragged = true;
            if (m_dragged) {
                horizontalScrollBar()->setValue(m_pressScroll.x() - d.x());
                verticalScrollBar()->setValue(m_pressScroll.y() - d.y());
            }
            return true;
        }
        if (me && ev->type() == QEvent::MouseButtonRelease
            && me->button() == Qt::LeftButton) {
            // A drag moved the image and must not ALSO change the zoom, or
            // every attempt to look around would throw the user back out to
            // the fitted view.
            if (!m_dragged && m_canZoom) apply(!m_actual, me->pos());
            else                         updateCursor();
            return true;
        }
        return QScrollArea::eventFilter(obj, ev);
    }

private:
    // `anchor` is where the user clicked, in label coordinates, so the zoom
    // lands on what they were looking at instead of the top-left corner.
    void apply(bool actual, QPoint anchor)
    {
        const QSizeF before = m_label->pixmap().size();
        QPointF rel(0.5, 0.5);
        if (!anchor.isNull() && before.width() > 0 && before.height() > 0)
            rel = QPointF(anchor.x() / before.width(),
                          anchor.y() / before.height());

        m_actual = actual;
        const QPixmap shown = actual
            ? m_full
            : m_full.scaled(m_fitted, Qt::KeepAspectRatio,
                            Qt::SmoothTransformation);
        m_label->setPixmap(shown);
        m_label->resize(shown.size());

        horizontalScrollBar()->setValue(
            int(rel.x() * shown.width())  - viewport()->width()  / 2);
        verticalScrollBar()->setValue(
            int(rel.y() * shown.height()) - viewport()->height() / 2);
        updateCursor();
    }

    void updateCursor()
    {
        // What the pointer promises has to be what a click does: a hand where
        // there is something to drag, a finger where a click zooms in.
        const bool pannable = horizontalScrollBar()->maximum() > 0
                           || verticalScrollBar()->maximum() > 0;
        m_label->setCursor(pannable   ? Qt::OpenHandCursor
                           : m_canZoom ? Qt::PointingHandCursor
                                       : Qt::ArrowCursor);
        m_label->setToolTip(m_canZoom
            ? (m_actual ? T("fomod_preview_fit") : T("fomod_preview_actual"))
            : QString());
    }

    QLabel *m_label = nullptr;
    QPixmap m_full;
    QSize   m_fitted;
    bool    m_canZoom = false;
    bool    m_actual  = false;
    bool    m_dragged = false;
    QPoint  m_pressPos;
    QPoint  m_pressScroll;
};

} // namespace

QSize FomodWizard::previewFitSize(QSize native, QSize available,
                                  double maxUpscale)
{
    if (native.isEmpty() || available.isEmpty()) return {};
    const double k = qMin(qMin(double(available.width())  / native.width(),
                               double(available.height()) / native.height()),
                          maxUpscale);
    return QSize(qMax(1, qRound(native.width()  * k)),
                 qMax(1, qRound(native.height() * k)));
}

void FomodWizard::openFullImage()
{
    // Nothing shown, nothing to open - which is exactly the placeholder case.
    if (m_previewAbsPath.isEmpty()) return;

    // The FILE, not m_previewCache: that holds a 376px thumbnail, and
    // enlarging it would be the opposite of "full resolution".
    const QPixmap full(m_previewAbsPath);
    if (full.isNull()) return;

    auto *win = new QDialog(this, Qt::Window);
    win->setAttribute(Qt::WA_DeleteOnClose);
    win->setWindowTitle(m_previewName.isEmpty()
                            ? T("fomod_preview_window")
                            : m_previewName);

    QSize avail(1280, 800);
    if (QScreen *sc = screen() ? screen() : QGuiApplication::primaryScreen())
        avail = sc->availableGeometry().size() * 0.9;
    const QSize fitted = previewFitSize(full.size(), avail);

    auto *lay = new QVBoxLayout(win);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->addWidget(new ZoomView(full, fitted, win));

    win->resize(fitted + QSize(2, 2));
    win->show();
}

bool FomodWizard::eventFilter(QObject *obj, QEvent *ev)
{
    if (m_previewPane && ev->type() == QEvent::Enter)
        if (auto *btn = qobject_cast<QAbstractButton *>(obj))
            showPreviewFor(btn);

    // The pane: open the picture properly. Everything the full-size window
    // does with the mouse belongs to ZoomView, not here.
    if (obj == m_previewImage && ev->type() == QEvent::MouseButtonRelease
        && static_cast<QMouseEvent *>(ev)->button() == Qt::LeftButton) {
        openFullImage();
        return true;
    }
    return QDialog::eventFilter(obj, ev);
}

void FomodWizard::showPreviewFor(QAbstractButton *btn)
{
    if (!m_previewPane || !btn) return;
    const QString rel  = btn->property("fomodImage").toString();
    const QString name = btn->property("fomodName").toString();
    const QString desc = btn->property("fomodDesc").toString();

    QString caption = QStringLiteral("<b>%1</b>").arg(name.toHtmlEscaped());
    if (!desc.isEmpty())
        caption += QStringLiteral("<br>%1").arg(desc.toHtmlEscaped());
    m_previewCaption->setText(caption);

    m_previewName = name;

    // The affordance must never lie: no picture, no pointing hand, no path
    // for a click to act on.
    const auto showPlaceholder = [this] {
        m_previewAbsPath.clear();
        m_previewImage->setPixmap({});
        m_previewImage->setText(T("fomod_no_preview"));
        m_previewImage->setCursor(Qt::ArrowCursor);
        m_previewImage->setToolTip({});
    };

    if (rel.isEmpty()) { showPlaceholder(); return; }
    // Resolved through the same case-insensitive walk the installer uses for
    // source files - the declared path is whatever the author typed, usually
    // with backslashes, and the on-disk case is whatever the archive held.
    const QString abs = fomod::resolvePath(m_archiveRoot, rel);
    auto it = m_previewCache.constFind(abs);
    if (it == m_previewCache.constEnd()) {
        QPixmap px;
        if (!abs.isEmpty()) px.load(abs);
        // Scale ONCE at cache time: authors ship 4K screenshots, and
        // rescaling one on every hover makes the pointer stutter.
        if (!px.isNull())
            px = px.scaled(QSize(376, 500), Qt::KeepAspectRatio,
                           Qt::SmoothTransformation);
        it = m_previewCache.insert(abs, px);
    }
    if (it->isNull()) {
        showPlaceholder();
    } else {
        m_previewAbsPath = abs;
        m_previewImage->setText({});
        m_previewImage->setPixmap(*it);
        m_previewImage->setCursor(Qt::PointingHandCursor);
        m_previewImage->setToolTip(T("fomod_preview_click"));
    }
}

void FomodWizard::defaultPreviewForStep(int si)
{
    if (!m_previewPane) return;
    if (si < 0 || si >= m_buttons.size()) return;
    // The step's answer so far, else the first option that has a picture -
    // arriving on a page with a stale image from the previous step reads as
    // the wrong option being illustrated.
    QAbstractButton *fallback = nullptr;
    for (const auto &grp : m_buttons[si])
        for (QAbstractButton *btn : grp) {
            if (!btn) continue;
            if (btn->isChecked()) { showPreviewFor(btn); return; }
            if (!fallback
                && !btn->property("fomodImage").toString().isEmpty())
                fallback = btn;
        }
    if (fallback) showPreviewFor(fallback);
}

bool FomodWizard::parse()
{
    QString configPath = findModuleConfig(m_archiveRoot);
    if (configPath.isEmpty()) return false;

    QFile file(configPath);
    if (!file.open(QIODevice::ReadOnly)) return false;

    QXmlStreamReader xml(&file);

    auto nameIs = [&](const char *s) {
        return xml.name().compare(QLatin1String(s), Qt::CaseInsensitive) == 0;
    };

    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.tokenType() != QXmlStreamReader::StartElement) continue;

        if (nameIs("moduleName")) {
            m_modName = xml.readElementText();
        }
        else if (nameIs("requiredInstallFiles")) {
            while (!xml.atEnd()) {
                xml.readNext();
                if (xml.tokenType() == QXmlStreamReader::EndElement && nameIs("requiredInstallFiles")) break;
                if (xml.tokenType() != QXmlStreamReader::StartElement) continue;
                if (nameIs("file"))   m_requiredFiles.append(parseFileAttrs(xml));
                if (nameIs("folder")) m_requiredFolders.append(parseFileAttrs(xml));
            }
        }
        else if (nameIs("installStep")) {
            FomodStep step;
            step.name = xml.attributes().value("name").toString();

            while (!xml.atEnd()) {
                xml.readNext();
                if (xml.tokenType() == QXmlStreamReader::EndElement && nameIs("installStep")) break;
                if (xml.tokenType() != QXmlStreamReader::StartElement) continue;
                if (!nameIs("group")) continue;

                FomodGroup group;
                group.name = xml.attributes().value("name").toString();
                group.type = xml.attributes().value("type").toString();

                while (!xml.atEnd()) {
                    xml.readNext();
                    if (xml.tokenType() == QXmlStreamReader::EndElement && nameIs("group")) break;
                    if (xml.tokenType() != QXmlStreamReader::StartElement) continue;
                    if (!nameIs("plugin")) continue;

                    FomodPlugin plugin;
                    plugin.name = xml.attributes().value("name").toString();

                    while (!xml.atEnd()) {
                        xml.readNext();
                        if (xml.tokenType() == QXmlStreamReader::EndElement && nameIs("plugin")) break;
                        if (xml.tokenType() != QXmlStreamReader::StartElement) continue;

                        if (nameIs("description")) {
                            plugin.description = xml.readElementText();
                        }
                        else if (nameIs("image")) {
                            plugin.imagePath =
                                xml.attributes().value("path").toString();
                        }
                        else if (nameIs("files")) {
                            while (!xml.atEnd()) {
                                xml.readNext();
                                if (xml.tokenType() == QXmlStreamReader::EndElement && nameIs("files")) break;
                                if (xml.tokenType() != QXmlStreamReader::StartElement) continue;
                                if (nameIs("file"))   plugin.files.append(parseFileAttrs(xml));
                                if (nameIs("folder")) plugin.folders.append(parseFileAttrs(xml));
                            }
                        }
                        else if (nameIs("typeDescriptor")) {
                            while (!xml.atEnd()) {
                                xml.readNext();
                                if (xml.tokenType() == QXmlStreamReader::EndElement && nameIs("typeDescriptor")) break;
                                if (xml.tokenType() != QXmlStreamReader::StartElement) continue;
                                if (nameIs("type"))
                                    plugin.type = xml.attributes().value("name").toString();
                            }
                        }
                        else if (nameIs("conditionFlags")) {
                            while (!xml.atEnd()) {
                                xml.readNext();
                                if (xml.tokenType() == QXmlStreamReader::EndElement && nameIs("conditionFlags")) break;
                                if (xml.tokenType() != QXmlStreamReader::StartElement) continue;
                                if (nameIs("flag")) {
                                    FomodFlagValue fv;
                                    fv.name  = xml.attributes().value("name").toString();
                                    fv.value = xml.readElementText();
                                    plugin.conditionFlags.append(fv);
                                }
                            }
                        }
                    }

                    group.plugins.append(plugin);
                }

                step.groups.append(group);
            }

            m_steps.append(step);
        }
        else if (nameIs("conditionalFileInstalls")) {
            // Root-level patterns: install files/folders when picked plugins'
            // conditionFlags satisfy the named flag combos. "option -> flag ->
            // files" FOMODs (EKM Corkbulb Retexture) install their content only
            // this way; skip this block and the mod is empty.
            while (!xml.atEnd()) {
                xml.readNext();
                if (xml.tokenType() == QXmlStreamReader::EndElement && nameIs("conditionalFileInstalls")) break;
                if (xml.tokenType() != QXmlStreamReader::StartElement)   continue;
                if (!nameIs("patterns")) continue;

                while (!xml.atEnd()) {
                    xml.readNext();
                    if (xml.tokenType() == QXmlStreamReader::EndElement && nameIs("patterns")) break;
                    if (xml.tokenType() != QXmlStreamReader::StartElement) continue;
                    if (!nameIs("pattern")) continue;

                    FomodPattern pat;
                    while (!xml.atEnd()) {
                        xml.readNext();
                        if (xml.tokenType() == QXmlStreamReader::EndElement && nameIs("pattern")) break;
                        if (xml.tokenType() != QXmlStreamReader::StartElement) continue;

                        if (nameIs("dependencies")) {
                            QString op = xml.attributes().value("operator").toString();
                            if (!op.isEmpty()) pat.op = op;
                            while (!xml.atEnd()) {
                                xml.readNext();
                                if (xml.tokenType() == QXmlStreamReader::EndElement && nameIs("dependencies")) break;
                                if (xml.tokenType() != QXmlStreamReader::StartElement) continue;
                                if (nameIs("flagDependency")) {
                                    FomodFlagValue fv;
                                    fv.name  = xml.attributes().value("flag").toString();
                                    fv.value = xml.attributes().value("value").toString();
                                    pat.flagDeps.append(fv);
                                }
                                // Only flagDependency handled; OpenMW FOMODs
                                // don't use fileDependency / nested deps.
                            }
                        }
                        else if (nameIs("files")) {
                            while (!xml.atEnd()) {
                                xml.readNext();
                                if (xml.tokenType() == QXmlStreamReader::EndElement && nameIs("files")) break;
                                if (xml.tokenType() != QXmlStreamReader::StartElement) continue;
                                if (nameIs("file"))   pat.files.append(parseFileAttrs(xml));
                                if (nameIs("folder")) pat.folders.append(parseFileAttrs(xml));
                            }
                        }
                    }
                    m_conditionalInstalls.append(pat);
                }
            }
        }
    }

    return !xml.hasError();
}

void FomodWizard::buildUi()
{
    QString title = m_modName.isEmpty()
        ? T("fomod_wizard_title_generic")
        : T("fomod_wizard_title_named").arg(m_modName);
    setWindowTitle(title);

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(6);

    m_titleLbl = new QLabel(this);
    QFont f = m_titleLbl->font();
    f.setBold(true);
    m_titleLbl->setFont(f);
    mainLayout->addWidget(m_titleLbl);

    m_stack = new QStackedWidget(this);

    // Any option with a picture earns the whole dialog a preview pane: for a
    // retexture installer ("Option 1".."Option 4") the image IS the option,
    // and without it the choice is a guessing game. Pictureless installers
    // keep the old compact shape.
    bool anyImage = false;
    for (const FomodStep &st : m_steps)
        for (const FomodGroup &g : st.groups)
            for (const FomodPlugin &pl : g.plugins)
                if (!pl.imagePath.isEmpty()) { anyImage = true; break; }

    if (anyImage) {
        auto *row = new QHBoxLayout;
        row->setSpacing(8);
        row->addWidget(m_stack, 1);

        m_previewPane = new QWidget(this);
        m_previewPane->setFixedWidth(380);
        auto *pv = new QVBoxLayout(m_previewPane);
        pv->setContentsMargins(0, 0, 0, 0);
        m_previewImage = new QLabel(m_previewPane);
        m_previewImage->setMinimumHeight(240);
        m_previewImage->setAlignment(Qt::AlignCenter);
        m_previewImage->setFrameShape(QFrame::StyledPanel);
        m_previewImage->installEventFilter(this);
        pv->addWidget(m_previewImage, 1);
        m_previewCaption = new QLabel(m_previewPane);
        m_previewCaption->setWordWrap(true);
        m_previewCaption->setAlignment(Qt::AlignTop | Qt::AlignLeft);
        m_previewCaption->setMaximumHeight(120);
        pv->addWidget(m_previewCaption);
        row->addWidget(m_previewPane);

        mainLayout->addLayout(row, 1);
        setMinimumSize(940, 480);
    } else {
        mainLayout->addWidget(m_stack, 1);
    }

    m_buttons.resize(m_steps.size());
    for (int si = 0; si < m_steps.size(); ++si) {
        const FomodStep &step = m_steps[si];
        m_buttons[si].resize(step.groups.size());

        auto *page    = new QWidget;
        auto *pageLay = new QVBoxLayout(page);
        pageLay->setContentsMargins(0, 0, 0, 0);

        auto *scroll  = new QScrollArea;
        scroll->setWidgetResizable(true);
        auto *inner   = new QWidget;
        auto *innerLay = new QVBoxLayout(inner);
        innerLay->setSpacing(8);

        for (int gi = 0; gi < step.groups.size(); ++gi) {
            const FomodGroup &group = step.groups[gi];
            auto *box    = new QGroupBox(group.name);
            auto *boxLay = new QVBoxLayout(box);

            bool selectExactlyOne = (group.type == "SelectExactlyOne");
            bool selectAtMostOne  = (group.type == "SelectAtMostOne");
            bool exclusive = selectExactlyOne || selectAtMostOne;
            bool selectAll = (group.type == "SelectAll");

            QButtonGroup *btnGroup = exclusive ? new QButtonGroup(box) : nullptr;
            if (btnGroup) btnGroup->setExclusive(true);

            bool firstSelectable = true;
            bool anyChecked = false;

            for (int pi = 0; pi < group.plugins.size(); ++pi) {
                const FomodPlugin &plugin = group.plugins[pi];

                bool required   = (plugin.type == "Required");
                bool notUsable  = (plugin.type == "NotUsable");
                bool recommended = (plugin.type == "Recommended");

                QAbstractButton *btn;
                if (exclusive) {
                    auto *rb = new QRadioButton(plugin.name, box);
                    if (btnGroup) btnGroup->addButton(rb);
                    btn = rb;
                } else {
                    btn = new QCheckBox(plugin.name, box);
                }

                bool defaultOn = required || recommended || selectAll;
                if (!defaultOn && !exclusive && group.plugins.size() == 1) {
                    defaultOn = true; // lone SelectAny plugin = core component, default ON
                }
                // Only SelectExactlyOne forces a pick; SelectAtMostOne may stay
                // on "none".
                if (selectExactlyOne && firstSelectable && !notUsable) {
                    defaultOn = true;   // at least one radio on
                    firstSelectable = false;
                }
                btn->setChecked(defaultOn);
                if (defaultOn) anyChecked = true;
                if (required || notUsable) btn->setEnabled(false);

                if (!plugin.description.isEmpty())
                    btn->setToolTip(plugin.description);

                // The preview pane follows the pointer and the selection.
                // Data rides on the button itself so one filter serves all.
                if (m_previewPane) {
                    btn->setProperty("fomodImage", group.plugins[pi].imagePath);
                    btn->setProperty("fomodName",  group.plugins[pi].name);
                    btn->setProperty("fomodDesc",  group.plugins[pi].description);
                    btn->installEventFilter(this);
                    connect(btn, &QAbstractButton::toggled, this,
                            [this, btn](bool on) { if (on) showPreviewFor(btn); });
                }
                boxLay->addWidget(btn);
                m_buttons[si][gi].append(btn);
            }

            // SelectAtMostOne allows zero, but a radio group can't return to
            // "none" once picked, and the FOMOD may want nothing on by default
            // (OAAB_Saplings optional patch steps). Add a synthetic "None" radio
            // to the exclusive group. Not in m_buttons, so applySelections /
            // collectChoices (indexed by plugin) install nothing while it's active.
            if (selectAtMostOne) {
                auto *noneBtn = new QRadioButton(T("fomod_select_none"), box);
                if (btnGroup) btnGroup->addButton(noneBtn);
                noneBtn->setChecked(!anyChecked);
                boxLay->addWidget(noneBtn);
            }

            // boxLay is owned by `box`; analyzer can't see the parent link.
            innerLay->addWidget(box); // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)
        }
        innerLay->addStretch();
        scroll->setWidget(inner);
        pageLay->addWidget(scroll);
        m_stack->addWidget(page);
    }

    // Smart defaults per exclusive group:
    //   Pass A - OpenMW vs MGE XE: always pick OpenMW; prior choices do NOT
    //            override. Recorded in openMwOverriddenGroups so the prior-choices
    //            block skips them.
    //   Pass B - Yes/No groups that are about ANOTHER mod -> pick Yes (it is in
    //            the modlist) / No (it is not). Names expand to variants
    //            ("OAAB_Data" / "OAAB Data" / "OAAB"). The two halves need
    //            different evidence: a modlist hit proves the step names a mod,
    //            a miss proves nothing, so "No" additionally needs wording that
    //            asks about the user's setup (fomod::asksAboutAnotherMod). A
    //            question about this mod's own options gets no verdict at all.
    //            Annotation always shown; selection only changes with no stored
    //            prior.

    QSet<quint64> openMwOverriddenGroups;
    // Checkbox plugins Pass C auto-recommends because the named mod is present.
    // Key: (si<<32)|(gi<<16)|pi. Prior-choices block ORs this in so a Recommended
    // checkbox isn't unticked just because an earlier install left it off.
    QSet<quint64> recommendedInstalledPlugins;
    // Ticks settled by what is in the modlist right now (Pass E and Pass F),
    // same key, value = the tick that was forced. Whether a mod is installed
    // is a fact about this profile, not a preference, so a choice stored from
    // an earlier install of this same FOMOD must not put the old tick back:
    // the modlist it was made against is gone.
    QHash<quint64, bool> modlistSettledPlugins;

    {
        // (si,gi) pairs with a stored selection. Pass B annotates but doesn't
        // change these.
        QSet<quint64> priorGroups;
        for (const QString &rec : m_priorChoices.split(u';', Qt::SkipEmptyParts)) {
            const QStringList f = rec.split(u':');
            if (f.size() == 3) {
                bool ok1, ok2, ok3;
                int si2 = f[0].toInt(&ok1), gi2 = f[1].toInt(&ok2); f[2].toInt(&ok3);
                if (ok1 && ok2 && ok3)
                    priorGroups.insert((quint64(si2) << 16) | quint64(gi2));
            }
        }

        // Search needles for the spellings a modlist actually uses. Shared
        // with the BAIN picker, which asks the same question of a folder
        // name - see mod_match.h.
        const auto needlesFor = &mod_match::needlesFor;

        for (int si = 0; si < m_steps.size(); ++si) {
            const FomodStep &step = m_steps[si];
            for (int gi = 0; gi < step.groups.size(); ++gi) {
                const FomodGroup &group = step.groups[gi];
                const quint64 groupKey = (quint64(si) << 16) | quint64(gi);

                const bool exclusive  = (group.type == QLatin1String("SelectExactlyOne") ||
                                          group.type == QLatin1String("SelectAtMostOne"));
                const bool isSelectAll = (group.type == QLatin1String("SelectAll"));
                if (isSelectAll) continue;  // all forced on

                if (exclusive) {
                    // Pass A: OpenMW vs MGE XE - prefer OpenMW.
                    {
                        int openMwIdx = -1, mgeIdx = -1;
                        for (int pi = 0; pi < group.plugins.size(); ++pi) {
                            const QString pn = group.plugins[pi].name.trimmed().toLower();
                            if (openMwIdx == -1 && pn.contains(QLatin1String("openmw")))
                                openMwIdx = pi;
                            if (mgeIdx == -1 && (pn.contains(QLatin1String("mge xe")) ||
                                                 pn.contains(QLatin1String("mg xe"))  ||
                                                 pn.contains(QLatin1String("mgxe"))   ||
                                                 pn.contains(QLatin1String("mge"))    ||
                                                 pn.contains(QLatin1String("mwse"))))
                                mgeIdx = pi;
                        }
                        if (openMwIdx != -1 && mgeIdx != -1) {
                            QAbstractButton *btn = m_buttons[si][gi].value(openMwIdx);
                            if (btn && btn->isEnabled()) {
                                btn->setChecked(true);
                                btn->setText(btn->text() +
                                    QStringLiteral(" \u2705 Recommended \u2014 Nerevarine is an OpenMW-only manager."));
                            }
                            openMwOverriddenGroups.insert(groupKey);
                            continue;  // skip Pass B for this group
                        }
                    }

                    // Pass A2: language groups - prefer English.
                    {
                        int engIdx = -1, nonEngIdx = -1;
                        static const QStringList kEngTokens  = {
                            "eng", "english", "en"
                        };
                        static const QStringList kRusTokens  = {
                            "rus", "russian", "ru", "russ"
                        };
                        // Match names that are just a language token, or
                        // led/trailed by one ("English", "ENG", "Russian patch").
                        for (int pi = 0; pi < group.plugins.size(); ++pi) {
                            const QString pn = group.plugins[pi].name.trimmed().toLower();
                            for (const QString &t : kEngTokens) {
                                if (pn == t || pn.startsWith(t + " ")
                                           || pn.endsWith(" " + t)) {
                                    engIdx = pi; break;
                                }
                            }
                            if (engIdx == -1) {
                                for (const QString &t : kRusTokens) {
                                    if (pn == t || pn.startsWith(t + " ")
                                               || pn.endsWith(" " + t)) {
                                        nonEngIdx = pi; break;
                                    }
                                }
                            }
                        }
                        if (engIdx != -1 && nonEngIdx != -1) {
                            QAbstractButton *btn = m_buttons[si][gi].value(engIdx);
                            if (btn && btn->isEnabled()) {
                                btn->setChecked(true);
                                btn->setText(btn->text() +
                                    QStringLiteral(" (Recommended - English)"));
                            }
                            openMwOverriddenGroups.insert(groupKey);
                            continue;  // skip Pass B for this group
                        }
                    }

                    // Pass A3: game-runtime pairs - an SKSE-plugin FOMOD
                    // offering its DLL per runtime ("SSE v1.6.629+
                    // ('Anniversary Edition')" vs "SSE v1.5.97 ('Special
                    // Edition')"). Which one is right is not a preference, it
                    // is a fact the manager already holds: the AE/SE split IS
                    // the game profile. Only fires when the group contains
                    // both kinds, so a stray "AE" in an unrelated option name
                    // never draws a tick.
                    {
                        const auto pref = fomod::runtimePreferenceForGame(m_gameId);
                        if (pref != fomod::SkyrimRuntime::None) {
                            bool haveAe = false, haveSe = false;
                            int prefIdx = -1;
                            for (int pi = 0; pi < group.plugins.size(); ++pi) {
                                const auto v = fomod::classifyRuntimeVariant(
                                    group.plugins[pi].name);
                                haveAe |= (v == fomod::SkyrimRuntime::AE);
                                haveSe |= (v == fomod::SkyrimRuntime::SE);
                                if (v == pref && prefIdx == -1) prefIdx = pi;
                            }
                            if (haveAe && haveSe && prefIdx != -1) {
                                QAbstractButton *btn =
                                    m_buttons[si][gi].value(prefIdx);
                                if (btn && btn->isEnabled()) {
                                    btn->setChecked(true);
                                    btn->setText(btn->text() + QStringLiteral(
                                        " ✅ Recommended - matches "
                                        "your game version."));
                                }
                                openMwOverriddenGroups.insert(groupKey);
                                continue;  // settled; skip Pass B
                            }
                        }
                    }

                    // Pass B: Yes/No groups - check modlist presence.
                    int yesIdx = -1, noIdx = -1;
                    for (int pi = 0; pi < group.plugins.size(); ++pi) {
                        const QString pname = group.plugins[pi].name.trimmed().toLower();
                        if (yesIdx == -1 && pname == QLatin1String("yes")) yesIdx = pi;
                        if (noIdx  == -1 && pname == QLatin1String("no"))  noIdx  = pi;
                    }
                    if (yesIdx == -1 || noIdx == -1) continue;

                    QAbstractButton *yesBtn = m_buttons[si][gi].value(yesIdx);
                    QAbstractButton *noBtn  = m_buttons[si][gi].value(noIdx);
                    if (!yesBtn || !noBtn) continue;

                    // Needles from the group name + step/group context, matched
                    // against installed mod names. Short needles (< 8 chars) must
                    // match start-of-name to avoid false hits.
                    const QString context = step.name + QLatin1Char(' ') + group.name;
                    QStringList contextNeedles = needlesFor(group.name);
                    for (const QString &cn : needlesFor(context))
                        if (!contextNeedles.contains(cn)) contextNeedles << cn;

                    bool modPresent = false;
                    for (const QString &needle : std::as_const(contextNeedles)) {
                        const bool shortNeedle = (needle.length() < 8);
                        const QString pat = shortNeedle
                            ? (QLatin1String("^") + QRegularExpression::escape(needle) + QLatin1String("\\b"))
                            : (QLatin1String("\\b") + QRegularExpression::escape(needle) + QLatin1String("\\b"));
                        const QRegularExpression re(pat, QRegularExpression::CaseInsensitiveOption);
                        for (const QString &modName : std::as_const(m_installedModNames)) {
                            if (re.match(modName).hasMatch()) { modPresent = true; break; }
                        }
                        if (modPresent) break;
                    }

                    const bool hasPrior = priorGroups.contains(groupKey);

                    if (modPresent) {
                        if (!hasPrior) yesBtn->setChecked(true);
                        yesBtn->setText(yesBtn->text() +
                            QStringLiteral(" \u2705 Recommended. The mod is currently present in the modlist."));
                    } else if (fomod::asksAboutAnotherMod(step.name, group.name)) {
                        if (!hasPrior) noBtn->setChecked(true);
                        noBtn->setText(noBtn->text() +
                            QStringLiteral(" \u2705 Recommended. This mod is not currently present in the modlist."));
                    }
                    // Nothing matched and the question never mentions the user's
                    // setup: it is one of this mod's own options, so leave the
                    // FOMOD's default alone and say nothing.

                } else {
                    // Pass C: checkbox groups - match each plugin name against
                    // the modlist; auto-check + annotate on a hit.
                    //
                    // Absence used to say nothing here, on the grounds that an
                    // unchecked optional is clear enough. That holds only while
                    // the FOMOD ships its options OFF. Vehicle Overhaul
                    // Continued pre-ticks eighteen patches named after the mods
                    // they patch, so on a list holding none of them the silence
                    // installed all eighteen. A patch is also the one case this
                    // could not match even when the mod IS present, because the
                    // needle was the whole option name and "A Forest Patch"
                    // never matches a mod called "A Forest".
                    const bool hasPrior = priorGroups.contains(groupKey);
                    // Only SelectAny may be emptied. SelectAtLeastOne has to
                    // keep one, and deciding WHICH is not this pass's business.
                    const bool mayUntick = (group.type == QLatin1String("SelectAny"));
                    for (int pi = 0; pi < group.plugins.size(); ++pi) {
                        QAbstractButton *btn = m_buttons[si][gi].value(pi);
                        if (!btn || !btn->isEnabled()) continue;

                        const QString pluginName = group.plugins[pi].name.trimmed();
                        if (pluginName.length() < 4) continue;

                        // A patch option names the mod it PATCHES, so ask about
                        // that instead of about the option's own name. Empty
                        // for everything that is not one, which leaves every
                        // ordinary option on the path below.
                        // "Foo - No BOS Patch" is a patch for Foo, for
                        // people WITHOUT BOS - not a patch for a mod called
                        // "Foo - No BOS". A negated name is Pass H territory
                        // in exclusive groups and silence everywhere else.
                        const QStringList patchTargets =
                            fomod::negatedModIn(pluginName).isEmpty()
                                ? fomod::patchTargetsOf(pluginName, group.name)
                                : QStringList();

                        bool    pluginInstalled = false;
                        QString matchedMod;
                        if (!patchTargets.isEmpty()) {
                            // Any one candidate answering is enough: a combined
                            // patch offers both halves, and a mod with an
                            // acronym answers to either spelling.
                            for (const QString &target : patchTargets) {
                                matchedMod = mod_match::installedUnderAnyName(
                                    target, m_installedModNames);
                                if (!matchedMod.isEmpty()) { pluginInstalled = true; break; }
                            }
                        } else {
                        // Plugin name + aliases vs each mod name. Short needles
                        // (< 8 chars) must match start-of-name so "MWSE" doesn't
                        // hit "Graphic Herbalism MWSE - OpenMW".
                        for (const QString &needle : needlesFor(pluginName)) {
                            const bool shortNeedle = (needle.length() < 8);
                            const QString pat = shortNeedle
                                ? (QLatin1String("^") + QRegularExpression::escape(needle) + QLatin1String("\\b"))
                                : (QLatin1String("\\b") + QRegularExpression::escape(needle) + QLatin1String("\\b"));
                            const QRegularExpression re(pat, QRegularExpression::CaseInsensitiveOption);
                            for (const QString &modName : std::as_const(m_installedModNames)) {
                                if (re.match(modName).hasMatch()) { pluginInstalled = true; break; }
                            }
                            if (pluginInstalled) break;
                        }
                        }

                        if (pluginInstalled) {
                            if (!hasPrior) btn->setChecked(true);
                            btn->setText(btn->text() +
                                QStringLiteral(" \u2705 Recommended. The mod is currently present in the modlist."));
                            recommendedInstalledPlugins.insert(
                                (quint64(si) << 32) | (quint64(gi) << 16) | quint64(pi));
                        } else if (!patchTargets.isEmpty()) {
                            // Say so whether or not it was ticked - "which of
                            // these do I have?" is the question the list is
                            // silently asking - but only change a tick the
                            // FOMOD made, never one the user already chose.
                            btn->setText(btn->text() +
                                QStringLiteral(" \u26a0\ufe0f %1 is not installed in this modlist.")
                                    .arg(patchTargets.first()));
                            const QString tip = btn->toolTip();
                            btn->setToolTip((tip.isEmpty() ? QString()
                                                           : tip + QStringLiteral("\n\n"))
                                + QStringLiteral("This patch is for %1, which is not in this "
                                                 "modlist, so its files would be installed for "
                                                 "nothing. Tick it back if you have that mod "
                                                 "outside the manager, or are about to add it.")
                                      .arg(patchTargets.first()));
                            if (mayUntick && !hasPrior && btn->isChecked())
                                btn->setChecked(false);
                        }
                    }
                }
            }
        }

        // Pass D: patch-hub auto-tick. SelectAny groups where every plugin's
        // <files> include an .omwscripts entry -> tick all by default. These are
        // patch-hub mods (Completionist Patch Hub) named after landmass mods the
        // user is unlikely to have all of; Pass C would leave the group empty,
        // installing only the root .omwscripts + an empty scripts/, and OpenMW
        // then fails loading the orphan script declarations. The .omwscripts are
        // tiny and harmless when their target isn't loaded, so over-installing is
        // safer. Skipped when prior choices cover the group so an untick survives.
        for (int si = 0; si < m_steps.size(); ++si) {
            const FomodStep &step = m_steps[si];
            for (int gi = 0; gi < step.groups.size(); ++gi) {
                const FomodGroup &group = step.groups[gi];
                if (group.type != QLatin1String("SelectAny")) continue;
                if (group.plugins.size() < 2)                continue;
                const quint64 groupKey = (quint64(si) << 16) | quint64(gi);
                if (priorGroups.contains(groupKey))          continue;

                bool patchHub = true;
                for (const FomodPlugin &plugin : group.plugins) {
                    bool hasOmw = false;
                    for (const FomodFile &f : plugin.files) {
                        if (f.source.endsWith(QLatin1String(".omwscripts"),
                                              Qt::CaseInsensitive)) {
                            hasOmw = true;
                            break;
                        }
                    }
                    if (!hasOmw) { patchHub = false; break; }
                }
                if (!patchHub) continue;

                for (int pi = 0; pi < group.plugins.size() &&
                                 pi < m_buttons[si][gi].size(); ++pi) {
                    QAbstractButton *btn = m_buttons[si][gi][pi];
                    if (!btn || !btn->isEnabled()) continue;
                    if (btn->isChecked()) continue;  // already on (Pass C)
                    btn->setChecked(true);
                    btn->setText(btn->text() +
                        QStringLiteral(" \u2705 Patch hub - default ON. "
                                       "Untick if you don't want this patch."));
                }
            }
        }

        // Pass E: compatibility options for mods the user does not have.
        // An Addendum to Tamrielic Lore Data offers Ashfall-compatible meshes
        // and pre-ticks a Glass Glowset option; with neither mod installed,
        // both quietly deliver meshes nothing will load. The option
        // descriptions link the mod they are for, and a Nexus mod-page URL
        // names exactly one page, so this matches by id - no name guessing,
        // and options that cite nothing stay silent. See fomod_hint.h.
        //
        // The verdict decides the tick, in both directions. Leaving the
        // author's default ticked underneath "Glass Glowset is not installed
        // in this modlist" states a fact and then acts against it, and the
        // warning is worth nothing if the box below it still installs meshes
        // for a mod that is not there. What IS in the modlist is a fact this
        // manager owns, so it answers with it - and the tick stays live, for
        // a copy installed outside the manager or one arriving later.
        //
        // Only where a tick is independently settable, as in Pass F: in an
        // exclusive group unticking means picking something else, which is
        // not ours to decide, so those are annotated only.
        for (int si = 0; si < m_steps.size(); ++si) {
            const FomodStep &step = m_steps[si];
            for (int gi = 0; gi < step.groups.size(); ++gi) {
                const FomodGroup &group = step.groups[gi];
                const bool tickable = group.type == QLatin1String("SelectAny")
                                   || group.type == QLatin1String("SelectAtLeastOne");

                // Collect, per cited mod page, the options citing it - the
                // option names are what name the mod when the group does not.
                QHash<QString, QStringList> citers;
                QList<QList<NexusModRef>>   perPlugin;
                perPlugin.reserve(group.plugins.size());
                for (const FomodPlugin &plugin : group.plugins) {
                    const auto cited = fomod::citedMods(plugin.description);
                    perPlugin.append(cited);
                    for (const NexusModRef &ref : cited) {
                        const QString key = ref.game.toLower() + u'/'
                                          + QString::number(ref.modId);
                        citers[key] << plugin.name;
                    }
                }

                for (int pi = 0; pi < group.plugins.size() &&
                                 pi < m_buttons[si][gi].size(); ++pi) {
                    const QList<NexusModRef> &cited = perPlugin[pi];
                    if (cited.isEmpty()) continue;   // no citation, no verdict

                    // A "(No BOS)" option routinely cites BOS's page - to say
                    // what it does WITHOUT. Vouching for it because the cited
                    // mod is installed recommends the wrong half of a variant
                    // pair, and unticking it because the mod is missing kills
                    // the one option made for that list. Not this pass's
                    // group either way: Pass H owns variant pairs, and a
                    // negated name elsewhere is safest left alone.
                    if (!fomod::negatedModIn(group.plugins[pi].name).isEmpty())
                        continue;

                    QAbstractButton *btn = m_buttons[si][gi][pi];
                    if (!btn) continue;

                    // An option citing several mods is only a problem when it
                    // has none of them; one present mod is reason enough for
                    // the option to exist.
                    QString     present;
                    QStringList missing;
                    for (const NexusModRef &ref : cited) {
                        const QString key = ref.game.toLower() + u'/'
                                          + QString::number(ref.modId);
                        const QString label =
                            fomod::missingModLabel(citers.value(key), group.name);
                        if (m_installedNexusKeys.contains(key)) {
                            present = label;
                            missing.clear();
                            break;
                        }
                        missing << label;
                    }

                    // A Required or NotUsable plugin is disabled: the FOMOD
                    // itself has already settled that tick, and there is no
                    // choice here to correct.
                    const quint64 key = (quint64(si) << 32) | (quint64(gi) << 16)
                                      | quint64(pi);
                    const bool settle = tickable && btn->isEnabled();
                    const QString tip = btn->toolTip();
                    auto addTip = [&](const QString &detail) {
                        btn->setToolTip((tip.isEmpty() ? QString()
                                                       : tip + QStringLiteral("\n\n"))
                                        + detail);
                    };

                    if (missing.isEmpty()) {
                        // Pass C already ticked this one and said why, having
                        // matched the option's own name against the modlist. A
                        // second tick and a second badge add nothing.
                        if (recommendedInstalledPlugins.contains(key)) continue;
                        if (settle) {
                            btn->setChecked(true);
                            modlistSettledPlugins.insert(key, true);
                        }
                        // Neither the options nor the group yielded a name
                        // worth printing, so say it without one - as the
                        // warning below does.
                        const QString what = present.isEmpty()
                            ? QStringLiteral("The mod this option is for")
                            : present;
                        btn->setText(btn->text() + (present.isEmpty()
                            ? QStringLiteral(" ✅ the mod this is for is installed")
                            : QStringLiteral(" ✅ %1 ✓").arg(present)));
                        addTip(settle
                            ? QStringLiteral("%1 is installed, so this option has "
                                             "been ticked.").arg(what)
                            : QStringLiteral("%1 is installed, so this option "
                                             "will work.").arg(what));
                        continue;
                    }

                    missing.removeDuplicates();
                    missing.removeAll(QString());

                    btn->setText(btn->text() + (missing.isEmpty()
                        ? QStringLiteral(" ⚠️ Warning: this option is "
                                         "for another mod that is not installed "
                                         "in this modlist.")
                        : QStringLiteral(" ⚠️ Warning: %1 is not "
                                         "installed in this modlist.")
                              .arg(missing.join(QStringLiteral(", ")))));
                    if (!settle) continue;
                    btn->setChecked(false);
                    modlistSettledPlugins.insert(key, false);
                    addTip(QStringLiteral("Unticked because that mod is not in this "
                                          "modlist, so these files would be installed "
                                          "for nothing. Tick it back if you have the "
                                          "mod outside the manager, or are about to "
                                          "add it."));
                }
            }
        }

        // A visible one-line explanation under an option, for choices the
        // wizard makes FOR the user. The reasoning used to live only in
        // tooltips, which nobody hovers - and "PRP v81 Previs" explains
        // nothing to someone who has never heard of PRP. Grey and indented,
        // so it reads as a footnote to the row above it, not another option.
        const auto addNoteUnder = [](QAbstractButton *btn, const QString &text) {
            if (!btn || !btn->parentWidget()) return;
            auto *lay = qobject_cast<QVBoxLayout *>(btn->parentWidget()->layout());
            if (!lay) return;
            auto *note = new QLabel(text, btn->parentWidget());
            note->setWordWrap(true);
            note->setIndent(22);
            note->setForegroundRole(QPalette::PlaceholderText);
            const int at = lay->indexOf(btn);
            if (at >= 0) lay->insertWidget(at + 1, note);
            else         lay->addWidget(note);
        };
        // What a framework IS, one line each, for the five this wizard can
        // recognise. Hardcoded English like every other annotation here.
        // Anything unknown gets the state sentence only - never invented
        // prose.
        const auto blurbFor = [](const QString &fullName) -> QString {
            const QString n = fullName.toLower();
            if (n == QLatin1String("previs repair pack"))
                return QStringLiteral("rebuilds the game's precombined meshes "
                                      "and visibility data (performance and "
                                      "occlusion)");
            if (n == QLatin1String("base object swapper"))
                return QStringLiteral("a framework that swaps placed objects "
                                      "in the world at runtime");
            if (n == QLatin1String("container distribution framework"))
                return QStringLiteral("a framework that distributes items "
                                      "into the game's containers");
            if (n == QLatin1String("skypatcher"))
                return QStringLiteral("a framework that patches game records "
                                      "at load time");
            if (n == QLatin1String("baka framework"))
                return QStringLiteral("a scripting framework extending F4SE");
            return {};
        };

        // Pass G: exclusive groups whose options name alternative FRAMEWORKS.
        //
        // Producers of Skyrim asks how to inject its orc-stronghold blacksmith
        // goods - Don't Install / Container Distribution Framework /
        // SkyPatcher - and ships with a framework pre-selected. With neither
        // installed that writes config files nothing reads: no crash, no
        // error, the blacksmiths simply have no goods.
        //
        // Runs BEFORE Pass F so a requirement stated in a description still
        // has the last word on the same option.
        for (int si = 0; si < m_steps.size(); ++si) {
            const FomodStep &step = m_steps[si];
            for (int gi = 0; gi < step.groups.size(); ++gi) {
                const FomodGroup &group = step.groups[gi];
                if (group.type != QLatin1String("SelectExactlyOne")
                    && group.type != QLatin1String("SelectAtMostOne")) continue;

                QStringList names;
                names.reserve(group.plugins.size());
                for (const FomodPlugin &p : group.plugins) names << p.name;

                const auto choice =
                    fomod::chooseFrameworkOption(names, m_installedModNames);
                if (choice.states.isEmpty()) continue;   // not a framework group

                using St = fomod::FrameworkChoice::State;
                for (int pi = 0; pi < names.size()
                                 && pi < m_buttons[si][gi].size(); ++pi) {
                    QAbstractButton *btn = m_buttons[si][gi][pi];
                    if (!btn) continue;
                    const QString tip = btn->toolTip();
                    auto note = [&](const QString &label, const QString &detail) {
                        btn->setText(btn->text() + label);
                        btn->setToolTip((tip.isEmpty() ? QString() : tip + QStringLiteral("\n\n"))
                                        + detail);
                    };
                    // The resolved framework, spelled out, and what it is -
                    // visible under the row, because a tooltip is where an
                    // explanation goes to be missed, and "PRP" explains
                    // nothing on its own.
                    const QString full  = choice.fullNames.value(pi);
                    const QString blurb = blurbFor(full);
                    const QString what =
                        full.isEmpty() ? QString()
                        : (names[pi].contains(full, Qt::CaseInsensitive)
                               ? full
                               : QStringLiteral("%1 = %2")
                                     .arg(names[pi].section(QLatin1Char(' '), 0, 0),
                                          full))
                          + (blurb.isEmpty() ? QString()
                                             : QStringLiteral(" \u2014 %1").arg(blurb));

                    // Hardcoded English, like every other annotation in this
                    // file - only the dialog chrome goes through T().
                    if (choice.states[pi] == St::Installed) {
                        note(QStringLiteral(" \u2705"),
                             QStringLiteral("%1 is installed, so this option "
                                            "will work.").arg(names[pi]));
                        addNoteUnder(btn,
                            (what.isEmpty() ? QString() : what + QStringLiteral(". "))
                            + QStringLiteral("Installed in your mod list, so "
                                             "this option will use it."));
                    } else if (choice.states[pi] == St::Missing) {
                        note(QStringLiteral(" \u26A0\uFE0F not installed"),
                             QStringLiteral("%1 is not installed in this "
                                            "modlist. Choosing this writes "
                                            "configuration files nothing will "
                                            "read - no error, the feature "
                                            "simply does nothing.").arg(names[pi]));
                        addNoteUnder(btn,
                            (what.isEmpty() ? QString() : what + QStringLiteral(". "))
                            + QStringLiteral("Not in your mod list \u2014 pick "
                                             "this only if you plan to install "
                                             "it."));
                    } else if (choice.states[pi] == St::OptOut
                               && !choice.anyInstalled) {
                        note(QStringLiteral(" \u2705"),
                             QStringLiteral("None of the frameworks this offers "
                                            "is installed, so this is the only "
                                            "option that does what it says."));
                    } else if (choice.states[pi] == St::Baseline
                               && !choice.anyInstalled) {
                        // Not a resignation, unlike the opt-out above: the
                        // game's own data is a working choice, and the one the
                        // other options are alternatives TO.
                        note(QStringLiteral(" \u2705"),
                             QStringLiteral("This builds against the game's own "
                                            "files, which are always there. The "
                                            "mod the other option needs is not "
                                            "in this modlist, so this is the "
                                            "one that will work."));
                        addNoteUnder(btn,
                            QStringLiteral("The game's own built-in data \u2014 "
                                           "always present, needs no extra mod. "
                                           "The safe choice unless you install "
                                           "the framework the other option "
                                           "needs."));
                    }
                }

                if (choice.index >= 0 && choice.index < m_buttons[si][gi].size()) {
                    QAbstractButton *pick = m_buttons[si][gi][choice.index];
                    if (pick && pick->isEnabled()) {
                        pick->setChecked(true);
                        if (choice.brokeTie)
                            pick->setToolTip(pick->toolTip()
                                + QStringLiteral("\n\nMore than one of these "
                                    "frameworks is installed, so either would "
                                    "work. This one is picked because more mods "
                                    "depend on it, making it the more "
                                    "field-tested - not because the other is "
                                    "known to be less stable."));
                    }
                    // Settled by what is installed, not by preference, so the
                    // prior-choices block must not undo it.
                    openMwOverriddenGroups.insert((quint64(si) << 16) | quint64(gi));
                }
            }
        }


        // Pass H: one mod, offered with and without a framework.
        //
        // Vehicle Overhaul Continued opens on "Vehicle Overhaul Continued"
        // vs "Vehicle Overhaul Continued (No BOS) [v1.2.2]", defaulting to
        // the No-BOS half - which on a list that HAS Base Object Swapper
        // silently gives up every swap the framework would drive. Worse,
        // Pass G briefly made it so: its mentioned-word lookup identified
        // the "(No BOS)" option AS Base Object Swapper and green-ticked the
        // wrong half, which is why chooseFrameworkOption now refuses negated
        // names outright. The marker is the evidence (fomod_hint.h), and the
        // modlist answers which half works.
        for (int si = 0; si < m_steps.size(); ++si) {
            const FomodStep &step = m_steps[si];
            for (int gi = 0; gi < step.groups.size(); ++gi) {
                const FomodGroup &group = step.groups[gi];
                if (group.type != QLatin1String("SelectExactlyOne")
                    && group.type != QLatin1String("SelectAtMostOne")) continue;

                QStringList names;
                names.reserve(group.plugins.size());
                for (const FomodPlugin &p : group.plugins) names << p.name;

                const auto v =
                    fomod::chooseFrameworkVariant(names, m_installedModNames);
                if (v.pick < 0 || v.pick >= m_buttons[si][gi].size()) continue;

                const auto annotate = [&](int pi, const QString &label,
                                          const QString &detail) {
                    QAbstractButton *btn = m_buttons[si][gi].value(pi);
                    if (!btn) return;
                    if (!label.isEmpty()) btn->setText(btn->text() + label);
                    const QString tip = btn->toolTip();
                    btn->setToolTip((tip.isEmpty() ? QString()
                                                   : tip + QStringLiteral("\n\n"))
                                    + detail);
                };
                const QString hBlurb = blurbFor(v.framework);
                const QString hWhat = v.framework
                    + (hBlurb.isEmpty() ? QString()
                                        : QStringLiteral(" \u2014 %1").arg(hBlurb));
                if (v.installed) {
                    annotate(v.positiveIdx,
                             QStringLiteral(" \u2705 uses %1, which is installed")
                                 .arg(v.framework),
                             QStringLiteral("%1 is in this modlist, so the "
                                            "variant built on it is the one "
                                            "that does everything the mod "
                                            "promises.").arg(v.framework));
                    addNoteUnder(m_buttons[si][gi].value(v.positiveIdx),
                        QStringLiteral("Uses %1. Installed in your mod list, "
                                       "so this variant does everything the "
                                       "mod promises.").arg(hWhat));
                    annotate(v.negativeIdx, QString(),
                             QStringLiteral("This variant is for lists WITHOUT "
                                            "%1 - which you have installed. "
                                            "The recommended option above "
                                            "actually uses it.").arg(v.framework));
                    addNoteUnder(m_buttons[si][gi].value(v.negativeIdx),
                        QStringLiteral("For lists without %1 \u2014 which you "
                                       "have, so the other variant is the "
                                       "better fit.").arg(v.framework));
                } else {
                    annotate(v.negativeIdx,
                             QStringLiteral(" \u2705 works without %1")
                                 .arg(v.framework),
                             QStringLiteral("%1 is not in this modlist, and "
                                            "this variant exists for exactly "
                                            "that. The other option would "
                                            "install swaps nothing ever "
                                            "triggers.").arg(v.framework));
                    addNoteUnder(m_buttons[si][gi].value(v.negativeIdx),
                        QStringLiteral("Works without %1, which is not in "
                                       "your mod list \u2014 the right pick "
                                       "for this list.").arg(v.framework));
                    annotate(v.positiveIdx,
                             QStringLiteral(" \u26a0\ufe0f needs %1")
                                 .arg(v.framework),
                             QStringLiteral("%1 is not installed in this "
                                            "modlist, so this variant's extra "
                                            "content would silently never "
                                            "fire.").arg(v.framework));
                    addNoteUnder(m_buttons[si][gi].value(v.positiveIdx),
                        QStringLiteral("Needs %1. Not in your mod list \u2014 "
                                       "its extra content would silently "
                                       "never fire.").arg(hWhat));
                }

                QAbstractButton *pick = m_buttons[si][gi].value(v.pick);
                if (pick && pick->isEnabled()) pick->setChecked(true);
                // Settled by what is installed, not by preference - the same
                // doctrine as Pass F - so the prior-choices block must not
                // undo it.
                openMwOverriddenGroups.insert((quint64(si) << 16) | quint64(gi));
            }
        }

        // Pass F: options whose description names a mod they REQUIRE.
        //
        // Grand Solitude's "SMIM Rotor" ships ticked and reads "Required
        // Static Mesh Improvement Mod - SMIM by Brumbek". With SMIM absent
        // that installs a mesh nothing loads correctly, and no URL is cited
        // so Pass E cannot see it. fomod::requiredMods reads the prose, and
        // only when what follows the keyword is shaped like a mod name - see
        // fomod_hint.h for the three corpus sentences that must stay quiet.
        //
        // This decides the tick in BOTH directions and overrides a stored
        // prior choice, unlike the passes above. Whether the required mod is
        // in the list right now is a fact about the modlist, not a
        // preference, and the same argument settles the Skyrim runtime pass.
        for (int si = 0; si < m_steps.size(); ++si) {
            const FomodStep &step = m_steps[si];
            for (int gi = 0; gi < step.groups.size(); ++gi) {
                const FomodGroup &group = step.groups[gi];
                // Only where a tick is independently settable. In an
                // exclusive group unticking means picking something else,
                // which is not ours to decide, so those are annotated only.
                const bool tickable = group.type == QLatin1String("SelectAny")
                                   || group.type == QLatin1String("SelectAtLeastOne");

                for (int pi = 0; pi < group.plugins.size() &&
                                 pi < m_buttons[si][gi].size(); ++pi) {
                    // Same negation guard as Pass E: a "(No X)" option that
                    // mentions X in its prose is not an option FOR X.
                    if (!fomod::negatedModIn(group.plugins[pi].name).isEmpty())
                        continue;

                    const QStringList needed =
                        fomod::requiredMods(group.plugins[pi].description,
                                            group.plugins[pi].name, group.name);
                    if (needed.isEmpty()) continue;

                    // Widen by the scene's acronyms, so "SMIM" finds a mod
                    // installed as "Static Mesh Improvement Mod" and back.
                    bool present = false;
                    QString firstName;
                    for (const QString &cand : mod_aliases::expand(needed)) {
                        if (firstName.isEmpty()) firstName = cand;
                        for (const QString &needle : needlesFor(cand)) {
                            const bool shortNeedle = (needle.length() < 8);
                            const QString pat = shortNeedle
                                ? (QLatin1String("^") + QRegularExpression::escape(needle) + QLatin1String("\\b"))
                                : (QLatin1String("\\b") + QRegularExpression::escape(needle) + QLatin1String("\\b"));
                            const QRegularExpression re(pat, QRegularExpression::CaseInsensitiveOption);
                            for (const QString &modName : std::as_const(m_installedModNames))
                                if (re.match(modName).hasMatch()) { present = true; break; }
                            if (present) break;
                        }
                        if (present) break;
                    }

                    QAbstractButton *btn = m_buttons[si][gi][pi];
                    if (!btn || !btn->isEnabled()) continue;

                    const quint64 key = (quint64(si) << 32) | (quint64(gi) << 16)
                                      | quint64(pi);
                    if (tickable) modlistSettledPlugins.insert(key, present);

                    // Short on the label, full sentence in the tooltip. The
                    // long form ran off the end of the dialog and buried the
                    // option's own name, which is what the user is reading.
                    const QString tip = btn->toolTip();
                    if (present) {
                        if (tickable) btn->setChecked(true);
                        btn->setText(btn->text()
                            + QStringLiteral(" \u2705 %1 \u2713").arg(firstName));
                        btn->setToolTip(
                            (tip.isEmpty() ? QString() : tip + QStringLiteral("\n\n"))
                            + QStringLiteral("%1 is installed, so this option works.")
                                  .arg(firstName));
                    } else {
                        if (tickable) btn->setChecked(false);
                        btn->setText(btn->text()
                            + QStringLiteral(" \u26A0\uFE0F needs %1").arg(firstName));
                        btn->setToolTip(
                            (tip.isEmpty() ? QString() : tip + QStringLiteral("\n\n"))
                            + QStringLiteral("This option requires %1, which is not "
                                             "installed in this modlist, so it has "
                                             "been unticked.").arg(firstName));
                    }
                }
            }
        }
    }

    // Apply prior choices over the defaults above. Format "si:gi:pi;..."
    // (step/group/plugin indices). Radio: check the stored plugin, QButtonGroup
    // clears the rest. Checkbox with any prior entry: uncheck all enabled, check
    // the stored ones. Groups with no prior entry keep the FOMOD defaults.
    if (!m_priorChoices.isEmpty()) {
        QSet<quint64> priorSet;
        auto encode = [](int si, int gi, int pi) -> quint64 {
            return (static_cast<quint64>(si) << 32)
                 | (static_cast<quint64>(gi) << 16)
                 | static_cast<quint64>(pi);
        };
        for (const QString &rec : m_priorChoices.split(';', Qt::SkipEmptyParts)) {
            const QStringList f = rec.split(':');
            if (f.size() == 3) {
                bool ok1, ok2, ok3;
                int si = f[0].toInt(&ok1), gi = f[1].toInt(&ok2), pi = f[2].toInt(&ok3);
                if (ok1 && ok2 && ok3)
                    priorSet.insert(encode(si, gi, pi));
            }
        }
        for (int si = 0; si < m_steps.size() && si < m_buttons.size(); ++si) {
            const FomodStep &step = m_steps[si];
            for (int gi = 0; gi < step.groups.size() && gi < m_buttons[si].size(); ++gi) {
                const FomodGroup &group = step.groups[gi];
                if (group.type == "SelectAll") continue; // all forced on

                bool hasPrior = false;
                for (int pi = 0; pi < group.plugins.size(); ++pi) {
                    if (priorSet.contains(encode(si, gi, pi))) { hasPrior = true; break; }
                }
                if (!hasPrior) continue;
                // OpenMW rule wins over stored choices.
                if (openMwOverriddenGroups.contains((quint64(si) << 16) | quint64(gi))) continue;

                bool exclusive = (group.type == "SelectExactlyOne" ||
                                  group.type == "SelectAtMostOne");
                if (exclusive) {
                    for (int pi = 0; pi < group.plugins.size() && pi < m_buttons[si][gi].size(); ++pi) {
                        QAbstractButton *btn = m_buttons[si][gi][pi];
                        if (btn->isEnabled() && priorSet.contains(encode(si, gi, pi))) {
                            btn->setChecked(true);
                            break;
                        }
                    }
                } else {
                    // Uncheck all enabled, then check stored ones. Keep Pass C's
                    // Recommended (mod-installed) picks ticked even if the prior
                    // omitted them, else label and state disagree.
                    for (int pi = 0; pi < group.plugins.size() && pi < m_buttons[si][gi].size(); ++pi) {
                        QAbstractButton *btn = m_buttons[si][gi][pi];
                        if (!btn->isEnabled()) continue;
                        const quint64 key = encode(si, gi, pi);
                        // A verdict from Pass E or F outranks the stored
                        // choice: the label says the cited mod is missing (or
                        // present), and a tick restored from a different
                        // modlist would contradict it on screen.
                        const auto settled = modlistSettledPlugins.constFind(key);
                        if (settled != modlistSettledPlugins.constEnd()) {
                            btn->setChecked(*settled);
                            continue;
                        }
                        btn->setChecked(priorSet.contains(key) ||
                                        recommendedInstalledPlugins.contains(key));
                    }
                }
            }
        }
    }

    // Navigation row
    auto *navLay = new QHBoxLayout;
    auto *cancelBtn = new QPushButton(T("fomod_cancel"), this);
    m_prevBtn    = new QPushButton(T("fomod_prev"), this);
    m_nextBtn    = new QPushButton(T("fomod_next"), this);
    m_installBtn = new QPushButton(T("fomod_install"), this);
    m_installBtn->setDefault(true);

    navLay->addWidget(cancelBtn);
    navLay->addStretch();
    navLay->addWidget(m_prevBtn);
    navLay->addWidget(m_nextBtn);
    navLay->addWidget(m_installBtn);
    mainLayout->addLayout(navLay);

    connect(cancelBtn,   &QPushButton::clicked, this, &QDialog::reject);
    connect(m_prevBtn,   &QPushButton::clicked, this, [this] {
        m_stack->setCurrentIndex(--m_curPage);
        updateButtons();
        defaultPreviewForStep(m_curPage);
    });
    connect(m_nextBtn,   &QPushButton::clicked, this, [this] {
        m_stack->setCurrentIndex(++m_curPage);
        updateButtons();
        defaultPreviewForStep(m_curPage);
    });
    connect(m_installBtn, &QPushButton::clicked, this, &QDialog::accept);

    updateButtons();
    defaultPreviewForStep(m_curPage);
}

void FomodWizard::updateButtons()
{
    int last = m_stack->count() - 1;
    m_prevBtn->setEnabled(m_curPage > 0);
    m_nextBtn->setVisible(m_curPage < last);
    m_installBtn->setVisible(m_curPage == last);

    if (!m_steps.isEmpty() && m_curPage < m_steps.size()) {
        m_titleLbl->setText(
            T("fomod_step_label")
                .arg(m_curPage + 1)
                .arg(m_steps.size())
                .arg(m_steps[m_curPage].name));
    } else {
        m_titleLbl->setText(T("fomod_wizard_title_generic"));
    }
}

// Apply selections and stage the install dir.
QString FomodWizard::applySelections()
{
    QString installDir = m_archiveRoot + "/fomod_install";

    // Start fresh
    if (QDir(installDir).exists())
        QDir(installDir).removeRecursively();
    QDir().mkpath(installDir);

    QStringList failed;   // sources we couldn't find or copy

    // Install one FomodFile entry (file or folder).
    auto installFile = [&](const FomodFile &f, bool isFolder) {
        QString normalizedDest = f.destination;
        normalizedDest.replace('\\', '/');

        QString src = fomod::resolvePath(m_archiveRoot, f.source);
        if (src.isEmpty() || !QFileInfo::exists(src)) {
            failed << f.source;
            return;
        }
        if (isFolder) {
            QString dst = normalizedDest.isEmpty()
                ? installDir
                : fomod::resolveDest(installDir, normalizedDest);
            fomod_copy::copyContents(src, dst);
        } else {
            const QString rel = normalizedDest.isEmpty()
                ? QFileInfo(src).fileName()
                : normalizedDest;
            const fomod::ResolvedPath dst = fomod::resolveDest(installDir, rel);
            if (!fomod_copy::copyFile(src, dst)) failed << f.source;

            // Patch-hub rescue: an .omwscripts manifest declares lua bodies the
            // FOMOD often doesn't list as separate <file>/<folder> entries
            // (Completionist Patch Hub, Nexus 58523: ships manifest +
            // scripts/.../*.lua but lists only the manifest). Pull the lua from
            // the manifest's parent dir so the install matches what OpenMW loads.
            if (src.endsWith(QLatin1String(".omwscripts"),
                             Qt::CaseInsensitive)) {
                fomod_scripts::installDeclaredScripts(
                    src, m_archiveRoot, installDir);
            }
        }
    };

    // Flags raised by picked plugins; drives the conditionalFileInstalls below.
    QHash<QString, QString> activeFlags;

    // 1. Required files (always)
    for (const FomodFile &f : m_requiredFiles)   installFile(f, false);
    for (const FomodFile &f : m_requiredFolders) installFile(f, true);

    // 2. Selected plugin files + their conditionFlags
    for (int si = 0; si < m_steps.size() && si < m_buttons.size(); ++si) {
        const FomodStep &step = m_steps[si];
        for (int gi = 0; gi < step.groups.size() && gi < m_buttons[si].size(); ++gi) {
            const FomodGroup &group = step.groups[gi];
            for (int pi = 0; pi < group.plugins.size() && pi < m_buttons[si][gi].size(); ++pi) {
                if (!m_buttons[si][gi][pi]->isChecked()) continue;
                const FomodPlugin &plugin = group.plugins[pi];
                for (const FomodFile &f : plugin.files)   installFile(f, false);
                for (const FomodFile &f : plugin.folders) installFile(f, true);
                for (const FomodFlagValue &fv : plugin.conditionFlags)
                    activeFlags.insert(fv.name, fv.value);
            }
        }
    }

    // 3. conditionalFileInstalls - test each pattern against the active flags.
    //    "And" (default): every flagDependency matches. "Or": at least one.
    //    Zero flagDeps = always install (unconditional fallback).
    for (const FomodPattern &pat : m_conditionalInstalls) {
        bool satisfied = false;
        if (pat.flagDeps.isEmpty()) {
            satisfied = true;
        } else if (pat.op.compare("Or", Qt::CaseInsensitive) == 0) {
            for (const FomodFlagValue &dep : pat.flagDeps) {
                if (activeFlags.value(dep.name) == dep.value) {
                    satisfied = true;
                    break;
                }
            }
        } else { // "And" (default)
            satisfied = true;
            for (const FomodFlagValue &dep : pat.flagDeps) {
                if (activeFlags.value(dep.name) != dep.value) {
                    satisfied = false;
                    break;
                }
            }
        }
        if (!satisfied) continue;
        for (const FomodFile &f : pat.files)   installFile(f, false);
        for (const FomodFile &f : pat.folders) installFile(f, true);
    }

    return installDir;
}

// Serialize button state for the modlist.
QString FomodWizard::collectChoices() const
{
    QStringList entries;
    for (int si = 0; si < m_steps.size() && si < m_buttons.size(); ++si) {
        const FomodStep &step = m_steps[si];
        for (int gi = 0; gi < step.groups.size() && gi < m_buttons[si].size(); ++gi) {
            const FomodGroup &group = step.groups[gi];
            for (int pi = 0; pi < group.plugins.size() && pi < m_buttons[si][gi].size(); ++pi) {
                if (m_buttons[si][gi][pi]->isChecked())
                    entries << QString("%1:%2:%3").arg(si).arg(gi).arg(pi);
            }
        }
    }
    return entries.join(';');
}
