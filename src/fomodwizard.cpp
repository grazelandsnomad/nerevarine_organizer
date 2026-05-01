#include "fomodwizard.h"
#include "fomod_copy.h"
#include "fomod_path.h"
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
#include <QSet>
#include <QStackedWidget>
#include <QXmlStreamReader>

// FOMOD detection

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

bool FomodWizard::hasFomod(const QString &archiveRoot)
{
    return !findModuleConfig(archiveRoot).isEmpty();
}

// Public entry point

std::expected<QString, QString>
FomodWizard::run(const QString &archiveRoot,
                 const QString &priorChoices,
                 QString *outChoices,
                 QWidget *parent,
                 const QStringList &installedModNames)
{
    FomodWizard dlg(archiveRoot, parent);
    dlg.m_priorChoices      = priorChoices;
    dlg.m_installedModNames = installedModNames;

    if (!dlg.parse()) {
        // Malformed XML - ask whether to fall back to raw install
        auto ans = QMessageBox::warning(
            parent,
            T("fomod_parse_error_title"),
            T("fomod_parse_error_body"),
            QMessageBox::Ok | QMessageBox::Cancel,
            QMessageBox::Ok);
        if (ans == QMessageBox::Ok) return archiveRoot;
        return std::unexpected(QStringLiteral("cancelled"));
    }

    // No optional steps - install required files silently
    if (dlg.m_steps.isEmpty()) {
        if (dlg.m_requiredFiles.isEmpty() && dlg.m_requiredFolders.isEmpty())
            return archiveRoot; // nothing special, treat as normal install
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
    std::function<void(const QString &, const QString &)> onDone)
{
    auto *dlg = new FomodWizard(archiveRoot, parent);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->m_priorChoices      = priorChoices;
    dlg->m_installedModNames = installedModNames;

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
    // Make the wizard a proper independent top-level window so multiple
    // wizards can be open simultaneously and the user can click between them.
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

// Constructor

FomodWizard::FomodWizard(const QString &archiveRoot, QWidget *parent)
    : QDialog(parent), m_archiveRoot(archiveRoot)
{
    setModal(true);
    setMinimumSize(540, 420);
}

// XML parser

static FomodFile parseFileAttrs(const QXmlStreamReader &xml)
{
    FomodFile f;
    f.source      = xml.attributes().value("source").toString();
    f.destination = xml.attributes().value("destination").toString();
    f.priority    = xml.attributes().value("priority").toInt();
    return f;
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
            // Root-level patterns: install <files>/<folder> when the
            // specified flag combinations are all satisfied by the selected
            // plugins' conditionFlags.  This is how "option→flag→files"
            // FOMODs (EKM Corkbulb Retexture and many others) actually
            // install content - ignoring this block produces an empty mod.
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
                                // fileDependency / dependencies nesting are
                                // not yet supported.  The common FOMOD dialect
                                // used by OpenMW mods relies on flagDependency
                                // only; adding the rest is scope for later.
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

// UI construction

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
    mainLayout->addWidget(m_stack, 1);

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

            bool exclusive = (group.type == "SelectExactlyOne" ||
                              group.type == "SelectAtMostOne");
            bool selectAll = (group.type == "SelectAll");

            QButtonGroup *btnGroup = exclusive ? new QButtonGroup(box) : nullptr;
            if (btnGroup) btnGroup->setExclusive(true);

            bool firstSelectable = true;

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
                    defaultOn = true; // SelectAny single-plugin groups default ON (common pattern, e.g. core components)
                }
                if (exclusive && firstSelectable && !notUsable) {
                    defaultOn = true;   // ensure at least one radio is on
                    firstSelectable = false;
                }
                btn->setChecked(defaultOn);
                if (required || notUsable) btn->setEnabled(false);

                if (!plugin.description.isEmpty())
                    btn->setToolTip(plugin.description);

                boxLay->addWidget(btn);
                m_buttons[si][gi].append(btn);
            }

            innerLay->addWidget(box);
        }
        innerLay->addStretch();
        scroll->setWidget(inner);
        pageLay->addWidget(scroll);
        m_stack->addWidget(page);
    }

    // ---
    // Smart defaults - two passes per exclusive group.
    //
    // Pass A - OpenMW rule (hard, prior choices do NOT override):
    //   When a group offers both an "OpenMW" plugin and an "MGE XE" plugin,
    //   always pick OpenMW.  Nerevarine is an OpenMW-only manager.
    //   Groups handled here are recorded in openMwOverriddenGroups so the
    //   prior-choices block below skips them.
    //
    // Pass B - modlist presence (annotation always shown; selection only
    //   changed when there are no stored prior choices for the group):
    //   Groups with "Yes"/"No" plugins whose step/group name contains an
    //   installed mod name → select Yes (present) or No (absent).
    //   Mod names are expanded to multiple search forms to handle variants
    //   such as "OAAB_Data" ↔ "OAAB Data" ↔ "OAAB".
    // ---

    // Declared here so the prior-choices block below can read it.
    QSet<quint64> openMwOverriddenGroups;
    // Checkbox-group plugins that Pass C auto-recommends because the named
    // mod is currently present in the modlist.  Key: (si<<32)|(gi<<16)|pi.
    // The prior-choices block ORs this in so a ✅ Recommended checkbox never
    // ends up unticked just because an earlier install left it unchecked.
    QSet<quint64> recommendedInstalledPlugins;

    {
        // Pre-parse prior choices: (si,gi) pairs that already have a stored
        // selection.  Pass B annotates but does NOT change selection for these.
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

        // Expand a mod display name into multiple search needles to handle
        // common naming variants:
        //   "OAAB_Data"               → ["OAAB_Data", "OAAB Data", "OAAB"]
        //   "Ashfall - Survival Sim"  → ["Ashfall - Survival Sim", "Ashfall"]
        //   "Canonical Gear (OpenMW)" → ["Canonical Gear (OpenMW)", "Canonical Gear"]
        auto needlesFor = [](const QString &modName) -> QStringList {
            static const QStringList kSeps{
                QStringLiteral(" - "), QStringLiteral(" ("), QStringLiteral("_")};
            QStringList needles;
            const QString n = modName.trimmed();
            if (n.length() < 4) return needles;
            needles << n;
            if (n.contains(u'_')) {             // "OAAB_Data" → "OAAB Data"
                QString sp = n;
                sp.replace(u'_', u' ');
                needles << sp;
            }
            int firstSep = -1;                  // earliest separator position
            for (const QString &sep : kSeps) {
                const int idx = n.indexOf(sep);
                if (idx >= 4 && (firstSep < 0 || idx < firstSep)) firstSep = idx;
            }
            if (firstSep >= 4) {
                const QString prefix = n.left(firstSep).trimmed();
                if (prefix.length() >= 4) needles << prefix;
            }
            return needles;
        };

        // Step names that are generic FOMOD page titles, not mod references.
        static const QStringList kGenericSteps = {
            "options", "settings", "configuration", "install options", "general",
            "welcome", "installation", "core", "main", "select", "choose",
            "finish", "complete", "introduction", "default", "config", "extras"
        };

        for (int si = 0; si < m_steps.size(); ++si) {
            const FomodStep &step = m_steps[si];
            for (int gi = 0; gi < step.groups.size(); ++gi) {
                const FomodGroup &group = step.groups[gi];
                const quint64 groupKey = (quint64(si) << 16) | quint64(gi);

                const bool exclusive  = (group.type == QLatin1String("SelectExactlyOne") ||
                                          group.type == QLatin1String("SelectAtMostOne"));
                const bool isSelectAll = (group.type == QLatin1String("SelectAll"));
                if (isSelectAll) continue;  // all plugins forced on, nothing to do

                if (exclusive) {
                    // ---
                    // Pass A: OpenMW vs MGE XE - always prefer OpenMW.
                    // ---
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

                    // ---
                    // Pass A2: Language groups - prefer English over Russian
                    // (and other non-English options).
                    // ---
                    {
                        int engIdx = -1, nonEngIdx = -1;
                        static const QStringList kEngTokens  = {
                            "eng", "english", "en"
                        };
                        static const QStringList kRusTokens  = {
                            "rus", "russian", "ru", "russ"
                        };
                        // Any option whose name is *only* a language token (no
                        // other words) counts - e.g. "English", "ENG", "RUS".
                        // We also accept "English version", "Russian patch", etc.
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
                                    QStringLiteral(" ✅ Recommended - English."));
                            }
                            openMwOverriddenGroups.insert(groupKey);
                            continue;  // skip Pass B for this group
                        }
                    }

                    // ---
                    // Pass B: Yes/No groups - check modlist presence.
                    // ---
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

                    // Generate needles from both the group name and the
                    // combined step+group context, then match against
                    // installed mod names (same direction as Pass C).
                    // For short needles (< 8 chars) require start-of-name
                    // match to avoid false positives.
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
                    } else {
                        const QString stepLower = step.name.trimmed().toLower();
                        if (stepLower.length() >= 4 && !kGenericSteps.contains(stepLower)) {
                            if (!hasPrior) noBtn->setChecked(true);
                            noBtn->setText(noBtn->text() +
                                QStringLiteral(" \u2705 Recommended. This mod is not currently present in the modlist."));
                        }
                    }

                } else {
                    // ---
                    // Pass C: Checkbox groups - check each plugin name against
                    // the modlist.  If a plugin name matches an installed mod,
                    // auto-check it and annotate.  No annotation when absent
                    // (unchecked is self-explanatory for optional items).
                    // ---
                    const bool hasPrior = priorGroups.contains(groupKey);
                    for (int pi = 0; pi < group.plugins.size(); ++pi) {
                        QAbstractButton *btn = m_buttons[si][gi].value(pi);
                        if (!btn || !btn->isEnabled()) continue;

                        const QString pluginName = group.plugins[pi].name.trimmed();
                        if (pluginName.length() < 4) continue;

                        // Match plugin name (and its aliases) against each mod name.
                        // For short needles (< 8 chars) require the match at the
                        // start of the mod name to avoid false positives like
                        // "MWSE" matching "Graphic Herbalism MWSE - OpenMW".
                        bool pluginInstalled = false;
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

                        if (pluginInstalled) {
                            if (!hasPrior) btn->setChecked(true);
                            btn->setText(btn->text() +
                                QStringLiteral(" \u2705 Recommended. The mod is currently present in the modlist."));
                            recommendedInstalledPlugins.insert(
                                (quint64(si) << 32) | (quint64(gi) << 16) | quint64(pi));
                        }
                    }
                }
            }
        }

        // ---
        // Pass D: Patch-hub auto-tick.
        //   For SelectAny groups where every plugin's <files> include an
        //   .omwscripts entry, tick all plugins by default.  This catches
        //   "patch hub" mods (Completionist Patch Hub and similar) whose
        //   plugins are named after target landmass mods the user is unlikely
        //   to have all of - Pass C would otherwise leave the group empty
        //   and the user would end up with only required content (the
        //   .omwscripts files at root) plus an empty `scripts/` placeholder,
        //   then OpenMW would fail to load the orphan script declarations.
        //   .omwscripts files are tiny and harmless when their target mod
        //   isn't loaded, so the over-install is the safer default.
        //   Skipped when prior choices already cover the group, so a user's
        //   explicit untick survives a re-run.
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
    }

    // Apply prior choices on top of the defaults set above.
    // priorChoices format: "si:gi:pi;..." where si/gi/pi are step/group/plugin indices.
    // For exclusive (radio) groups: check the stored plugin, QButtonGroup unchecks others.
    // For checkbox groups with any prior entry: uncheck all enabled plugins, then check
    // only the stored ones.  Groups with no prior entry keep the FOMOD defaults.
    if (!m_priorChoices.isEmpty()) {
        // Parse into a set of (si, gi, pi) triplets.
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
                if (group.type == "SelectAll") continue; // all forced on, nothing to override

                // Check whether any prior entry references this step+group.
                bool hasPrior = false;
                for (int pi = 0; pi < group.plugins.size(); ++pi) {
                    if (priorSet.contains(encode(si, gi, pi))) { hasPrior = true; break; }
                }
                if (!hasPrior) continue;
                // OpenMW rule takes precedence - do not let stored choices override it.
                if (openMwOverriddenGroups.contains((quint64(si) << 16) | quint64(gi))) continue;

                bool exclusive = (group.type == "SelectExactlyOne" ||
                                  group.type == "SelectAtMostOne");
                if (exclusive) {
                    // Radio group: check the stored plugin (auto-unchecks others).
                    for (int pi = 0; pi < group.plugins.size() && pi < m_buttons[si][gi].size(); ++pi) {
                        QAbstractButton *btn = m_buttons[si][gi][pi];
                        if (btn->isEnabled() && priorSet.contains(encode(si, gi, pi))) {
                            btn->setChecked(true);
                            break;
                        }
                    }
                } else {
                    // Checkbox group: uncheck all enabled, then check stored ones.
                    // Keep Pass C's ✅ Recommended (mod-is-installed) picks ticked
                    // even if the stored prior didn't include them - otherwise the
                    // label and the state would contradict each other.
                    for (int pi = 0; pi < group.plugins.size() && pi < m_buttons[si][gi].size(); ++pi) {
                        QAbstractButton *btn = m_buttons[si][gi][pi];
                        if (!btn->isEnabled()) continue;
                        const quint64 key = encode(si, gi, pi);
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
    });
    connect(m_nextBtn,   &QPushButton::clicked, this, [this] {
        m_stack->setCurrentIndex(++m_curPage);
        updateButtons();
    });
    connect(m_installBtn, &QPushButton::clicked, this, &QDialog::accept);

    updateButtons();
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

// Apply user selections and build the install directory

QString FomodWizard::applySelections()
{
    QString installDir = m_archiveRoot + "/fomod_install";

    // Start fresh
    if (QDir(installDir).exists())
        QDir(installDir).removeRecursively();
    QDir().mkpath(installDir);

    QStringList failed;   // source paths that could not be located or copied

    // Helper: install one FomodFile entry (file or folder)
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
                : installDir + "/" + normalizedDest;
            fomod_copy::copyContents(src, dst);
        } else {
            QString dst = normalizedDest.isEmpty()
                ? installDir + "/" + QFileInfo(src).fileName()
                : installDir + "/" + normalizedDest;
            QDir().mkpath(QFileInfo(dst).absolutePath());
            if (!QFile::copy(src, dst)) failed << f.source;
        }
    };

    // Collects flags raised by plugins the user picked - used below to
    // evaluate conditionalFileInstalls patterns.
    QHash<QString, QString> activeFlags;

    // 1. Required files (always installed)
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

    // 3. conditionalFileInstalls - evaluate each pattern against the
    //    flags raised above.  Match rule depends on the pattern's operator:
    //      · "And" (default): every flagDependency must match activeFlags
    //      · "Or":             at least one must match
    //    A pattern with zero flagDeps is treated as always-install (mirrors
    //    how FOMOD installers behave for unconditional fallback blocks).
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

// Serialize current button state for storage in the modlist

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
