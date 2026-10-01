#include "fomodwizard.h"
#include "fomod_choices.h"
#include "fomod_hint.h"
#include "fomod_install.h"
#include "fomod_plan.h"
#include "mod_aliases.h"
#include "mod_match.h"
#include "fomod_path.h"
#include "translator.h"

#include <QAbstractButton>
#include <QBoxLayout>
#include <QButtonGroup>
#include <QCheckBox>
#include <QCryptographicHash>
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

#include <functional>

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
                 const QStringList &installedNexusUrls,
                 const game_runtime::Probe &runtime)
{
    FomodWizard dlg(archiveRoot, parent);
    dlg.m_runtime           = runtime;
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
    std::function<void(const Decision &)> onDone,
    const QString &gameId,
    const QStringList &installedNexusUrls,
    const game_runtime::Probe &runtime)
{
    auto *dlg = new FomodWizard(archiveRoot, parent);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->m_priorChoices      = priorChoices;
    dlg->m_installedModNames = installedModNames;
    dlg->m_gameId            = gameId;
    dlg->m_runtime           = runtime;
    dlg->m_installedNexusKeys = nexusKeys(installedNexusUrls);

    const bool parsed = dlg->parse();
    // Unparsable ones too: an installer this wizard cannot read is a bug.
    keepForCorpus(findModuleConfig(dlg->m_archiveRoot), dlg->m_modName);
    if (!parsed) {
        auto ans = QMessageBox::warning(
            parent,
            T("fomod_parse_error_title"),
            T("fomod_parse_error_body"),
            QMessageBox::Ok | QMessageBox::Cancel,
            QMessageBox::Ok);
        delete dlg;
        Decision d;
        if (ans == QMessageBox::Ok) d.kind = Decision::Kind::InstallRaw;
        onDone(d);
        return;
    }

    if (dlg->m_steps.isEmpty()) {
        Decision d;
        d.kind = Decision::Kind::InstallPlan;
        d.plan = dlg->plannedInstall();
        delete dlg;
        onDone(d);
        return;
    }

    dlg->buildUi();
    // Top-level non-modal so multiple wizards can be open at once.
    dlg->setWindowModality(Qt::NonModal);
    dlg->setWindowFlag(Qt::Window, true);

    QObject::connect(dlg, &QDialog::accepted, dlg,
                     [dlg, onDone]() {
        Decision d;
        d.kind    = Decision::Kind::InstallPlan;
        d.choices = dlg->collectChoices();
        d.plan    = dlg->plannedInstall();
        onDone(d);
    });
    QObject::connect(dlg, &QDialog::rejected, dlg,
                     [onDone]() { onDone(Decision{}); });

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

void FomodWizard::keepForCorpus(const QString &configPath, const QString &modName)
{
    const QString dir = qEnvironmentVariable("NRV_FOMOD_CORPUS");
    if (dir.isEmpty() || configPath.isEmpty()) return;
    QFile in(configPath);
    if (!in.open(QIODevice::ReadOnly)) return;
    const QByteArray bytes = in.readAll();

    QString name = modName.trimmed().isEmpty()
        ? QFileInfo(QFileInfo(configPath).absolutePath()).dir().dirName()
        : modName.trimmed();
    static const QRegularExpression kUnsafe(QStringLiteral("[^A-Za-z0-9._-]+"));
    name.replace(kUnsafe, QStringLiteral("-"));
    const QString hash = QString::fromLatin1(
        QCryptographicHash::hash(bytes, QCryptographicHash::Sha1).toHex().left(6));
    const QString out = QDir(dir).filePath(name.left(80) + QStringLiteral("__") + hash
                                           + QStringLiteral(".xml"));
    if (QFileInfo::exists(out)) return;
    QDir().mkpath(dir);
    QFile f(out);
    if (f.open(QIODevice::WriteOnly)) f.write(bytes);
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

// A visible one-line explanation under an option, for a choice the wizard
// made for the user. Grey and indented, so it reads as a footnote to the row
// above it, not another option. Inserted directly under its option, so of
// several notes the last made shows first.
static void addNoteUnder(QAbstractButton *btn, const QString &text)
{
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
    // The synthetic "None" radio of each SelectAtMostOne group, by (si<<16)|gi.
    QHash<quint64, QAbstractButton *> noneButtons;
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

            const bool selectAtMostOne = (group.type == "SelectAtMostOne");
            const bool exclusive = (group.type == "SelectExactlyOne") || selectAtMostOne;

            QButtonGroup *btnGroup = exclusive ? new QButtonGroup(box) : nullptr;
            if (btnGroup) btnGroup->setExclusive(true);

            // Born blank: what starts ticked, what each says and whether it can
            // be changed is the plan's to decide (fomod_plan), applied below.
            for (int pi = 0; pi < group.plugins.size(); ++pi) {
                const FomodPlugin &plugin = group.plugins[pi];

                QAbstractButton *btn;
                if (exclusive) {
                    auto *rb = new QRadioButton(plugin.name, box);
                    if (btnGroup) btnGroup->addButton(rb);
                    btn = rb;
                } else {
                    btn = new QCheckBox(plugin.name, box);
                }

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
                boxLay->addWidget(noneBtn);
                noneButtons.insert((quint64(si) << 16) | quint64(gi), noneBtn);
            }

            // boxLay is owned by `box`; analyzer can't see the parent link.
            innerLay->addWidget(box); // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)
        }
        innerLay->addStretch();
        scroll->setWidget(inner);
        pageLay->addWidget(scroll);
        m_stack->addWidget(page);
    }

    // What to tick and say before the user touches anything. Decided in
    // fomod_plan, where a test reaches every decision without building a
    // dialog; applied here.
    fomod::PlanContext ctx;
    ctx.installedModNames  = m_installedModNames;
    ctx.installedNexusKeys = m_installedNexusKeys;
    ctx.gameId             = m_gameId;
    ctx.runtime            = m_runtime;
    ctx.priorChoices       = m_priorChoices;
    const fomod::Plan plan = fomod::planSelections(m_steps, ctx);
    for (int si = 0; si < plan.size() && si < m_buttons.size(); ++si) {
        for (int gi = 0; gi < plan[si].size() && gi < m_buttons[si].size(); ++gi) {
            const fomod::GroupPlan &gp = plan[si][gi];
            const QList<QAbstractButton *> &btns = m_buttons[si][gi];
            for (int pi = 0; pi < gp.options.size() && pi < btns.size(); ++pi) {
                const fomod::OptionPlan &o = gp.options[pi];
                QAbstractButton *btn = btns[pi];
                btn->setText(o.text);
                btn->setToolTip(o.toolTip);
                btn->setEnabled(o.enabled);
                // A radio is only ever ticked; its group unticks the rest.
                if (!gp.exclusive || o.checked) btn->setChecked(o.checked);
                for (const QString &note : o.notes) addNoteUnder(btn, note);
            }
            if (gp.hasNone && gp.noneChecked)
                if (QAbstractButton *none = noneButtons.value((quint64(si) << 16) | quint64(gi)))
                    none->setChecked(true);
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

// What the buttons say to install, as a plan for fomod_install::execute.
fomod_install::Plan FomodWizard::plannedInstall() const
{
    fomod_install::Plan plan;
    plan.archiveRoot = m_archiveRoot;
    plan.installDir  = m_archiveRoot + "/fomod_install";

    auto add = [&plan](const QList<FomodFile> &files, bool isFolder) {
        for (const FomodFile &f : files)
            plan.copies.append({f.source, f.destination, isFolder});
    };

    // Flags raised by picked plugins; drives the conditionalFileInstalls below.
    QHash<QString, QString> activeFlags;

    // 1. Required files (always)
    add(m_requiredFiles,   false);
    add(m_requiredFolders, true);

    // 2. Selected plugin files + their conditionFlags
    for (int si = 0; si < m_steps.size() && si < m_buttons.size(); ++si) {
        const FomodStep &step = m_steps[si];
        for (int gi = 0; gi < step.groups.size() && gi < m_buttons[si].size(); ++gi) {
            const FomodGroup &group = step.groups[gi];
            for (int pi = 0; pi < group.plugins.size() && pi < m_buttons[si][gi].size(); ++pi) {
                if (!m_buttons[si][gi][pi]->isChecked()) continue;
                const FomodPlugin &plugin = group.plugins[pi];
                add(plugin.files,   false);
                add(plugin.folders, true);
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
        add(pat.files,   false);
        add(pat.folders, true);
    }

    return plan;
}

// Apply selections and stage the install dir, here and now.
QString FomodWizard::applySelections()
{
    const fomod_install::Plan plan = plannedInstall();
    fomod_install::execute(plan);
    return plan.installDir;
}

// Serialize button state for the modlist.
QString FomodWizard::collectChoices() const
{
    // Named, not bare positions: see fomod_choices.h for what positions alone
    // did to OAAB_Data's picks when its installer was reordered.
    QSet<quint64> checked;
    for (int si = 0; si < m_steps.size() && si < m_buttons.size(); ++si) {
        const FomodStep &step = m_steps[si];
        for (int gi = 0; gi < step.groups.size() && gi < m_buttons[si].size(); ++gi) {
            const FomodGroup &group = step.groups[gi];
            for (int pi = 0; pi < group.plugins.size() && pi < m_buttons[si][gi].size(); ++pi) {
                if (m_buttons[si][gi][pi]->isChecked())
                    checked.insert(fomod_choices::key(si, gi, pi));
            }
        }
    }
    return fomod_choices::encode(m_steps, checked);
}
