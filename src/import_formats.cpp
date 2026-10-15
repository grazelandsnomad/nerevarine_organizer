#include "import_formats.h"

#include "nxmurl.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonValue>
#include <QRegularExpression>
#include <QSettings>
#include <QTextStream>
#include <QUrl>

#include <algorithm>

namespace import_formats {

int readMO2MetaId(const QString &metaIniPath)
{
    // QSettings keeps the keys of an ini's [General] section at the root, so
    // they are read as "modid", not "General/modid" - the prefixed spelling
    // never found anything, and every MO2 import used to fall back to a
    // search link. The prefixed form is still tried for a file whose
    // section is spelled [%General].
    QSettings meta(metaIniPath, QSettings::IniFormat);
    const int id = meta.value("modid", meta.value("General/modid", 0)).toInt();
    if (id <= 0) return 0;
    const QString repo = meta.value("repository", meta.value("General/repository"))
                             .toString().trimmed().toLower();
    return (repo == "nexus" || repo.isEmpty()) ? id : 0;
}

QStringList mo2ModlistLines(const QString &contents)
{
    QStringList lines;
    for (const QString &raw : contents.split(QLatin1Char('\n'))) {
        const QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith('#')) continue;
        lines << line;
    }
    std::reverse(lines.begin(), lines.end());
    return lines;
}

ModEntry separatorRow(const QString &name)
{
    ModEntry e = ModEntry::named(name);
    e.itemType  = ItemType::Separator;
    e.bgColor   = QColor(55, 55, 75);
    e.fgColor   = QColor(Qt::white);
    e.itemFlags = Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled;
    return e;
}

std::optional<MO2Row> mo2Row(const QString &line, const QString &modsDir,
                             const QString &nexusGame, bool linkFolder)
{
    if (line.isEmpty()) return std::nullopt;
    const QChar prefix = line[0];
    QString name = line.mid(1).trimmed();
    if (name.isEmpty()) return std::nullopt;
    // `*` entries are MO2 "unmanaged" / overwrite markers - nothing to import.
    if (prefix == '*') return std::nullopt;

    const bool enabled = (prefix == '+');
    const bool isSep   = name.endsWith(QLatin1String("_separator"));
    if (isSep) name.chop(QStringLiteral("_separator").size());

    MO2Row row;
    if (isSep) {
        row.entry = separatorRow(name);
        return row;
    }

    ModEntry e = ModEntry::named(name);
    e.customName = name;
    e.dateAdded  = QDateTime::currentDateTime();
    e.checked    = enabled;
    e.installStatus = int(InstallStatus::NotInstalled);

    if (linkFolder && !modsDir.isEmpty()) {
        // Link to the actual mod folder when it exists in the MO2 mods dir.
        const QDir candidate(modsDir + "/" + name);
        if (candidate.exists()) {
            e.modPath       = candidate.absolutePath();
            e.installStatus = int(InstallStatus::Installed);
            row.folderLinked = true;
        }
    }

    const int modId = modsDir.isEmpty() ? 0
                                        : readMO2MetaId(modsDir + "/" + name + "/meta.ini");
    if (modId > 0) {
        e.nexusId  = modId;
        e.nexusUrl = nexusModUrl(nexusGame, modId);
        row.nexusLinked = true;
    } else {
        // No mod ID - point at the Nexus search page for this mod name so the
        // user can find and install it from the context menu.
        e.sourceUrl = QString("https://www.nexusmods.com/%1/search/?q=%2")
            .arg(nexusGame, QString::fromUtf8(QUrl::toPercentEncoding(name)));
    }
    row.entry = e;
    return row;
}

MO2Instance resolveMO2Instance(const QString &instanceDir)
{
    MO2Instance inst;
    const QDir iDir(instanceDir);
    const bool hasIni      = QFile::exists(instanceDir + "/ModOrganizer.ini");
    const bool hasMods     = iDir.exists("mods");
    const bool hasProfiles = iDir.exists("profiles");
    if (!hasIni && !(hasMods && hasProfiles)) return inst;

    inst.valid       = true;
    inst.modsDir     = instanceDir + "/mods";
    inst.profilesDir = instanceDir + "/profiles";
    if (!hasIni) return inst;

    QSettings ini(instanceDir + "/ModOrganizer.ini", QSettings::IniFormat);
    // MO2 stores paths as @ByteArray(...); strip the wrapper, then normalise
    // Windows backslashes. If the resolved path doesn't exist (e.g. a Wine
    // drive-letter path), fall back to the instance-relative default.
    auto readPath = [&](const QString &key, const QString &fallback) -> QString {
        QString v = ini.value(key).toString();
        if (v.startsWith(QLatin1String("@ByteArray(")) && v.endsWith(')'))
            v = v.mid(11, v.size() - 12);
        v.replace('\\', '/');
        v = v.trimmed();
        if (v.isEmpty())             return fallback;
        // A Wine drive letter: Z: is the host's root, so the folder may well
        // be right here under its Unix name; any other letter is not.
        static const QRegularExpression kDrive(QStringLiteral("^([A-Za-z]):/"));
        if (const auto drive = kDrive.match(v); drive.hasMatch()) {
            if (drive.captured(1).toUpper() == QLatin1String("Z")) {
                const QString hostPath = v.mid(2);
                if (QDir(hostPath).exists()) return hostPath;
            }
            return fallback;   // a Wine path not reachable on this host
        }
        if (QDir::isRelativePath(v)) return instanceDir + '/' + v;
        if (QDir(v).exists())        return v;
        return fallback;
    };
    inst.modsDir     = readPath("Settings/mods_directory",     inst.modsDir);
    inst.profilesDir = readPath("Settings/profiles_directory", inst.profilesDir);
    return inst;
}

