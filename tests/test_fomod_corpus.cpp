// test_fomod_corpus - replay real FOMOD installers through the wizard and diff
// what it decides against a recorded baseline.
//
// Every FOMOD bug this year was reproduced from screenshots, because by then
// the installer's ModuleConfig.xml had been cleaned up with its staging
// folder. This keeps a corpus of them and pins, per installer and per modlist,
// everything the wizard decides before the user touches anything: which
// options are ticked and enabled, what each says, its tooltip, and the notes
// under it, in layout order.
//
//   NRV_FOMOD_CORPUS=<dir>        a folder of ModuleConfig .xml files; unset,
//                                 the test skips (exit 77). Kept outside the
//                                 repo: these are other people's installers.
//   NRV_FOMOD_CORPUS_RECORD=1     write <dir>/.baseline/ instead of comparing.
//
// Not part of the unit label: CI has no corpus.

#include "fomodwizard.h"
#include "fomod_choices.h"
#include "nxmurl.h"

#include <QAbstractButton>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QGroupBox>
#include <QLabel>
#include <QLayout>
#include <QTemporaryDir>
#include <QTextStream>

#include <iostream>

// Friend hook into the wizard's private parse/build and button tree, as in
// test_fomod.cpp - a separate executable, so its own definition.
struct FomodWizardTestHook {
    static FomodWizard *make(const QString &root) { return new FomodWizard(root); }
    static bool parse(FomodWizard *w) { return w->parse(); }
    static void build(FomodWizard *w) { w->buildUi(); }
    static void context(FomodWizard *w, const QStringList &installed,
                        const QStringList &urls, const QString &gameId,
                        const game_runtime::Probe &runtime, const QString &prior)
    {
        w->m_installedModNames = installed;
        for (const QString &u : urls)
            if (const auto ref = parseNexusModUrl(u))
                w->m_installedNexusKeys.insert(ref->game.toLower() + u'/'
                                               + QString::number(ref->modId));
        w->m_gameId       = gameId;
        w->m_runtime      = runtime;
        w->m_priorChoices = prior;
    }
    static const QList<FomodStep> &steps(FomodWizard *w) { return w->m_steps; }
    static const QList<QList<QList<QAbstractButton *>>> &buttons(FomodWizard *w)
    { return w->m_buttons; }
};

