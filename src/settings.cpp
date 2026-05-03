#include "settings.h"

#include <QByteArray>
#include <QSettings>
#include <QStringLiteral>
#include <QVariant>

// All key strings live here, exactly once.  Anything else in the codebase
// either calls one of these accessors, OR (for migrations) reads/writes a
// historical key literal that no longer has an accessor.

namespace {

// Top-level keys
constexpr auto kGamesList     = "games/list";
constexpr auto kGamesCurrent  = "games/current";

constexpr auto kWindowGeometry  = "window/geometry";
constexpr auto kWindowMaximized = "window/maximized";

constexpr auto kUiZoomPt              = "ui/zoom_pt";
constexpr auto kUiScaleFactor         = "ui/scale_factor";
constexpr auto kUiLanguage            = "ui/language";
constexpr auto kUiUtilityExplainer    = "ui/utility_explainer_seen";

constexpr auto kLootBannerDisabled  = "loot/banner_disabled";
constexpr auto kQueueVisible        = "queue/visible";
constexpr auto kSeparatorsHidden    = "separators/hidden_presets";
constexpr auto kLaunchSkipReboot    = "launch/skip_reboot_check";
constexpr auto kPatchesDeclined     = "patches/declined";
constexpr auto kGroundcoverApproved = "groundcover/approved";
constexpr auto kWizardCompleted     = "wizard/completed";
constexpr auto kSkipDesktopCheck    = "shortcuts/skipDesktopCheck";
constexpr auto kNexusApiKey         = "nexus/apikey";

// Legacy keys promoted out of QSettings on first launch under newer code.
constexpr auto kLegacyOpenmwPath         = "launch/openmw";
constexpr auto kLegacyOpenmwLauncherPath = "launch/openmw_launcher";
constexpr auto kLegacyModsDir            = "mods/dir";

// Forbidden mods - read-only, awaiting migration to the portable file.
constexpr auto kForbiddenCount    = "forbidden/count";
constexpr auto kForbiddenSeededV1 = "forbidden/seeded_v1";

// `games/<id>/...` builder
QString gameKey(const QString &id, const char *suffix)
{
    return QStringLiteral("games/") + id + QLatin1Char('/')
        + QString::fromLatin1(suffix);
}

// `games/<id>/profile/<name>/...` builder
QString modlistKey(const QString &id, const QString &profile, const char *suffix)
{
    return QStringLiteral("games/") + id + QStringLiteral("/profile/") + profile
        + QLatin1Char('/') + QString::fromLatin1(suffix);
}

// `toolbar/<actionId>` builder
QString toolbarKey(const QString &actionId)
{
    return QStringLiteral("toolbar/") + actionId;
}

// `ui/col_<col>` and `ui/col_w_<col>[+suffix]` builders
QString colVisKey(const QString &col)
{
    return QStringLiteral("ui/col_") + col;
}

QString colWidthKey(const QString &col, const QString &stateSuffix)
{
    return QStringLiteral("ui/col_w_") + col + stateSuffix;
}

} // namespace

// -- Game registry ---

QStringList Settings::gameIds()
{
    return QSettings().value(kGamesList).toStringList();
}

void Settings::setGameIds(const QStringList &ids)
{
    QSettings().setValue(kGamesList, ids);
}

QString Settings::currentGameId()
{
    return QSettings().value(kGamesCurrent, QStringLiteral("morrowind")).toString();
}

void Settings::setCurrentGameId(const QString &id)
{
    QSettings().setValue(kGamesCurrent, id);
}

// -- Per-game profile fields ---

QString Settings::displayName(const QString &gameId)
{
    return QSettings().value(gameKey(gameId, "name"), gameId).toString();
}

void Settings::setDisplayName(const QString &gameId, const QString &name)
{
    QSettings().setValue(gameKey(gameId, "name"), name);
}

QString Settings::modsDirFor(const QString &gameId)
{
    // No default here - the registry composes a per-game default itself
    // (legacy `mods/dir` for morrowind, `~/Games/<id>_mods` for others).
    return QSettings().value(gameKey(gameId, "mods_dir")).toString();
}

void Settings::setModsDirFor(const QString &gameId, const QString &dir)
{
    QSettings().setValue(gameKey(gameId, "mods_dir"), dir);
}

QString Settings::openmwPath(const QString &gameId)
{
    return QSettings().value(gameKey(gameId, "openmw_path")).toString();
}

void Settings::setOpenmwPath(const QString &gameId, const QString &path)
{
    QSettings().setValue(gameKey(gameId, "openmw_path"), path);
}

QString Settings::openmwLauncherPath(const QString &gameId)
{
    return QSettings().value(gameKey(gameId, "openmw_launcher_path")).toString();
}