QString wabbajackGameSlug(const QString &gameType)
{
    // Wabbajack game names -> Nexus game slugs (lowercase; most are direct
    // lower-case but a few need explicit remapping).
    static const QHash<QString, QString> kGameSlug {
        { "SkyrimSpecialEdition",  "skyrimspecialedition"   },
        { "SkyrimVR",              "skyrimspecialedition"   },
        { "Skyrim",                "skyrim"                 },
        { "Fallout4",              "fallout4"               },
        { "Fallout4VR",            "fallout4"               },
        { "FalloutNV",             "falloutnewvegas"        },
        { "Fallout3",              "fallout3"               },
        { "Morrowind",             "morrowind"              },
        { "Oblivion",              "oblivion"               },
        { "Enderal",               "enderal"                },
        { "EnderalSpecialEdition", "enderalspecialedition"  },
        { "Starfield",             "starfield"              },
        { "DarkestDungeon",        "darkestdungeon"         },
        { "DarkestDungeon2",       "darkestdungeon2"        },
    };
    return kGameSlug.value(gameType, gameType.toLower());
}

QList<WJMod> wabbajackMods(const QJsonObject &root)
{
    const QString defaultSlug = wabbajackGameSlug(root["GameType"].toString());

    QList<WJMod> mods;
    mods.reserve(512);
    for (const QJsonValue &val : root["Archives"].toArray()) {
        const QJsonObject arch  = val.toObject();
        const QJsonObject state = arch["State"].toObject();
        const QString stateType = state["$type"].toString();

        // Skip entries that are vanilla game files, not downloadable mods.
        if (stateType.contains(QLatin1String("GameFileSource"), Qt::CaseInsensitive))
            continue;

        // Skip tools that are irrelevant when installing via Nerevarine:
        // MO2, MWSE, MGE XE, xEdit, LOOT, etc. are launcher/tool entries
        // that Wabbajack bundles for its own install flow.
        {
            const QString aname = arch["Name"].toString().trimmed().toLower();
            const QString sname = state["Name"].toString().trimmed().toLower();
            static const QStringList kToolPrefixes = {
                "mod organizer", "modorganizer", "mod.organizer",
                "mwse", "mge xe", "mge-xe", "mgexe",
                "xedit", "tes3edit", "openmw-csh", "mlox",
                "loot", "wrye mash", "wryemash",
            };
            bool isTool = false;
            for (const QString &p : kToolPrefixes) {
                if (aname.startsWith(p) || sname.startsWith(p)) {
                    isTool = true; break;
                }
            }
            if (isTool) continue;
        }

        // Display name: State.Name is the human-readable title (Nexus page
        // name). Fall back to the archive filename with extension stripped.
        QString name = state["Name"].toString().trimmed();
        if (name.isEmpty()) {
            name = arch["Name"].toString().trimmed();
            for (const QLatin1String ext :
                     { QLatin1String(".7z"), QLatin1String(".zip"),
                       QLatin1String(".rar"), QLatin1String(".fomod") }) {
                if (name.endsWith(ext, Qt::CaseInsensitive)) {
                    name.chop(ext.size());
                    break;
                }
            }
        }
        if (name.isEmpty()) continue;

        // MO2-style separator: some WJ lists embed separator mods.
        const bool isSep = name.endsWith(QLatin1String("_separator"));
        if (isSep) name.chop(QStringLiteral("_separator").size());

        // Nexus identity (both 2.x and 3.x type strings contain "Nexus").
        QString nexusUrl;
        QString sourceUrl;
        int nexusId = 0;
        if (stateType.contains(QLatin1String("Nexus"), Qt::CaseInsensitive)) {
            nexusId = state["ModID"].toInt();
            if (nexusId > 0) {
                // State.GameName may differ from the list's top-level GameType
                // (e.g. a Skyrim SE list that links a mod for plain Skyrim).
                const QString gameName = state["GameName"].toString();
                const QString modSlug = gameName.isEmpty() ? defaultSlug
                                                           : wabbajackGameSlug(gameName);
                nexusUrl = nexusModUrl(modSlug, nexusId);
            }
        } else {
            // Non-Nexus sources: extract a human-visitable download URL so
            // the user knows where to get the file manually.
            // GitHub release: construct from User + Repository + Tag.
            const QString user = state["User"].toString().trimmed();
            const QString repo = state["Repository"].toString().trimmed();
            const QString tag  = state["Tag"].toString().trimmed();
            if (!user.isEmpty() && !repo.isEmpty()) {
                sourceUrl = tag.isEmpty()
                    ? QString("https://github.com/%1/%2").arg(user, repo)
                    : QString("https://github.com/%1/%2/releases/tag/%3")
                          .arg(user, repo, tag);
            }
            // Direct HTTP / Manual: use the Url field if present.
            if (sourceUrl.isEmpty()) {
                const QString url = state["Url"].toString().trimmed();
                if (!url.isEmpty() && url.startsWith("http"))
                    sourceUrl = url;
            }
        }

        mods.append({ name, nexusUrl, sourceUrl, nexusId,
                      arch["Size"].toVariant().toLongLong(), isSep });
    }

    // Deduplicate by Nexus mod ID: a single mod page may supply several
    // archives (main file + update, multiple texture resolutions, etc.).
    // Keep the entry with the largest file size - that's the primary
    // download.  Mods without a Nexus ID are kept as-is (no key to dedup on).
    QHash<int, int> idxById; // nexusId -> index into deduped
    QList<WJMod> deduped;
    deduped.reserve(mods.size());
    for (const WJMod &m : std::as_const(mods)) {
        if (m.nexusId <= 0 || m.isSep) {
            deduped.append(m);
            continue;
        }
        auto it = idxById.find(m.nexusId);
        if (it == idxById.end()) {
            idxById.insert(m.nexusId, int(deduped.size()));
            deduped.append(m);
        } else {
            WJMod &kept = deduped[it.value()];
            // Prefer the shorter name: the base mod title is shorter than
            // "ModName - Update" / "ModName - 2K Textures" variants.
            if (!m.name.isEmpty() && m.name.length() < kept.name.length())
                kept.name = m.name;
            // Keep the larger size.
            if (m.sizeBytes > kept.sizeBytes)
                kept.sizeBytes = m.sizeBytes;
        }
    }
    return deduped;
}