namespace {

struct Context {
    QString             name;
    QStringList         installed;
    QStringList         urls;
    QString             gameId;
    game_runtime::Probe runtime;
    bool                priorFirsts = false;   // a stored pick: each group's first option
};

QList<Context> contexts()
{
    Context bare;
    bare.name   = QStringLiteral("bare");
    bare.gameId = QStringLiteral("morrowind");

    Context mw;
    mw.name   = QStringLiteral("morrowind");
    mw.gameId = QStringLiteral("morrowind");
    mw.installed = {"Tamriel Data (HD)", "Tamriel Rebuilt", "OAAB_Data",
                    "Patch for Purists", "Project Atlas", "OpenMW Quest Menu",
                    "Graphic Herbalism MWSE - OpenMW", "Skyrim Home of the Nords",
                    "Glass Glowset", "Morrowind Optimization Patch", "Better Bodies",
                    "Ashfall", "Remiros' Groundcover"};
    mw.urls = {"https://www.nexusmods.com/morrowind/mods/44537",
               "https://www.nexusmods.com/morrowind/mods/42145",
               "https://www.nexusmods.com/morrowind/mods/45096"};

    Context sk;
    sk.name   = QStringLiteral("skyrim-ae");
    sk.gameId = QStringLiteral("skyrimspecialedition");
    sk.runtime.game.major = 1; sk.runtime.game.minor = 6; sk.runtime.game.build = 1170;
    sk.runtime.game.valid = true;
    sk.runtime.extenderPresent = true;
    sk.installed = {"SKSE64", "Address Library for SKSE Plugins", "Base Object Swapper",
                    "SkyPatcher", "Container Distribution Framework",
                    "Static Mesh Improvement Mod - SMIM",
                    "Unofficial Skyrim Special Edition Patch", "PapyrusUtil SE",
                    "powerofthree's Papyrus Extender", "JContainers SE", "Campfire",
                    "SkyUI", "Spell Perk Item Distributor", "Keyword Item Distributor"};
    sk.urls = {"https://www.nexusmods.com/skyrimspecialedition/mods/32444",
               "https://www.nexusmods.com/skyrimspecialedition/mods/60805",
               "https://www.nexusmods.com/skyrimspecialedition/mods/12604",
               "https://www.nexusmods.com/skyrimspecialedition/mods/266"};

    Context fo;
    fo.name   = QStringLiteral("fallout4-noext");
    fo.gameId = QStringLiteral("fallout4");
    fo.runtime.game.major = 1; fo.runtime.game.minor = 10; fo.runtime.game.build = 163;
    fo.runtime.game.valid = true;
    fo.installed = {"Previs Repair Pack (PRP)", "Unofficial Fallout 4 Patch"};

    Context prior = mw;
    prior.name        = QStringLiteral("prior-firsts");
    prior.priorFirsts = true;

    return {bare, mw, sk, fo, prior};
}

QString esc(QString s)
{
    s.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    s.replace(QLatin1Char('\n'), QStringLiteral("\\n"));
    return s;
}

// Everything the wizard decided, in layout order: each group box's buttons -
// the synthetic "None" radio included - and the notes between them.
QString dump(FomodWizard *w)
{
    QString out;
    QTextStream ts(&out);
    const auto &steps   = FomodWizardTestHook::steps(w);
    const auto &buttons = FomodWizardTestHook::buttons(w);
    for (int si = 0; si < steps.size() && si < buttons.size(); ++si) {
        ts << "step " << si << ' ' << esc(steps[si].name) << '\n';
        for (int gi = 0; gi < steps[si].groups.size() && gi < buttons[si].size(); ++gi) {
            const FomodGroup &g = steps[si].groups[gi];
            ts << " group " << si << '.' << gi << " [" << g.type << "] " << esc(g.name) << '\n';
            if (buttons[si][gi].isEmpty()) continue;
            QWidget *box = buttons[si][gi].first()->parentWidget();
            QLayout *lay = box ? box->layout() : nullptr;
            if (!lay) continue;
            for (int i = 0; i < lay->count(); ++i) {
                QWidget *wid = lay->itemAt(i)->widget();
                if (auto *b = qobject_cast<QAbstractButton *>(wid)) {
                    ts << "  " << (b->isChecked() ? "[x] " : "[ ] ")
                       << (b->isEnabled() ? "" : "(disabled) ")
                       << esc(b->text());
                    if (!b->toolTip().isEmpty()) ts << "  tip: " << esc(b->toolTip());
                    ts << '\n';
                } else if (auto *l = qobject_cast<QLabel *>(wid)) {
                    ts << "    note: " << esc(l->text()) << '\n';
                }
            }
        }
    }
    return out;
}

// The stored-pick context: the first option of every group, as a previous
// install would have recorded it.
QString firstsPrior(const QList<FomodStep> &steps)
{
    QSet<quint64> keys;
    for (int si = 0; si < steps.size(); ++si)
        for (int gi = 0; gi < steps[si].groups.size(); ++gi)
            if (!steps[si].groups[gi].plugins.isEmpty())
                keys.insert(fomod_choices::key(si, gi, 0));
    return fomod_choices::encode(steps, keys);
}

} // namespace

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", QByteArray("offscreen"));
    QApplication app(argc, argv);

    const QString corpus = qEnvironmentVariable("NRV_FOMOD_CORPUS");
    if (corpus.isEmpty()) {
        std::cout << "NRV_FOMOD_CORPUS not set - skipped\n";
        return 77;
    }
    const bool record = qEnvironmentVariableIntValue("NRV_FOMOD_CORPUS_RECORD") == 1;
    const QString baseDir = QDir(corpus).filePath(QStringLiteral(".baseline"));
    if (record) QDir().mkpath(baseDir);

    const QStringList xmls = QDir(corpus).entryList({"*.xml"}, QDir::Files, QDir::Name);
    int checked = 0, recorded = 0, same = 0, differ = 0, missing = 0, unparsed = 0;

    for (const QString &xml : xmls) {
        QTemporaryDir root;
        QDir().mkpath(root.filePath("fomod"));
        QFile::copy(QDir(corpus).filePath(xml), root.filePath("fomod/ModuleConfig.xml"));

        for (const Context &ctx : contexts()) {
            FomodWizard *w = FomodWizardTestHook::make(root.path());
            if (!FomodWizardTestHook::parse(w)) {
                delete w;
                if (ctx.name == QLatin1String("bare")) ++unparsed;
                continue;
            }
            const QString prior = ctx.priorFirsts
                ? firstsPrior(FomodWizardTestHook::steps(w)) : QString();
            FomodWizardTestHook::context(w, ctx.installed, ctx.urls, ctx.gameId,
                                         ctx.runtime, prior);
            FomodWizardTestHook::build(w);
            const QString got = dump(w);
            delete w;
            ++checked;

            const QString file = QDir(baseDir).filePath(xml + QLatin1Char('.') + ctx.name);
            if (record) {
                QFile f(file);
                if (f.open(QIODevice::WriteOnly)) f.write(got.toUtf8());
                ++recorded;
                continue;
            }
            QFile f(file);
            if (!f.open(QIODevice::ReadOnly)) { ++missing; continue; }
            const QString want = QString::fromUtf8(f.readAll());
            if (want == got) { ++same; continue; }
            ++differ;
            // The first differing line, enough to start from.
            const QStringList a = want.split('\n'), b = got.split('\n');
            int i = 0;
            while (i < a.size() && i < b.size() && a[i] == b[i]) ++i;
            std::cout << "DIFF " << xml.toStdString() << " [" << ctx.name.toStdString() << "]\n"
                      << "  want: " << a.value(i).toStdString() << "\n"
                      << "  got:  " << b.value(i).toStdString() << "\n";
        }
    }

    std::cout << xmls.size() << " installers (" << unparsed << " unparsable), "
              << checked << " runs: ";
    if (record) {
        std::cout << recorded << " baselines recorded\n";
        return 0;
    }
    std::cout << same << " same, " << differ << " differ, " << missing << " without a baseline\n";
    return (differ == 0 && missing == 0) ? 0 : 1;
}