void Settings::setOpenmwLauncherPath(const QString &gameId, const QString &path)
{
    QSettings().setValue(gameKey(gameId, "openmw_launcher_path"), path);
}

QString Settings::gameExePath(const QString &gameId)
{
    return QSettings().value(gameKey(gameId, "exe_path")).toString();
}

void Settings::setGameExePath(const QString &gameId, const QString &path)
{
    QSettings().setValue(gameKey(gameId, "exe_path"), path);
}

QString Settings::launcherExePath(const QString &gameId)
{
    return QSettings().value(gameKey(gameId, "launcher_exe_path")).toString();
}

void Settings::setLauncherExePath(const QString &gameId, const QString &path)
{
    QSettings().setValue(gameKey(gameId, "launcher_exe_path"), path);
}

QString Settings::iniDir(const QString &gameId)
{
    return QSettings().value(gameKey(gameId, "ini_dir")).toString();
}

void Settings::setIniDir(const QString &gameId, const QString &dir)
{
    QSettings().setValue(gameKey(gameId, "ini_dir"), dir);
}

// -- Modlist profiles ---

QStringList Settings::modlistProfileNames(const QString &gameId)
{
    return QSettings().value(gameKey(gameId, "profiles")).toStringList();
}

void Settings::setModlistProfileNames(const QString &gameId, const QStringList &names)
{
    QSettings().setValue(gameKey(gameId, "profiles"), names);
}

QString Settings::activeModlistProfileName(const QString &gameId)
{
    return QSettings().value(gameKey(gameId, "active_profile")).toString();
}

void Settings::setActiveModlistProfileName(const QString &gameId, const QString &name)
{
    QSettings().setValue(gameKey(gameId, "active_profile"), name);
}

QString Settings::modlistProfileModsDir(const QString &gameId, const QString &profile)
{
    return QSettings().value(modlistKey(gameId, profile, "mods_dir")).toString();
}

void Settings::setModlistProfileModsDir(const QString &gameId, const QString &profile, const QString &dir)
{
    QSettings().setValue(modlistKey(gameId, profile, "mods_dir"), dir);
}

QString Settings::modlistFilename(const QString &gameId, const QString &profile)
{
    return QSettings().value(modlistKey(gameId, profile, "modlist_filename")).toString();
}

void Settings::setModlistFilename(const QString &gameId, const QString &profile, const QString &name)
{
    QSettings().setValue(modlistKey(gameId, profile, "modlist_filename"), name);
}

QString Settings::loadOrderFilename(const QString &gameId, const QString &profile)
{
    return QSettings().value(modlistKey(gameId, profile, "loadorder_filename")).toString();
}

void Settings::setLoadOrderFilename(const QString &gameId, const QString &profile, const QString &name)
{
    QSettings().setValue(modlistKey(gameId, profile, "loadorder_filename"), name);
}

void Settings::removeModlistProfileGroup(const QString &gameId, const QString &profile)
{
    // QSettings::remove(prefix) drops the prefix and every nested key.
    QSettings().remove(QStringLiteral("games/") + gameId
                       + QStringLiteral("/profile/") + profile);
}

// -- Legacy keys ---

QString Settings::legacyOpenmwPath()
{
    return QSettings().value(kLegacyOpenmwPath).toString();
}

QString Settings::legacyOpenmwLauncherPath()
{
    return QSettings().value(kLegacyOpenmwLauncherPath).toString();
}

QString Settings::legacyModsDir()
{
    return QSettings().value(kLegacyModsDir).toString();
}

// -- Window state ---

QByteArray Settings::windowGeometry()
{
    return QSettings().value(kWindowGeometry).toByteArray();
}

void Settings::setWindowGeometry(const QByteArray &geom)
{
    QSettings().setValue(kWindowGeometry, geom);
}

bool Settings::windowMaximized()
{
    return QSettings().value(kWindowMaximized, false).toBool();
}

void Settings::setWindowMaximized(bool maximized)
{
    QSettings().setValue(kWindowMaximized, maximized);
}

// -- UI ---

int Settings::uiZoomPt(int defaultPt)
{
    return QSettings().value(kUiZoomPt, defaultPt).toInt();
}

void Settings::setUiZoomPt(int pt)
{
    QSettings().setValue(kUiZoomPt, pt);
}

double Settings::uiScaleFactor()
{
    return QSettings().value(kUiScaleFactor, 1.0).toDouble();
}

void Settings::setUiScaleFactor(double factor)
{
    QSettings().setValue(kUiScaleFactor, factor);
}

QString Settings::uiLanguage()
{
    return QSettings().value(kUiLanguage).toString();
}