ModEntry wabbajackRow(const WJMod &mod)
{
    if (mod.isSep) return separatorRow(mod.name);
    ModEntry e = ModEntry::named(mod.name);
    e.customName = mod.name;
    e.nexusUrl   = mod.nexusUrl;
    e.nexusId    = mod.nexusId;
    e.sourceUrl  = mod.sourceUrl;
    e.installStatus = int(InstallStatus::NotInstalled);
    e.dateAdded  = QDateTime::currentDateTime();
    // Untagged (no expectedFileId: the Wabbajack record names no Nexus file),
    // so it never judges a download; the nxm-time lookup brings the size for
    // the file actually fetched.
    if (mod.sizeBytes > 0) e.expectedSize = mod.sizeBytes;
    e.checked = true;
    return e;
}

ModlistFormat sniffModlistFormat(const QString &path)
{
    // Peek at the first non-comment line to identify the format. Any .txt
    // name is accepted, since users routinely rename their exports.
    //
    // MO2 lines:        +ModName           - no tab
    // Nerevarine lines: +/path/to\tname\t... - tab-separated
    //                   (or "+\tname\t..." for not-installed entries)
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return ModlistFormat::Unknown;
    QTextStream in(&f);
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith('#')) continue;
        if (line.size() < 2) continue;
        const QChar c = line[0];
        if (c != '+' && c != '-' && c != '*') continue;
        return line.mid(1).contains('\t') ? ModlistFormat::Nerevarine : ModlistFormat::MO2;
    }
    return ModlistFormat::Unknown;
}

QString detectModsBaseFromFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    QTextStream in(&f);
    QString detectedBase;
    bool allMissing = true;
    while (!in.atEnd()) {
        const QString line = in.readLine();
        if (line.size() < 2) continue;
        if (line[0] != '+' && line[0] != '-') continue;
        const QStringList parts = line.mid(2).split('\t');
        if (parts.isEmpty()) continue;
        const QString modPath = parts[0];
        if (!modPath.startsWith('/')) continue; // not an absolute path

        if (QDir(modPath).exists()) {
            allMissing = false;
            continue; // this path works locally, don't count it as foreign
        }

        const QString parent = QFileInfo(modPath).absolutePath();
        if (detectedBase.isEmpty()) {
            detectedBase = parent;
        } else if (detectedBase != parent) {
            // Paths have different parents - fall back to longest common prefix
            int i = 0;
            while (i < detectedBase.size() && i < parent.size()
                   && detectedBase[i] == parent[i])
                ++i;
            detectedBase = detectedBase.left(i);
            if (detectedBase.isEmpty()) return {};
        }
    }
    // If every path exists locally, no remap is needed.
    if (!allMissing && detectedBase.isEmpty()) return {};
    return detectedBase;
}

} // namespace import_formats
