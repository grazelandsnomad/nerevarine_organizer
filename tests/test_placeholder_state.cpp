// tests/test_placeholder_state.cpp
//
// Locks in the placeholder lifecycle role transitions (pending → installing →
// installed / cancelled).  These sequences were duplicated across MainWindow's
// install path (the 4-flag set ~7x, the "reset to not installed" sequence 3x,
// "mark installed" 2x); centralizing them in placeholder_state:: is only safe
// if the exact role effects are pinned down - that's what this file does.
//
// Operates on a standalone heap QListWidgetItem (no live QListWidget needed),
// hence the QtWidgets link but no QApplication.

#include "placeholder_state.h"
#include "modroles.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QListWidgetItem>
#include <QUuid>
#include <iostream>

static int s_passed = 0;
static int s_failed = 0;

static void check(const char *name, bool ok, const QString &hint = {})
{
    if (ok) {
        std::cout << "  \033[32m✓\033[0m " << name << "\n";
        ++s_passed;
    } else {
        std::cout << "  \033[31m✗\033[0m " << name;
        if (!hint.isEmpty()) std::cout << " (" << hint.toStdString() << ")";
        std::cout << "\n";
        ++s_failed;
    }
}

// -- Scenarios ---

static void testRestoreInteractiveFlags()
{
    std::cout << "\n[restoreInteractiveFlags sets the full interactive set]\n";
    QListWidgetItem it;
    it.setFlags(Qt::NoItemFlags);
    placeholder_state::restoreInteractiveFlags(&it);
    const auto f = it.flags();
    check("enabled",        f.testFlag(Qt::ItemIsEnabled));
    check("selectable",     f.testFlag(Qt::ItemIsSelectable));
    check("drag-enabled",   f.testFlag(Qt::ItemIsDragEnabled));
    check("user-checkable", f.testFlag(Qt::ItemIsUserCheckable));
}

static void testSetBusyFlags()
{
    std::cout << "\n[setBusyFlags drops drag + check while installing]\n";
    QListWidgetItem it;
    placeholder_state::restoreInteractiveFlags(&it);
    placeholder_state::setBusyFlags(&it);
    const auto f = it.flags();
    check("enabled",            f.testFlag(Qt::ItemIsEnabled));
    check("selectable",         f.testFlag(Qt::ItemIsSelectable));
    check("NOT drag-enabled",   !f.testFlag(Qt::ItemIsDragEnabled));
    check("NOT user-checkable", !f.testFlag(Qt::ItemIsUserCheckable));
}

static void testClearInstallTransients()
{
    std::cout << "\n[clearInstallTransients wipes the 4 in-flight hint roles]\n";
    QListWidgetItem it;
    it.setData(ModRole::IntendedModPath, "/x");
    it.setData(ModRole::PrevModPath,     "/y");
    it.setData(ModRole::MergeTargetPath, "/z");
    it.setData(ModRole::InstallToken,    QUuid::createUuid());
    placeholder_state::clearInstallTransients(&it);
    check("IntendedModPath cleared", it.data(ModRole::IntendedModPath).toString().isEmpty());
    check("PrevModPath cleared",     it.data(ModRole::PrevModPath).toString().isEmpty());
    check("MergeTargetPath cleared", it.data(ModRole::MergeTargetPath).toString().isEmpty());
    check("InstallToken cleared",    it.data(ModRole::InstallToken).toUuid().isNull());
}

static void testResetToNotInstalledKeepsCustomName()
{
    std::cout << "\n[resetToNotInstalled rolls back, keeps CustomName over fallback]\n";
    QListWidgetItem it;
    it.setData(ModRole::InstallStatus,    2);
    it.setData(ModRole::DownloadProgress, 42);
    it.setData(ModRole::ModPath,          "/mods/half_extracted");
    it.setData(ModRole::InstallToken,     QUuid::createUuid());
    it.setData(ModRole::CustomName,       "My Mod");
    it.setText("⠋ installing…");

    placeholder_state::resetToNotInstalled(&it, "archive-basename");
    check("status -> 0 (not installed)", it.data(ModRole::InstallStatus).toInt() == 0);
    check("download progress cleared",   it.data(ModRole::DownloadProgress).toString().isEmpty());
    check("mod path cleared",            it.data(ModRole::ModPath).toString().isEmpty());
    check("install token cleared",       it.data(ModRole::InstallToken).toUuid().isNull());
    check("text restored to CustomName", it.text() == "My Mod");
    check("interactive flags restored",  it.flags().testFlag(Qt::ItemIsUserCheckable));
}

