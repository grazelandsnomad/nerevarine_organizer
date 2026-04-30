#include "log_triage.h"

#include <QHash>
#include <QRegularExpression>
#include <QSet>

namespace openmw {

namespace {

// Lowercase-plugin -> displayName lookup, O(1) per match. A mod can own
// multiple plugins; collisions between mods on the same filename resolve
// to the last one seen (in practice unique).
static QHash<QString, QString> indexPlugins(const QList<TriageMod> &mods)
{
    QHash<QString, QString> byPlugin;
    for (const TriageMod &m : mods)
        for (const QString &p : m.plugins)
            byPlugin.insert(p.toLower(), m.displayName);
    return byPlugin;
}

// Strip quotes, brackets, and trailing punctuation from a captured filename.
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

    // -- Pattern set ---
    //
    // 1. Missing master: "File X asks for parent file Y, but it is not
    //    available or has been loaded in the wrong order" (or
    //    "...has been loaded after" on older OpenMW). The rx(...)rx
    //    delimiters let groups contain )" without closing the literal.
    static const QRegularExpression rxMissingMaster(
        R"rx(File\s+"?([^"\r\n]+?\.(?:esp|esm|omwaddon|omwscripts))"?\s+asks for parent file\s+"?([^"\r\n,]+?\.(?:esp|esm))"?)rx",
        QRegularExpression::CaseInsensitiveOption);

    // 2. Missing plugin referenced by a content= line: "Fatal error: Failed
    //    loading X.esp: the content file does not exist". Tolerant of
    //    wrapping prose so a future rephrase still catches.
    static const QRegularExpression rxMissingPlugin(
        R"rx(Failed loading\s+"?([^"\r\n]+?\.(?:esp|esm|omwaddon|omwscripts))"?\s*:\s*[^\r\n]*(?:does not exist|cannot be found|not found))rx",
        QRegularExpression::CaseInsensitiveOption);

    // 3a. Missing texture; matches "Can't", "Cannot", and "Can not" since
    //     OpenMW's phrasing varies across graphics backends.
    static const QRegularExpression rxMissingTexture(
        R"rx((?:Can(?:'t|\s*not) find texture|texture not found|Texture .* (?:missing|not found|could not be loaded))[^"\r\n]*?"?([A-Za-z0-9_\-/\\.]+\.(?:dds|tga|bmp|png|jpg))"?)rx",
        QRegularExpression::CaseInsensitiveOption);

    // 3b. Missing mesh / NIF.
    static const QRegularExpression rxMissingMesh(
        R"rx((?:Error loading|Failed to load|Could not (?:load|find))\s+"?([A-Za-z0-9_\-/\\. ]+\.(?:nif|kf|x))"?)rx",
        QRegularExpression::CaseInsensitiveOption);

    // 3c. Generic asset not found. Lowest-priority match: only fires when
    //     the more specific regexes missed.
    static const QRegularExpression rxMissingAsset(
        R"rx((?:Could not (?:find|open)|Cannot find|Unable to open|File not found):?\s+"?([A-Za-z0-9_\-/\\. ]+\.(?:dds|tga|bmp|png|jpg|nif|kf|x|wav|mp3|ogg|bsa|ini))"?)rx",
        QRegularExpression::CaseInsensitiveOption);

    // Count every error-shaped line, classified or not, so the UI can show
    // "triage saw N errors, matched M".
    static const QRegularExpression rxErrorLine(
        R"rx((?:^|\s)(?:\[[^\]]*\bE\]|Fatal error:|Error:|Warning:))rx",
        QRegularExpression::CaseInsensitiveOption);

    // -- Dedup bookkeeping ---
    //
    // Keyed by (kind, target.toLower()) so a log that repeats the same
    // "asks for parent file X" 80 times still produces a single issue.
    QSet<QString> seen;
    auto already = [&](LogIssueKind k, const QString &target) {
        const QString key = QString::number(int(k)) + "|" + target.toLower();
        if (seen.contains(key)) return true;
        seen.insert(key);
        return false;
    };

    // Generic catch-all for Fatal/E lines no specific matcher claimed.
    // Capped at 10 distinct messages so a 50k-line log doesn't drown the
    // dialog with unclassified errors.
    const int kMaxOtherErrors = 10;
    int otherErrorsEmitted = 0;

    const QStringList lines = logText.split('\n');
    for (const QString &raw : lines) {
        QString line = raw;
        if (line.endsWith('\r')) line.chop(1);
        if (line.isEmpty()) continue;

        // Count every error line, even ones a specific matcher claims, so
        // the summary stat is "total errors seen".
        bool isError = false;
        {
            auto m = rxErrorLine.match(line);
            if (m.hasMatch()) {
                isError = true;
                ++report.errorLines;
            }
        }

        // 1. Missing master - check FIRST so the more specific (two-file)
        //    pattern wins over a plain missing-plugin match further down
        //    if the same line ever contained both shapes.
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

        // 4. Unclassified error line.  Budget-capped so a pathological log
        //    doesn't blow out the dialog with thousands of "unknown" rows.
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
