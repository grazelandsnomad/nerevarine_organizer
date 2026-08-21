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

        // Missing deps: labels are already human-readable.
        if (item->data(ModRole::HasMissingDependency).toBool()) {
            const QStringList entries =
                item->data(ModRole::MissingDependencies).toStringList();
            for (const QString &entry : entries)
                r.missingDeps << QString("%1: %2").arg(name, entry);
        }

        // Empty install: marked installed but no plugins on disk. Resource-only
        // mods are fine, so only flag when there are also no data= roots and no
        // shaders. If the folder has any files at all, accept it (unknown format).
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

void addScriptExtender(Result &into, const skse_check::Findings &f)
{
    if (f.empty()) return;

    if (f.loaderMismatch) {
        into.scriptExtenderNotes
            << T("launch_warn_skse_loader").arg(f.loaderRuntime.shortString(),
                                                f.game.shortString());
    }
    if (f.missingDatabase) {
        into.scriptExtenderNotes
            << T("launch_warn_skse_no_db").arg(f.game.shortString());
    }
    if (f.stale.isEmpty()) return;

    // Said once, above the list. Three sentences, in the order somebody acts
    // on them, because the first version of this said the right things in the
    // wrong order and got the script extender updated instead: the extender
    // was already correct, the check had established that, and the dialog
    // never mentioned it.
    // "updated to" would be a claim about history nobody checked: all that is
    // known is the version on disk now and that these mods are behind it.
    QStringList explain;
    explain << (f.stale.size() == 1
                    ? T("launch_warn_skse_lead_one").arg(f.game.shortString())
                    : T("launch_warn_skse_lead").arg(f.game.shortString())
                                                 .arg(f.stale.size()));

    // Only with the file name behind it, and only when the check actually
    // looked: "the extender is fine" is a claim, not a reassurance.
    if (!f.loaderMismatch && !f.loaderFile.isEmpty())
        explain << T("launch_warn_skse_not_the_extender").arg(f.loaderFile);

    explain << T("launch_warn_skse_each_is_a_mod").arg(f.game.shortString());
    into.scriptExtenderExplain = explain.join(QStringLiteral("\n\n"));

    for (const skse_check::Stale &s : f.stale) {
        const QString mod  = s.mod.isEmpty() ? T("launch_warn_skse_no_mod") : s.mod;
        const QString when = s.built.toString(QStringLiteral("yyyy-MM-dd"));
        into.scriptExtenderStale
            << (s.declaredFor.valid
                    ? T("launch_warn_skse_stale_for")
                          .arg(mod, s.file, when, s.declaredFor.shortString())
                    : T("launch_warn_skse_stale").arg(mod, s.file, when));
        into.scriptExtenderMods << s.mod;
    }
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
    // Last, and closest to the buttons: it is the one that actually stops the
    // game rather than degrading it.
    body += formatSection(T("launch_warn_section_skse"),
                          warnings.scriptExtenderStale);

    QDialog dlg(parent);
    dlg.setWindowTitle(T("launch_warn_title"));
    dlg.setMinimumSize(680, 440);
    auto *v = new QVBoxLayout(&dlg);

    // A stale script-extender plugin does not "may misbehave": the game shows
    // one message box and quits. When that is the whole of it, say so.
    const bool onlySkse = warnings.missingDeps.isEmpty()
                       && warnings.emptyInstalls.isEmpty()
                       && warnings.forbiddenEnabled.isEmpty()
                       && !warnings.scriptExtenderStale.isEmpty();
    auto *header = new QLabel(onlySkse
                                  ? T("launch_warn_header_skse")
                                  : T("launch_warn_header").arg(warnings.total()),
                              &dlg);
    header->setWordWrap(true);
    header->setStyleSheet("font-weight: bold; padding: 4px 2px;");
    v->addWidget(header);

    // Sentences go here rather than into the list below. That list is
    // monospace and deliberately does not wrap, which suits a column of mod
    // names and cuts a paragraph off at the window edge.
    QStringList prose = warnings.scriptExtenderNotes;
    if (!warnings.scriptExtenderExplain.isEmpty())
        prose << warnings.scriptExtenderExplain;
    if (!prose.isEmpty()) {
        auto *note = new QLabel(prose.join(QStringLiteral("\n\n")), &dlg);
        note->setWordWrap(true);
        note->setTextInteractionFlags(Qt::TextSelectableByMouse);
        note->setStyleSheet("padding: 2px 2px 6px 2px;");
        v->addWidget(note);
    }

    auto *txt = new QPlainTextEdit(&dlg);
    txt->setReadOnly(true);
    txt->setLineWrapMode(QPlainTextEdit::NoWrap);
    txt->setFont(QFont("monospace"));
    txt->setPlainText(body.trimmed());
    v->addWidget(txt, 1);

    auto *suppress = new QCheckBox(T("launch_warn_suppress"), &dlg);
    v->addWidget(suppress);

    auto *btns = new QDialogButtonBox(&dlg);
    // The step that was missing. Every row is a mod with a Nexus page, so
    // "has the author shipped a build for this game version" is a question
    // the manager can answer, and leaving the user to go and ask it by hand
    // is how the wrong thing got updated.
    QPushButton *checkBtn = nullptr;
    if (!warnings.scriptExtenderStale.isEmpty()) {
        checkBtn = btns->addButton(
            T("launch_warn_skse_check_updates").arg(warnings.scriptExtenderStale.size()),
            QDialogButtonBox::ActionRole);
    }
    auto *launchBtn = btns->addButton(T("launch_warn_launch_anyway"),
                                       QDialogButtonBox::DestructiveRole);
    btns->addButton(QDialogButtonBox::Cancel);
    bool wantsCheck = false;
    if (checkBtn)
        QObject::connect(checkBtn, &QPushButton::clicked, &dlg,
                         [&wantsCheck, &dlg]() { wantsCheck = true; dlg.reject(); });
    QObject::connect(launchBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
    QObject::connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    v->addWidget(btns);

    const bool proceed = (dlg.exec() == QDialog::Accepted);
    // Suppressing only counts when the user chose to go ahead: ticking the box
    // and then cancelling is not a request to be told nothing next time.
    return Choice{proceed, proceed && suppress->isChecked(), wantsCheck};
}

bool refuseIfRebootPending(QWidget *parent)
{
    // Escape hatch: the heuristic false-fires on NixOS / sandboxes / odd module
    // layouts. "Run anyway (don't ask again)" disables the gate permanently.
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

    // Use a reboot/update glyph from the icon theme instead of the warning
    // triangle (this is advisory, not a crash). Falls back to Warning if the
    // theme has none of the four.
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