static void testResetToNotInstalledFallbackName()
{
    std::cout << "\n[resetToNotInstalled falls back to the archive name]\n";
    QListWidgetItem it;        // no CustomName set
    it.setText("⠋ installing…");
    placeholder_state::resetToNotInstalled(&it, "Cool Mod-1234");
    check("text uses fallback",        it.text() == "Cool Mod-1234");
    check("fallback persisted to name", it.data(ModRole::CustomName).toString() == "Cool Mod-1234");
}

static void testMarkInstalled()
{
    std::cout << "\n[markInstalled rolls forward to installed-at-path]\n";
    QListWidgetItem it;
    it.setData(ModRole::CustomName,      "OAAB Data");
    it.setData(ModRole::InstallStatus,   2);
    it.setData(ModRole::DownloadProgress, 99);
    it.setData(ModRole::PrevModPath,     "/old");
    it.setData(ModRole::MergeTargetPath, "/target");
    it.setData(ModRole::InstallToken,    QUuid::createUuid());

    placeholder_state::markInstalled(&it, "/mods/OAAB_Data");
    check("status -> 1 (installed)",  it.data(ModRole::InstallStatus).toInt() == 1);
    check("item type is mod",         it.data(ModRole::ItemType).toString() == ItemType::Mod);
    check("mod path set",             it.data(ModRole::ModPath).toString() == "/mods/OAAB_Data");
    check("download progress cleared", it.data(ModRole::DownloadProgress).toString().isEmpty());
    check("transients cleared",       it.data(ModRole::PrevModPath).toString().isEmpty()
                                   && it.data(ModRole::MergeTargetPath).toString().isEmpty()
                                   && it.data(ModRole::InstallToken).toUuid().isNull());
    check("checked",                  it.checkState() == Qt::Checked);
    check("tooltip is the path",      it.toolTip() == "/mods/OAAB_Data");
    check("text from CustomName",     it.text() == "OAAB Data");
    check("DateAdded stamped (was unset)", it.data(ModRole::DateAdded).toDateTime().isValid());
}

static void testMarkInstalledKeepsExistingDate()
{
    std::cout << "\n[markInstalled keeps a valid DateAdded unless it was an update]\n";
    const QDateTime old = QDateTime::fromString("2024-01-01T00:00:00", Qt::ISODate);

    QListWidgetItem keep;
    keep.setData(ModRole::DateAdded, old);
    keep.setData(ModRole::UpdateAvailable, false);
    placeholder_state::markInstalled(&keep, "/mods/x");
    check("non-update install keeps the original date",
          keep.data(ModRole::DateAdded).toDateTime() == old);

    QListWidgetItem upd;
    upd.setData(ModRole::DateAdded, old);
    upd.setData(ModRole::UpdateAvailable, true);
    placeholder_state::markInstalled(&upd, "/mods/x");
    check("update install refreshes the date",
          upd.data(ModRole::DateAdded).toDateTime() != old);
    check("update flag cleared after install",
          !upd.data(ModRole::UpdateAvailable).toBool());
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    std::cout << "=== placeholder_state ===\n";
    testRestoreInteractiveFlags();
    testSetBusyFlags();
    testClearInstallTransients();
    testResetToNotInstalledKeepsCustomName();
    testResetToNotInstalledFallbackName();
    testMarkInstalled();
    testMarkInstalledKeepsExistingDate();

    std::cout << "\n"
              << s_passed << " passed, "
              << s_failed << " failed\n";
    return s_failed == 0 ? 0 : 1;
}
