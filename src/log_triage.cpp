#include "log_triage.h"

#include <QHash>
#include <QRegularExpression>
#include <QSet>

namespace openmw {

namespace {

// lowercase plugin -> displayName. A mod can own several plugins; same-name
// collisions keep the last one (filenames are unique in practice).
static QHash<QString, QString> indexPlugins(const QList<TriageMod> &mods)
{
    QHash<QString, QString> byPlugin;
    for (const TriageMod &m : mods)
        for (const QString &p : m.plugins)
            byPlugin.insert(p.toLower(), m.displayName);
    return byPlugin;
}

// Strip quotes/brackets/trailing punctuation off a captured filename.
static QString cleanToken(QString s)
{
    while (!s.isEmpty() && (s.startsWith('"') || s.startsWith('<')
                         || s.startsWith('\'')))
        s.remove(0, 1);
    while (!s.isEmpty() && (s.endsWith('"')  || s.endsWith('>')
                         || s.endsWith('\'') || s.endsWith(',')
                         || s.endsWith('.')  || s.endsWith(':')))
        s.chop(1);
    return s.trimmed();
}

} // namespace

LogTriageReport triageOpenMWLog(const QString          &logText,
                                const QList<TriageMod> &mods)
{
    LogTriageReport report;
    if (logText.isEmpty()) return report;

    const QHash<QString, QString> pluginOwner = indexPlugins(mods);

    // 1. Missing master: "File X asks for parent file Y, but it is not
    //    available or has been loaded in the wrong order" (older OpenMW says
    //    "...has been loaded after"). rx(...)rx delimiters so groups can hold )".
    static const QRegularExpression rxMissingMaster(
        R"rx(File\s+"?([^"\r\n]+?\.(?:esp|esm|omwaddon|omwscripts))"?\s+asks for parent file\s+"?([^"\r\n,]+?\.(?:esp|esm))"?)rx",
        QRegularExpression::CaseInsensitiveOption);

    // 2. Missing plugin from a content= line: "Fatal error: Failed loading
    //    X.esp: the content file does not exist". Loose on the prose so a
    //    rephrase still matches.
    static const QRegularExpression rxMissingPlugin(
        R"rx(Failed loading\s+"?([^"\r\n]+?\.(?:esp|esm|omwaddon|omwscripts))"?\s*:\s*[^\r\n]*(?:does not exist|cannot be found|not found))rx",
        QRegularExpression::CaseInsensitiveOption);

    // 3a. Missing texture. Matches Can't/Cannot/Can not; phrasing varies by
    //     graphics backend.
    static const QRegularExpression rxMissingTexture(
        R"rx((?:Can(?:'t|\s*not) find texture|texture not found|Texture .* (?:missing|not found|could not be loaded))[^"\r\n]*?"?([A-Za-z0-9_\-/\\.]+\.(?:dds|tga|bmp|png|jpg))"?)rx",
        QRegularExpression::CaseInsensitiveOption);

    // 3b. Missing mesh / NIF.
    static const QRegularExpression rxMissingMesh(
        R"rx((?:Error loading|Failed to load|Could not (?:load|find))\s+"?([A-Za-z0-9_\-/\\. ]+\.(?:nif|kf|x))"?)rx",
        QRegularExpression::CaseInsensitiveOption);

    // 3c. Generic asset not found. Lowest priority; only fires if the
    //     specific regexes above missed.
    static const QRegularExpression rxMissingAsset(
        R"rx((?:Could not (?:find|open)|Cannot find|Unable to open|File not found):?\s+"?([A-Za-z0-9_\-/\\. ]+\.(?:dds|tga|bmp|png|jpg|nif|kf|x|wav|mp3|ogg|bsa|ini))"?)rx",
        QRegularExpression::CaseInsensitiveOption);

    // Count every error-shaped line (matched or not) for the "saw N, matched M"
    // stat.
    static const QRegularExpression rxErrorLine(
        R"rx((?:^|\s)(?:\[[^\]]*\bE\]|Fatal error:|Error:|Warning:))rx",
        QRegularExpression::CaseInsensitiveOption);

