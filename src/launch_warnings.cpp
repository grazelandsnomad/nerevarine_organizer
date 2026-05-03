#include "launch_warnings.h"

#include "settings.h"

#include <QAbstractButton>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QFont>
#include <QIcon>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSysInfo>
#include <QUrl>
#include <QVBoxLayout>
#include <Qt>

#include "forbidden_mods.h"
#include "modroles.h"
#include "pluginparser.h"
#include "reboot_check.h"
#include "translator.h"

namespace launch_warnings {
namespace {

bool hasShaderFile(const QString &modPath)
{
    static const QStringList shaderExts{".frag", ".vert", ".oglsl", ".glsl"};
    if (!QFileInfo(modPath).isDir()) return false;
    QDir d(modPath);
    for (const QString &f : d.entryList(QDir::Files, QDir::Name))
        for (const QString &ext : shaderExts)
            if (f.endsWith(ext, Qt::CaseInsensitive)) return true;
    for (const QString &sub : d.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        QDir subD(modPath + "/" + sub);
        for (const QString &f : subD.entryList(QDir::Files, QDir::Name))
            for (const QString &ext : shaderExts)
                if (f.endsWith(ext, Qt::CaseInsensitive)) return true;
    }
    return false;
}

bool dirContainsAnyFile(const QString &path)
{
    if (!QFileInfo(path).isDir()) return false;
    QDirIterator it(path,
        QDir::Files | QDir::NoDotAndDotDot,
        QDirIterator::Subdirectories);
    return it.hasNext();
}

} // namespace

Result scan(QListWidget *list,
            const ForbiddenModsRegistry *forbidden,
            const QString &gameId)
{
    static const QStringList contentExts{
        ".esp", ".esm", ".omwaddon", ".omwscripts"
    };

    Result r;
    if (!list) return r;

    for (int i = 0; i < list->count(); ++i) {
        auto *item = list->item(i);
        if (item->data(ModRole::ItemType).toString() != ItemType::Mod) continue;
        if (item->checkState() != Qt::Checked) continue;

        QString name = item->data(ModRole::CustomName).toString();
        if (name.isEmpty()) name = item->text();

        // Missing dependencies - already human-readable labels.
        if (item->data(ModRole::HasMissingDependency).toBool()) {
            const QStringList entries =
                item->data(ModRole::MissingDependencies).toStringList();
            for (const QString &entry : entries)
                r.missingDeps << QString("%1: %2").arg(name, entry);
        }

        // Empty installs - mod marked installed but no plugins on disk.
        // (Resource-only mods are valid; only flag when there are also no
        // collectable data= roots and no shader files. Last resort: if the
        // folder has ANY files at all, accept it - we just don't recognise
        // the format.)
        if (item->data(ModRole::InstallStatus).toInt() == 1) {
            const QString modPath = item->data(ModRole::ModPath).toString();
            if (!modPath.isEmpty()
             && plugins::collectDataFolders(modPath, contentExts).isEmpty()
             && plugins::collectResourceFolders(modPath).isEmpty()
             && !hasShaderFile(modPath)
             && !dirContainsAnyFile(modPath)) {
                r.emptyInstalls
                    << QString("%1: no plugin files found on disk").arg(name);
            }
        }

        // Forbidden mods currently enabled.
        const QString url = item->data(ModRole::NexusUrl).toString();
        if (forbidden && !url.isEmpty() && !gameId.isEmpty()) {
            const QStringList p = QUrl(url).path().split('/', Qt::SkipEmptyParts);
            if (p.size() >= 3 && p[1] == "mods") {
                bool ok; int modId = p[2].toInt(&ok);
                if (ok) {
                    if (const ForbiddenMod *f = forbidden->find(p[0], modId)) {
                        const QString reason = f->annotation.trimmed();
                        r.forbiddenEnabled << (reason.isEmpty()
                            ? QString("%1: on the forbidden list").arg(name)
                            : QString("%1: on the forbidden list (%2)")
                                  .arg(name, reason));
                    }
                }
            }
        }
    }

    return r;
}

Choice showDialog(QWidget *parent, const Result &warnings)
{
    auto formatSection = [](const QString &heading, const QStringList &rows) {
        if (rows.isEmpty()) return QString();
        QString block = heading + "\n";
        const int cap = 15;
        for (int i = 0; i < rows.size() && i < cap; ++i)
            block += "  • " + rows.at(i) + "\n";
        if (rows.size() > cap)
            block += "  " + T("launch_warn_entry_more")
                               .arg(rows.size() - cap) + "\n";
        return block + "\n";
    };

    QString body;
    body += formatSection(T("launch_warn_section_deps"),      warnings.missingDeps);
    body += formatSection(T("launch_warn_section_empty"),     warnings.emptyInstalls);
    body += formatSection(T("launch_warn_section_forbidden"), warnings.forbiddenEnabled);

    QDialog dlg(parent);
    dlg.setWindowTitle(T("launch_warn_title"));
    dlg.setMinimumSize(680, 440);
    auto *v = new QVBoxLayout(&dlg);

    auto *header = new QLabel(T("launch_warn_header").arg(warnings.total()), &dlg);
    header->setWordWrap(true);
    header->setStyleSheet("font-weight: bold; padding: 4px 2px;");
    v->addWidget(header);

    auto *txt = new QPlainTextEdit(&dlg);
    txt->setReadOnly(true);
    txt->setLineWrapMode(QPlainTextEdit::NoWrap);
    txt->setFont(QFont("monospace"));
    txt->setPlainText(body.trimmed());
    v->addWidget(txt, 1);

    auto *suppress = new QCheckBox(T("launch_warn_suppress"), &dlg);
    v->addWidget(suppress);

    auto *btns = new QDialogButtonBox(&dlg);
    auto *launchBtn = btns->addButton(T("launch_warn_launch_anyway"),
                                       QDialogButtonBox::DestructiveRole);
    btns->addButton(QDialogButtonBox::Cancel);
    QObject::connect(launchBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
    QObject::connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    v->addWidget(btns);

    const bool proceed = (dlg.exec() == QDialog::Accepted);
    return Choice{proceed, proceed && suppress->isChecked()};
}

bool refuseIfRebootPending(QWidget *parent)
{
    // Persistent escape hatch for users on NixOS / sandboxes / unusual module
    // layouts where the heuristic produces false positives. Once they pick
    // "Run anyway (don't ask again)" the gate stops firing for good.
    if (Settings::skipRebootCheck())
        return false;
    if (!isRebootPending()) return false;

    QMessageBox box(QMessageBox::Warning,
                    T("launch_reboot_pending_title"),
                    T("launch_reboot_pending_body")
                        .arg(QSysInfo::kernelVersion()),
                    QMessageBox::NoButton,
                    parent);
    auto *cancelBtn = box.addButton(QMessageBox::Cancel);
    auto *runOnceBtn = box.addButton(T("launch_reboot_pending_run_once"),
                                     QMessageBox::AcceptRole);
    auto *runAlwaysBtn = box.addButton(T("launch_reboot_pending_run_always"),
                                       QMessageBox::AcceptRole);
    box.setDefaultButton(cancelBtn);

    // Prefer a reboot/update glyph from the freedesktop icon theme over the
    // Warning triangle - this is an advisory, not a crash report. Falls back
    // to Warning when the theme lacks all four candidates.
    const QIcon themed =
        QIcon::fromTheme(QStringLiteral("system-reboot"),
        QIcon::fromTheme(QStringLiteral("system-restart"),
        QIcon::fromTheme(QStringLiteral("system-software-update"),
        QIcon::fromTheme(QStringLiteral("software-update-available")))));
    const QPixmap pm = themed.pixmap(48, 48);
    if (!pm.isNull())
        box.setIconPixmap(pm);
    box.exec();

    QAbstractButton *clicked = box.clickedButton();
    if (clicked == runAlwaysBtn) {
        Settings::setSkipRebootCheck(true);
        return false;
    }
    if (clicked == runOnceBtn) return false;
    return true;
}

} // namespace launch_warnings