void Settings::setUiLanguage(const QString &lang)
{
    QSettings().setValue(kUiLanguage, lang);
}

bool Settings::utilityExplainerSeen()
{
    return QSettings().value(kUiUtilityExplainer).toBool();
}

void Settings::setUtilityExplainerSeen(bool seen)
{
    QSettings().setValue(kUiUtilityExplainer, seen);
}

bool Settings::colVisible(const QString &col, bool defaultVisible)
{
    return QSettings().value(colVisKey(col), defaultVisible).toBool();
}

void Settings::setColVisible(const QString &col, bool visible)
{
    QSettings().setValue(colVisKey(col), visible);
}

int Settings::colWidth(const QString &col, const QString &stateSuffix, int defaultPx)
{
    return QSettings().value(colWidthKey(col, stateSuffix), defaultPx).toInt();
}

void Settings::setColWidth(const QString &col, const QString &stateSuffix, int px)
{
    QSettings().setValue(colWidthKey(col, stateSuffix), px);
}

// -- Misc single-purpose flags ---

bool Settings::lootBannerDisabled()
{
    return QSettings().value(kLootBannerDisabled, false).toBool();
}

void Settings::setLootBannerDisabled(bool disabled)
{
    QSettings().setValue(kLootBannerDisabled, disabled);
}

bool Settings::queueVisible(bool defaultVisible)
{
    return QSettings().value(kQueueVisible, defaultVisible).toBool();
}

void Settings::setQueueVisible(bool visible)
{
    QSettings().setValue(kQueueVisible, visible);
}

bool Settings::toolbarActionVisible(const QString &actionId, bool defaultVisible)
{
    return QSettings().value(toolbarKey(actionId), defaultVisible).toBool();
}

void Settings::setToolbarActionVisible(const QString &actionId, bool visible)
{
    QSettings().setValue(toolbarKey(actionId), visible);
}

QStringList Settings::hiddenSeparatorPresets()
{
    return QSettings().value(kSeparatorsHidden).toStringList();
}

void Settings::setHiddenSeparatorPresets(const QStringList &keys)
{
    QSettings().setValue(kSeparatorsHidden, keys);
}

bool Settings::skipRebootCheck()
{
    return QSettings().value(kLaunchSkipReboot, false).toBool();
}

void Settings::setSkipRebootCheck(bool skip)
{
    QSettings().setValue(kLaunchSkipReboot, skip);
}

QStringList Settings::declinedPatches()
{
    return QSettings().value(kPatchesDeclined).toStringList();
}

void Settings::setDeclinedPatches(const QStringList &keys)
{
    QSettings().setValue(kPatchesDeclined, keys);
}

QStringList Settings::groundcoverApproved()
{
    return QSettings().value(kGroundcoverApproved).toStringList();
}

void Settings::setGroundcoverApproved(const QStringList &keys)
{
    QSettings().setValue(kGroundcoverApproved, keys);
}

bool Settings::wizardCompleted()
{
    return QSettings().value(kWizardCompleted, false).toBool();
}

void Settings::setWizardCompleted(bool completed)
{
    QSettings().setValue(kWizardCompleted, completed);
}

bool Settings::skipDesktopCheck()
{
    return QSettings().value(kSkipDesktopCheck, false).toBool();
}

void Settings::setSkipDesktopCheck(bool skip)
{
    QSettings().setValue(kSkipDesktopCheck, skip);
}

QString Settings::nexusApiKey()
{
    return QSettings().value(kNexusApiKey).toString();
}

void Settings::setNexusApiKey(const QString &key)
{
    QSettings().setValue(kNexusApiKey, key);
}

void Settings::removeNexusApiKey()
{
    QSettings().remove(kNexusApiKey);
}

// -- Forbidden mods (legacy migration) ---

int Settings::forbiddenCount()
{
    return QSettings().value(kForbiddenCount, 0).toInt();
}

QString Settings::forbiddenName(int idx)
{
    return QSettings().value(QStringLiteral("forbidden/%1/name").arg(idx)).toString();
}

QString Settings::forbiddenUrl(int idx)
{
    return QSettings().value(QStringLiteral("forbidden/%1/url").arg(idx)).toString();
}

QString Settings::forbiddenAnnotation(int idx)
{
    return QSettings().value(QStringLiteral("forbidden/%1/annotation").arg(idx)).toString();
}

bool Settings::forbiddenSeededV1()
{
    return QSettings().value(kForbiddenSeededV1, false).toBool();
}

void Settings::setForbiddenSeededV1(bool seeded)
{
    QSettings().setValue(kForbiddenSeededV1, seeded);
}