    // Dedup keyed by (kind, target): the same "asks for parent file X" repeated
    // 80 times collapses to one issue.
    QSet<QString> seen;
    auto already = [&](LogIssueKind k, const QString &target) {
        const QString key = QString::number(int(k)) + "|" + target.toLower();
        if (seen.contains(key)) return true;
        seen.insert(key);
        return false;
    };

    // Catch-all for Fatal/E lines nothing else claimed. Cap at 10 distinct
    // messages so a 50k-line log doesn't drown the dialog.
    const int kMaxOtherErrors = 10;
    int otherErrorsEmitted = 0;

    const QStringList lines = logText.split('\n');
    for (const QString &raw : lines) {
        QString line = raw;
        if (line.endsWith('\r')) line.chop(1);
        if (line.isEmpty()) continue;

        // Count error lines even if a matcher later claims them (total seen).
        bool isError = false;
        {
            auto m = rxErrorLine.match(line);
            if (m.hasMatch()) {
                isError = true;
                ++report.errorLines;
            }
        }

        // Missing master first: the two-file pattern must win over the plain
        // missing-plugin match below if a line has both shapes.
        {
            auto m = rxMissingMaster.match(line);
            if (m.hasMatch()) {
                const QString child  = cleanToken(m.captured(1));
                const QString parent = cleanToken(m.captured(2));
                if (!child.isEmpty() && !already(LogIssueKind::MissingMaster, child)) {
                    LogIssue issue;
                    issue.kind   = LogIssueKind::MissingMaster;
                    issue.target = child;
                    issue.parent = parent;
                    issue.detail = line;
                    issue.suspectMod = pluginOwner.value(child.toLower());
                    report.issues << issue;
                }
                continue;
            }
        }

        // 2. Missing plugin.
        {
            auto m = rxMissingPlugin.match(line);
            if (m.hasMatch()) {
                const QString plugin = cleanToken(m.captured(1));
                if (!plugin.isEmpty() && !already(LogIssueKind::MissingPlugin, plugin)) {
                    LogIssue issue;
                    issue.kind   = LogIssueKind::MissingPlugin;
                    issue.target = plugin;
                    issue.detail = line;
                    issue.suspectMod = pluginOwner.value(plugin.toLower());
                    report.issues << issue;
                }
                continue;
            }
        }

        // 3a. Missing texture.
        {
            auto m = rxMissingTexture.match(line);
            if (m.hasMatch()) {
                const QString tex = cleanToken(m.captured(1));
                if (!tex.isEmpty() && !already(LogIssueKind::MissingAsset, tex)) {
                    LogIssue issue;
                    issue.kind   = LogIssueKind::MissingAsset;
                    issue.target = tex;
                    issue.detail = line;
                    report.issues << issue;
                }
                continue;
            }
        }

        // 3b. Missing mesh / NIF.
        {
            auto m = rxMissingMesh.match(line);
            if (m.hasMatch()) {
                const QString mesh = cleanToken(m.captured(1));
                if (!mesh.isEmpty() && !already(LogIssueKind::MissingAsset, mesh)) {
                    LogIssue issue;
                    issue.kind   = LogIssueKind::MissingAsset;
                    issue.target = mesh;
                    issue.detail = line;
                    report.issues << issue;
                }
                continue;
            }
        }

        // 3c. Generic asset (last chance before the catch-all).
        {
            auto m = rxMissingAsset.match(line);
            if (m.hasMatch()) {
                const QString asset = cleanToken(m.captured(1));
                if (!asset.isEmpty() && !already(LogIssueKind::MissingAsset, asset)) {
                    LogIssue issue;
                    issue.kind   = LogIssueKind::MissingAsset;
                    issue.target = asset;
                    issue.detail = line;
                    report.issues << issue;
                }
                continue;
            }
        }

        // Unclassified error. Budget-capped (see above) so a pathological log
        // doesn't emit thousands of "unknown" rows.
        if (isError && otherErrorsEmitted < kMaxOtherErrors) {
            const QString excerpt = line.trimmed();
            if (!already(LogIssueKind::OtherError, excerpt)) {
                LogIssue issue;
                issue.kind   = LogIssueKind::OtherError;
                issue.target = excerpt;
                issue.detail = line;
                report.issues << issue;
                ++otherErrorsEmitted;
            }
        }
    }

    return report;
}

} // namespace openmw
