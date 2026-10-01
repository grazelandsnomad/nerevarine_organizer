#include "fomod_plan.h"

#include "fomod_choices.h"
#include "fomod_hint.h"
#include "mod_aliases.h"
#include "mod_match.h"
#include "nxmurl.h"

#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QString>
#include <QStringList>

#include <memory>
#include <utility>
#include <vector>

namespace fomod {
namespace {

// One option of the plan, answering the calls the passes used to make on its
// button - so the passes moved over unchanged. setChecked keeps a radio
// group's rule: checking one unchecks the rest, the "None" radio included, and
// a radio is never unchecked directly (Qt refuses that for an exclusive group;
// another one gets picked instead).
class Option {
public:
    Option(GroupPlan *group, int index) : m_group(group), m_index(index) {}

    bool    isEnabled() const { return plan().enabled; }
    bool    isChecked() const { return plan().checked; }
    QString text() const      { return plan().text; }
    QString toolTip() const   { return plan().toolTip; }
    void setText(const QString &t)    { plan().text = t; }
    void setToolTip(const QString &t) { plan().toolTip = t; }
    void addNote(const QString &n)    { plan().notes << n; }

    void setChecked(bool on)
    {
        if (!m_group->exclusive) {
            plan().checked = on;
            return;
        }
        if (!on) return;
        for (OptionPlan &o : m_group->options) o.checked = false;
        m_group->noneChecked = false;
        plan().checked = true;
    }

private:
    OptionPlan &plan() const { return m_group->options[m_index]; }

    GroupPlan *m_group;
    int        m_index;
};

// What an option's own words say about other mods, worked out once for every
// pass that asks. Three passes used to re-derive the "(No BOS)" reading each
// on its own - one from the trimmed name, two from the raw one - where nothing
// kept them from drifting apart.
struct OptionFacts {
    // The mod a "(No X)" / "without X" option is WITHOUT, or "". Such an
    // option is about a variant pair (Pass H), never an option FOR X.
    QString           negatedMod;
    // The mods a patch option patches (Pass C) - none for a negated name:
    // "Foo - No BOS Patch" is a patch for Foo, for people without BOS.
    QStringList       patchTargets;
    QList<NexusModRef> cited;      // mod pages its description links (Pass E)
    QStringList       required;    // mods its description says it requires (Pass F)
};

} // namespace

Plan planSelections(const QList<FomodStep> &steps, const PlanContext &ctx)
{
    // The installer's own defaults, as the buttons were born with them. A radio
    // group keeps the last default-on option, as Qt does when several are set
    // in turn.
    Plan plan;
    plan.resize(steps.size());
    for (int si = 0; si < steps.size(); ++si) {
        plan[si].resize(steps[si].groups.size());
        for (int gi = 0; gi < steps[si].groups.size(); ++gi) {
            const FomodGroup &group = steps[si].groups[gi];
            GroupPlan &gp = plan[si][gi];
            const bool selectExactlyOne = (group.type == "SelectExactlyOne");
            const bool selectAtMostOne  = (group.type == "SelectAtMostOne");
            gp.exclusive = selectExactlyOne || selectAtMostOne;
            gp.hasNone   = selectAtMostOne;
            const bool selectAll = (group.type == "SelectAll");

            bool firstSelectable = true;
            bool anyChecked = false;
            for (int pi = 0; pi < group.plugins.size(); ++pi) {
                const FomodPlugin &plugin = group.plugins[pi];
                const bool required    = (plugin.type == "Required");
                const bool notUsable   = (plugin.type == "NotUsable");
                const bool recommended = (plugin.type == "Recommended");

                OptionPlan o;
                o.text    = plugin.name;
                o.toolTip = plugin.description;
                o.enabled = !(required || notUsable);
                gp.options.append(o);

                bool defaultOn = required || recommended || selectAll;
                if (!defaultOn && !gp.exclusive && group.plugins.size() == 1)
                    defaultOn = true;   // lone SelectAny plugin = core component, default ON
                // Only SelectExactlyOne forces a pick; SelectAtMostOne may stay
                // on "none".
                if (selectExactlyOne && firstSelectable && !notUsable) {
                    defaultOn = true;   // at least one radio on
                    firstSelectable = false;
                }
                if (defaultOn) anyChecked = true;
                Option(&gp, pi).setChecked(defaultOn);
            }
            gp.noneChecked = gp.hasNone && !anyChecked;
        }
    }

    // The passes ask for options as they asked for buttons: [si][gi][pi], and
    // value(pi) for "that one, if it exists".
    std::vector<std::unique_ptr<Option>> owned;
    QList<QList<QList<Option *>>> buttons;
    buttons.resize(steps.size());
    for (int si = 0; si < steps.size(); ++si) {
        buttons[si].resize(steps[si].groups.size());
        for (int gi = 0; gi < steps[si].groups.size(); ++gi)
            for (int pi = 0; pi < plan[si][gi].options.size(); ++pi) {
                owned.push_back(std::make_unique<Option>(&plan[si][gi], pi));
                buttons[si][gi].append(owned.back().get());
            }
    }

    QList<QList<QList<OptionFacts>>> facts;
    facts.resize(steps.size());
    for (int si = 0; si < steps.size(); ++si) {
        facts[si].resize(steps[si].groups.size());
        for (int gi = 0; gi < steps[si].groups.size(); ++gi) {
            const FomodGroup &group = steps[si].groups[gi];
            for (const FomodPlugin &plugin : group.plugins) {
                OptionFacts f;
                const QString name = plugin.name.trimmed();
                f.negatedMod   = fomod::negatedModIn(name);
                f.patchTargets = f.negatedMod.isEmpty()
                    ? fomod::patchTargetsOf(name, group.name) : QStringList();
                f.cited        = fomod::citedMods(plugin.description);
                f.required     = fomod::requiredMods(plugin.description, plugin.name,
                                                     group.name);
                facts[si][gi].append(f);
            }
        }
    }

    // Smart defaults per exclusive group:
    //   Pass A - OpenMW vs MGE XE: always pick OpenMW; prior choices do NOT
    //            override. Recorded in settledGroups so the prior-choices
    //            block skips them.
    //   Pass B - Yes/No groups that are about ANOTHER mod -> pick Yes (it is in
    //            the modlist) / No (it is not). Names expand to variants
    //            ("OAAB_Data" / "OAAB Data" / "OAAB"). The two halves need
    //            different evidence: a modlist hit proves the step names a mod,
    //            a miss proves nothing, so "No" additionally needs wording that
    //            asks about the user's setup (fomod::asksAboutAnotherMod). A
    //            question about this mod's own options gets no verdict at all.
    //            Annotation always shown; selection only changes with no stored
    //            prior.

    // What was picked last time, found in THIS installer - by name, so an
    // update that moved its options still gets the same picks (see
    // fomod_choices.h). The passes' "has a prior" test and the replay at the
    // end both read this one set.
    const QSet<quint64> priorKeys = fomod_choices::decode(steps, ctx.priorChoices);

    // Exclusive groups whose pick is a fact, not a preference, so a choice
    // stored from an earlier install must not put the old pick back: the
    // engine this manager runs (Pass A), the language it reads (A2), the
    // game's own runtime (A3), and what the modlist holds (G, H). Key:
    // (si<<16)|gi. Named for the first of them, OpenMW, until there were five.
    QSet<quint64> settledGroups;
    // Checkbox plugins Pass C auto-recommends because the named mod is present.
    // Key: (si<<32)|(gi<<16)|pi. Prior-choices block ORs this in so a Recommended
    // checkbox isn't unticked just because an earlier install left it off.
    QSet<quint64> recommendedInstalledPlugins;
    // Ticks settled by what is in the modlist right now (Pass E and Pass F),
    // and by the engine this manager runs (the checkbox shape of Pass A),
    // same key, value = the tick that was forced. Whether a mod is installed
    // is a fact about this profile, not a preference, so a choice stored from
    // an earlier install of this same FOMOD must not put the old tick back:
    // the modlist it was made against is gone.
    QHash<quint64, bool> modlistSettledPlugins;

    {
        // (si,gi) pairs with a stored selection. Pass B annotates but doesn't
        // change these.
        QSet<quint64> priorGroups;
        for (quint64 k : priorKeys) priorGroups.insert(k >> 16);

        // Search needles for the spellings a modlist actually uses. Shared
        // with the BAIN picker, which asks the same question of a folder
        // name - see mod_match.h.
        const auto needlesFor = &mod_match::needlesFor;

        // One wording for both shapes of engine question, radio and checkbox.
        const QString kOpenMwRecommended = QStringLiteral(
            " \u2705 Recommended \u2014 Nerevarine runs Morrowind through OpenMW.");

        // A footnote under an option; the wizard turns each into a grey label
        // directly beneath its button (FomodWizard::applyPlan).
        const auto addNoteUnder = [](Option *btn, const QString &text) {
            if (btn) btn->addNote(text);
        };
        // What a framework IS, one line each, for the five this wizard can
        // recognise. Hardcoded English like every other annotation here.
        // Anything unknown gets the state sentence only - never invented
        // prose.
        const auto blurbFor = [](const QString &fullName) -> QString {
            const QString n = fullName.toLower();
            if (n == QLatin1String("previs repair pack"))
                return QStringLiteral("rebuilds the game's precombined meshes "
                                      "and visibility data (performance and "
                                      "occlusion)");
            if (n == QLatin1String("base object swapper"))
                return QStringLiteral("a framework that swaps placed objects "
                                      "in the world at runtime");
            if (n == QLatin1String("container distribution framework"))
                return QStringLiteral("a framework that distributes items "
                                      "into the game's containers");
            if (n == QLatin1String("skypatcher"))
                return QStringLiteral("a framework that patches game records "
                                      "at load time");
            if (n == QLatin1String("baka framework"))
                return QStringLiteral("a scripting framework extending F4SE");
            return {};
        };

        for (int si = 0; si < steps.size(); ++si) {
            const FomodStep &step = steps[si];
            for (int gi = 0; gi < step.groups.size(); ++gi) {
                const FomodGroup &group = step.groups[gi];
                const quint64 groupKey = (quint64(si) << 16) | quint64(gi);

                const bool exclusive  = (group.type == QLatin1String("SelectExactlyOne") ||
                                          group.type == QLatin1String("SelectAtMostOne"));
                const bool isSelectAll = (group.type == QLatin1String("SelectAll"));
                if (isSelectAll) continue;  // all forced on

                if (exclusive) {
                    // Pass A: OpenMW vs MGE XE - prefer OpenMW.
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
                            Option *btn = buttons[si][gi].value(openMwIdx);
                            if (btn && btn->isEnabled()) {
                                btn->setChecked(true);
                                btn->setText(btn->text() + kOpenMwRecommended);
                            }
                            settledGroups.insert(groupKey);
                            continue;  // skip Pass B for this group
                        }
                    }

                    // Pass A2: language groups - prefer English.
                    {
                        int engIdx = -1, nonEngIdx = -1;
                        static const QStringList kEngTokens  = {
                            "eng", "english", "en"
                        };
                        static const QStringList kRusTokens  = {
                            "rus", "russian", "ru", "russ"
                        };
                        // Match names that are just a language token, or
                        // led/trailed by one ("English", "ENG", "Russian patch").
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
                            Option *btn = buttons[si][gi].value(engIdx);
                            if (btn && btn->isEnabled()) {
                                btn->setChecked(true);
                                btn->setText(btn->text() +
                                    QStringLiteral(" (Recommended - English)"));
                            }
                            settledGroups.insert(groupKey);
                            continue;  // skip Pass B for this group
                        }
                    }

                    // Pass A3: game-runtime pairs - an SKSE-plugin FOMOD
                    // offering its DLL per runtime ("SSE v1.6.629+
                    // ('Anniversary Edition')" vs "SSE v1.5.97 ('Special
                    // Edition')"). Which one is right is not a preference, it
                    // is a fact the manager already holds: the AE/SE split IS
                    // the game profile. Only fires when the group contains
                    // both kinds, so a stray "AE" in an unrelated option name
                    // never draws a tick.
                    {
                        const auto pref = fomod::runtimePreferenceForGame(
                            ctx.gameId, ctx.runtime.game);
                        bool haveCur = false, haveLeg = false;
                        int prefIdx = -1, optOutIdx = -1;
                        for (int pi = 0; pi < group.plugins.size(); ++pi) {
                            const auto v = fomod::classifyRuntimeVariant(
                                group.plugins[pi].name, ctx.gameId);
                            haveCur |= (v == fomod::Runtime::Current);
                            haveLeg |= (v == fomod::Runtime::Legacy);
                            if (v == pref && prefIdx == -1) prefIdx = pi;
                            // "None - install none of these" is the answer when
                            // the extender is absent. isOptOut is anchored and
                            // will not see it, and loosening that would weaken
                            // Pass G, so only the leading token is tested here.
                            if (optOutIdx == -1 && v == fomod::Runtime::None
                                && fomod::isOptOutLabel(
                                       group.plugins[pi].name.section(
                                           QLatin1Char('-'), 0, 0)))
                                optOutIdx = pi;
                        }
                        if (haveCur && haveLeg) {
                            // An extender plugin is worth nothing without the
                            // extender. Ask first, because "which build" is
                            // the wrong question when the answer is "none".
                            const bool haveExt = ctx.runtime.extenderPresent;
                            const QString ver = ctx.runtime.game.valid
                                ? ctx.runtime.game.shortString() : QString();

                            if (!haveExt && optOutIdx != -1) {
                                if (auto *btn = buttons[si][gi].value(optOutIdx);
                                    btn && btn->isEnabled())
                                    btn->setChecked(true);
                                for (int pi = 0; pi < group.plugins.size(); ++pi) {
                                    const auto v = fomod::classifyRuntimeVariant(
                                        group.plugins[pi].name, ctx.gameId);
                                    if (v == fomod::Runtime::None) continue;
                                    if (auto *b = buttons[si][gi].value(pi))
                                        addNoteUnder(b, QStringLiteral(
                                            "No script extender found in your "
                                            "game folder, so a plugin built for "
                                            "it would never load."));
                                }
                                settledGroups.insert(groupKey);
                                continue;
                            }
                            if (prefIdx != -1 && haveExt) {
                                Option *btn =
                                    buttons[si][gi].value(prefIdx);
                                if (btn && btn->isEnabled()) {
                                    btn->setChecked(true);
                                    btn->setText(btn->text() + QStringLiteral(
                                        " ✅ Recommended - matches "
                                        "your game version."));
                                    addNoteUnder(btn, ver.isEmpty()
                                        ? QStringLiteral(
                                            "Matches the script extender "
                                            "installed in your game folder.")
                                        : QStringLiteral(
                                            "Your game reports version %1, and "
                                            "a matching script extender is "
                                            "installed beside it.").arg(ver));
                                }
                                settledGroups.insert(groupKey);
                                continue;  // settled; skip Pass B
                            }
                        }
                    }

                    // Pass B: Yes/No groups - check modlist presence.
                    int yesIdx = -1, noIdx = -1;
                    for (int pi = 0; pi < group.plugins.size(); ++pi) {
                        const QString pname = group.plugins[pi].name.trimmed().toLower();
                        if (yesIdx == -1 && pname == QLatin1String("yes")) yesIdx = pi;
                        if (noIdx  == -1 && pname == QLatin1String("no"))  noIdx  = pi;
                    }
                    if (yesIdx == -1 || noIdx == -1) continue;

                    Option *yesBtn = buttons[si][gi].value(yesIdx);
                    Option *noBtn  = buttons[si][gi].value(noIdx);
                    if (!yesBtn || !noBtn) continue;

                    // Needles from the group name + step/group context, matched
                    // against installed mod names. Short needles (< 8 chars) must
                    // match start-of-name to avoid false hits.
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
                        for (const QString &modName : std::as_const(ctx.installedModNames)) {
                            if (re.match(modName).hasMatch()) { modPresent = true; break; }
                        }
                        if (modPresent) break;
                    }

                    const bool hasPrior = priorGroups.contains(groupKey);

                    if (modPresent) {
                        if (!hasPrior) yesBtn->setChecked(true);
                        yesBtn->setText(yesBtn->text() +
                            QStringLiteral(" \u2705 Recommended. The mod is currently present in the modlist."));
                    } else if (fomod::asksAboutAnotherMod(step.name, group.name)) {
                        if (!hasPrior) noBtn->setChecked(true);
                        noBtn->setText(noBtn->text() +
                            QStringLiteral(" \u2705 Recommended. This mod is not currently present in the modlist."));
                    }
                    // Nothing matched and the question never mentions the user's
                    // setup: it is one of this mod's own options, so leave the
                    // FOMOD's default alone and say nothing.

                } else {
                    // Pass A, checkbox shape. OAAB_Data asks its engine
                    // question as a SelectAtLeastOne pair of checkboxes -
                    // "OpenMW" and "MGE XE", both off - which the radio rule
                    // above never saw. Ticking both installs both: the OpenMW
                    // scripts and MWSE's Lua, which OpenMW never runs.
                    //
                    // Same answer as the radio case, settled per OPTION
                    // (modlistSettledPlugins) rather than per group, so a pick
                    // stored for another engine cannot come back while the
                    // user's other picks in a mixed group still do. Only names
                    // that are NOTHING but an engine take part
                    // (fomod::engineOption): a patch group offering "OpenMW
                    // Lua Helper Patch" beside "MWSE Magic Patch" is not an
                    // engine question.
                    QSet<int> engineOpts;   // Pass C must not look these up
                    {
                        QList<int> openMwIdx, originalIdx;
                        for (int pi = 0; pi < group.plugins.size(); ++pi) {
                            const auto e = fomod::engineOption(group.plugins[pi].name);
                            if (e == fomod::EngineOption::None) continue;
                            engineOpts.insert(pi);
                            if (e == fomod::EngineOption::OpenMW)        openMwIdx << pi;
                            else if (e == fomod::EngineOption::Original) originalIdx << pi;
                        }
                        for (int pi : std::as_const(openMwIdx)) {
                            Option *b = buttons[si][gi].value(pi);
                            if (!b || !b->isEnabled()) continue;
                            b->setChecked(true);
                            b->setText(b->text() + kOpenMwRecommended);
                            modlistSettledPlugins.insert(fomod_choices::key(si, gi, pi), true);
                        }
                        // SelectAtLeastOne has to keep one ticked: an option
                        // is only unticked while something else stays on.
                        const bool keepOne = group.type == QLatin1String("SelectAtLeastOne");
                        for (int pi : std::as_const(originalIdx)) {
                            Option *b = buttons[si][gi].value(pi);
                            if (!b || !b->isEnabled()) continue;
                            bool otherOn = false;
                            for (int o = 0; o < buttons[si][gi].size(); ++o)
                                if (o != pi && buttons[si][gi][o]->isChecked()
                                    && !originalIdx.contains(o))
                                    otherOn = true;
                            if (keepOne && !otherOn) continue;
                            b->setChecked(false);
                            modlistSettledPlugins.insert(fomod_choices::key(si, gi, pi), false);
                            addNoteUnder(b, QStringLiteral(
                                "For the original Morrowind.exe (MGE XE, MWSE, "
                                "Morrowind Code Patch). OpenMW runs none of them, "
                                "so this is unticked \u2014 tick it back only if "
                                "you also play without OpenMW."));
                        }
                    }

                    // Pass C: checkbox groups - match each plugin name against
                    // the modlist; auto-check + annotate on a hit.
                    //
                    // Absence used to say nothing here, on the grounds that an
                    // unchecked optional is clear enough. That holds only while
                    // the FOMOD ships its options OFF. Vehicle Overhaul
                    // Continued pre-ticks eighteen patches named after the mods
                    // they patch, so on a list holding none of them the silence
                    // installed all eighteen. A patch is also the one case this
                    // could not match even when the mod IS present, because the
                    // needle was the whole option name and "A Forest Patch"
                    // never matches a mod called "A Forest".
                    const bool hasPrior = priorGroups.contains(groupKey);
                    // Only SelectAny may be emptied. SelectAtLeastOne has to
                    // keep one, and deciding WHICH is not this pass's business.
                    const bool mayUntick = (group.type == QLatin1String("SelectAny"));
                    for (int pi = 0; pi < group.plugins.size(); ++pi) {
                        Option *btn = buttons[si][gi].value(pi);
                        if (!btn || !btn->isEnabled()) continue;

                        // An engine is never a mod in the modlist: "OpenMW"
                        // as a needle finds "OpenMW Quest Menu" and would
                        // announce that the mod is present.
                        if (engineOpts.contains(pi)) continue;

                        const QString pluginName = group.plugins[pi].name.trimmed();
                        if (pluginName.length() < 4) continue;

                        // A patch option names the mod it PATCHES, so ask about
                        // that instead of about the option's own name. Empty
                        // for everything that is not one, which leaves every
                        // ordinary option on the path below.
                        // "Foo - No BOS Patch" is a patch for Foo, for
                        // people WITHOUT BOS - not a patch for a mod called
                        // "Foo - No BOS". A negated name is Pass H territory
                        // in exclusive groups and silence everywhere else.
                        const QStringList &patchTargets = facts[si][gi][pi].patchTargets;

                        bool    pluginInstalled = false;
                        QString matchedMod;
                        if (!patchTargets.isEmpty()) {
                            // Any one candidate answering is enough: a combined
                            // patch offers both halves, and a mod with an
                            // acronym answers to either spelling.
                            for (const QString &target : patchTargets) {
                                matchedMod = mod_match::installedUnderAnyName(
                                    target, ctx.installedModNames);
                                if (!matchedMod.isEmpty()) { pluginInstalled = true; break; }
                            }
                        } else {
                        // Plugin name + aliases vs each mod name. Short needles
                        // (< 8 chars) must match start-of-name so "MWSE" doesn't
                        // hit "Graphic Herbalism MWSE - OpenMW".
                        for (const QString &needle : needlesFor(pluginName)) {
                            const bool shortNeedle = (needle.length() < 8);
                            const QString pat = shortNeedle
                                ? (QLatin1String("^") + QRegularExpression::escape(needle) + QLatin1String("\\b"))
                                : (QLatin1String("\\b") + QRegularExpression::escape(needle) + QLatin1String("\\b"));
                            const QRegularExpression re(pat, QRegularExpression::CaseInsensitiveOption);
                            for (const QString &modName : std::as_const(ctx.installedModNames)) {
                                if (re.match(modName).hasMatch()) { pluginInstalled = true; break; }
                            }
                            if (pluginInstalled) break;
                        }
                        }

                        if (pluginInstalled) {
                            if (!hasPrior) btn->setChecked(true);
                            btn->setText(btn->text() +
                                QStringLiteral(" \u2705 Recommended. The mod is currently present in the modlist."));
                            recommendedInstalledPlugins.insert(
                                (quint64(si) << 32) | (quint64(gi) << 16) | quint64(pi));
                        } else if (!patchTargets.isEmpty()) {
                            // Say so whether or not it was ticked - "which of
                            // these do I have?" is the question the list is
                            // silently asking - but only change a tick the
                            // FOMOD made, never one the user already chose.
                            btn->setText(btn->text() +
                                QStringLiteral(" \u26a0\ufe0f %1 is not installed in this modlist.")
                                    .arg(patchTargets.first()));
                            const QString tip = btn->toolTip();
                            btn->setToolTip((tip.isEmpty() ? QString()
                                                           : tip + QStringLiteral("\n\n"))
                                + QStringLiteral("This patch is for %1, which is not in this "
                                                 "modlist, so its files would be installed for "
                                                 "nothing. Tick it back if you have that mod "
                                                 "outside the manager, or are about to add it.")
                                      .arg(patchTargets.first()));
                            if (mayUntick && !hasPrior && btn->isChecked())
                                btn->setChecked(false);
                        }
                    }
                }
            }
        }

        // Pass D: patch-hub auto-tick. SelectAny groups where every plugin's
        // <files> include an .omwscripts entry -> tick all by default. These are
        // patch-hub mods (Completionist Patch Hub) named after landmass mods the
        // user is unlikely to have all of; Pass C would leave the group empty,
        // installing only the root .omwscripts + an empty scripts/, and OpenMW
        // then fails loading the orphan script declarations. The .omwscripts are
        // tiny and harmless when their target isn't loaded, so over-installing is
        // safer. Skipped when prior choices cover the group so an untick survives.
        for (int si = 0; si < steps.size(); ++si) {
            const FomodStep &step = steps[si];
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
                                 pi < buttons[si][gi].size(); ++pi) {
                    Option *btn = buttons[si][gi][pi];
                    if (!btn || !btn->isEnabled()) continue;
                    if (btn->isChecked()) continue;  // already on (Pass C)
                    btn->setChecked(true);
                    btn->setText(btn->text() +
                        QStringLiteral(" \u2705 Patch hub - default ON. "
                                       "Untick if you don't want this patch."));
                }
            }
        }

        // Pass E: compatibility options for mods the user does not have.
        // An Addendum to Tamrielic Lore Data offers Ashfall-compatible meshes
        // and pre-ticks a Glass Glowset option; with neither mod installed,
        // both quietly deliver meshes nothing will load. The option
        // descriptions link the mod they are for, and a Nexus mod-page URL
        // names exactly one page, so this matches by id - no name guessing,
        // and options that cite nothing stay silent. See fomod_hint.h.
        //
        // The verdict decides the tick, in both directions. Leaving the
        // author's default ticked underneath "Glass Glowset is not installed
        // in this modlist" states a fact and then acts against it, and the
        // warning is worth nothing if the box below it still installs meshes
        // for a mod that is not there. What IS in the modlist is a fact this
        // manager owns, so it answers with it - and the tick stays live, for
        // a copy installed outside the manager or one arriving later.
        //
        // Only where a tick is independently settable, as in Pass F: in an
        // exclusive group unticking means picking something else, which is
        // not ours to decide, so those are annotated only.
        for (int si = 0; si < steps.size(); ++si) {
            const FomodStep &step = steps[si];
            for (int gi = 0; gi < step.groups.size(); ++gi) {
                const FomodGroup &group = step.groups[gi];
                const bool tickable = group.type == QLatin1String("SelectAny")
                                   || group.type == QLatin1String("SelectAtLeastOne");

                // Collect, per cited mod page, the options citing it - the
                // option names are what name the mod when the group does not.
                QHash<QString, QStringList> citers;
                QList<QList<NexusModRef>>   perPlugin;
                perPlugin.reserve(group.plugins.size());
                for (int pi = 0; pi < group.plugins.size(); ++pi) {
                    const FomodPlugin &plugin = group.plugins[pi];
                    const QList<NexusModRef> &cited = facts[si][gi][pi].cited;
                    perPlugin.append(cited);
                    for (const NexusModRef &ref : cited) {
                        const QString key = ref.game.toLower() + u'/'
                                          + QString::number(ref.modId);
                        citers[key] << plugin.name;
                    }
                }

                for (int pi = 0; pi < group.plugins.size() &&
                                 pi < buttons[si][gi].size(); ++pi) {
                    const QList<NexusModRef> &cited = perPlugin[pi];
                    if (cited.isEmpty()) continue;   // no citation, no verdict

                    // A "(No BOS)" option routinely cites BOS's page - to say
                    // what it does WITHOUT. Vouching for it because the cited
                    // mod is installed recommends the wrong half of a variant
                    // pair, and unticking it because the mod is missing kills
                    // the one option made for that list. Not this pass's
                    // group either way: Pass H owns variant pairs, and a
                    // negated name elsewhere is safest left alone.
                    if (!facts[si][gi][pi].negatedMod.isEmpty())
                        continue;

                    Option *btn = buttons[si][gi][pi];
                    if (!btn) continue;

                    // An option citing several mods is only a problem when it
                    // has none of them; one present mod is reason enough for
                    // the option to exist.
                    QString     present;
                    QStringList missing;
                    for (const NexusModRef &ref : cited) {
                        const QString key = ref.game.toLower() + u'/'
                                          + QString::number(ref.modId);
                        const QString label =
                            fomod::missingModLabel(citers.value(key), group.name);
                        if (ctx.installedNexusKeys.contains(key)) {
                            present = label;
                            missing.clear();
                            break;
                        }
                        missing << label;
                    }

                    // A Required or NotUsable plugin is disabled: the FOMOD
                    // itself has already settled that tick, and there is no
                    // choice here to correct.
                    const quint64 key = (quint64(si) << 32) | (quint64(gi) << 16)
                                      | quint64(pi);
                    const bool settle = tickable && btn->isEnabled();
                    const QString tip = btn->toolTip();
                    auto addTip = [&](const QString &detail) {
                        btn->setToolTip((tip.isEmpty() ? QString()
                                                       : tip + QStringLiteral("\n\n"))
                                        + detail);
                    };

                    if (missing.isEmpty()) {
                        // Pass C already ticked this one and said why, having
                        // matched the option's own name against the modlist. A
                        // second tick and a second badge add nothing.
                        if (recommendedInstalledPlugins.contains(key)) continue;
                        if (settle) {
                            btn->setChecked(true);
                            modlistSettledPlugins.insert(key, true);
                        }
                        // Neither the options nor the group yielded a name
                        // worth printing, so say it without one - as the
                        // warning below does.
                        const QString what = present.isEmpty()
                            ? QStringLiteral("The mod this option is for")
                            : present;
                        btn->setText(btn->text() + (present.isEmpty()
                            ? QStringLiteral(" ✅ the mod this is for is installed")
                            : QStringLiteral(" ✅ %1 ✓").arg(present)));
                        addTip(settle
                            ? QStringLiteral("%1 is installed, so this option has "
                                             "been ticked.").arg(what)
                            : QStringLiteral("%1 is installed, so this option "
                                             "will work.").arg(what));
                        continue;
                    }

                    missing.removeDuplicates();
                    missing.removeAll(QString());

                    btn->setText(btn->text() + (missing.isEmpty()
                        ? QStringLiteral(" ⚠️ Warning: this option is "
                                         "for another mod that is not installed "
                                         "in this modlist.")
                        : QStringLiteral(" ⚠️ Warning: %1 is not "
                                         "installed in this modlist.")
                              .arg(missing.join(QStringLiteral(", ")))));
                    if (!settle) continue;
                    btn->setChecked(false);
                    modlistSettledPlugins.insert(key, false);
                    addTip(QStringLiteral("Unticked because that mod is not in this "
                                          "modlist, so these files would be installed "
                                          "for nothing. Tick it back if you have the "
                                          "mod outside the manager, or are about to "
                                          "add it."));
                }
            }
        }

        // A visible one-line explanation under an option, for choices the
        // wizard makes FOR the user. The reasoning used to live only in
        // tooltips, which nobody hovers - and "PRP v81 Previs" explains
        // nothing to someone who has never heard of PRP. Grey and indented,
        // so it reads as a footnote to the row above it, not another option.
        // Pass G: exclusive groups whose options name alternative FRAMEWORKS.
        //
        // Producers of Skyrim asks how to inject its orc-stronghold blacksmith
        // goods - Don't Install / Container Distribution Framework /
        // SkyPatcher - and ships with a framework pre-selected. With neither
        // installed that writes config files nothing reads: no crash, no
        // error, the blacksmiths simply have no goods.
        //
        // Runs BEFORE Pass F so a requirement stated in a description still
        // has the last word on the same option.
        for (int si = 0; si < steps.size(); ++si) {
            const FomodStep &step = steps[si];
            for (int gi = 0; gi < step.groups.size(); ++gi) {
                const FomodGroup &group = step.groups[gi];
                if (group.type != QLatin1String("SelectExactlyOne")
                    && group.type != QLatin1String("SelectAtMostOne")) continue;

                QStringList names;
                names.reserve(group.plugins.size());
                for (const FomodPlugin &p : group.plugins) names << p.name;

                const auto choice =
                    fomod::chooseFrameworkOption(names, ctx.installedModNames);
                if (choice.states.isEmpty()) continue;   // not a framework group

                using St = fomod::FrameworkChoice::State;
                for (int pi = 0; pi < names.size()
                                 && pi < buttons[si][gi].size(); ++pi) {
                    Option *btn = buttons[si][gi][pi];
                    if (!btn) continue;
                    const QString tip = btn->toolTip();
                    auto note = [&](const QString &label, const QString &detail) {
                        btn->setText(btn->text() + label);
                        btn->setToolTip((tip.isEmpty() ? QString() : tip + QStringLiteral("\n\n"))
                                        + detail);
                    };
                    // The resolved framework, spelled out, and what it is -
                    // visible under the row, because a tooltip is where an
                    // explanation goes to be missed, and "PRP" explains
                    // nothing on its own.
                    const QString full  = choice.fullNames.value(pi);
                    const QString blurb = blurbFor(full);
                    const QString what =
                        full.isEmpty() ? QString()
                        : (names[pi].contains(full, Qt::CaseInsensitive)
                               ? full
                               : QStringLiteral("%1 = %2")
                                     .arg(names[pi].section(QLatin1Char(' '), 0, 0),
                                          full))
                          + (blurb.isEmpty() ? QString()
                                             : QStringLiteral(" \u2014 %1").arg(blurb));

                    // Hardcoded English, like every other annotation in this
                    // file - only the dialog chrome goes through T().
                    if (choice.states[pi] == St::Installed) {
                        note(QStringLiteral(" \u2705"),
                             QStringLiteral("%1 is installed, so this option "
                                            "will work.").arg(names[pi]));
                        addNoteUnder(btn,
                            (what.isEmpty() ? QString() : what + QStringLiteral(". "))
                            + QStringLiteral("Installed in your mod list, so "
                                             "this option will use it."));
                    } else if (choice.states[pi] == St::Missing) {
                        note(QStringLiteral(" \u26A0\uFE0F not installed"),
                             QStringLiteral("%1 is not installed in this "
                                            "modlist. Choosing this writes "
                                            "configuration files nothing will "
                                            "read - no error, the feature "
                                            "simply does nothing.").arg(names[pi]));
                        addNoteUnder(btn,
                            (what.isEmpty() ? QString() : what + QStringLiteral(". "))
                            + QStringLiteral("Not in your mod list \u2014 pick "
                                             "this only if you plan to install "
                                             "it."));
                    } else if (choice.states[pi] == St::OptOut
                               && !choice.anyInstalled) {
                        note(QStringLiteral(" \u2705"),
                             QStringLiteral("None of the frameworks this offers "
                                            "is installed, so this is the only "
                                            "option that does what it says."));
                    } else if (choice.states[pi] == St::Baseline
                               && !choice.anyInstalled) {
                        // Not a resignation, unlike the opt-out above: the
                        // game's own data is a working choice, and the one the
                        // other options are alternatives TO.
                        note(QStringLiteral(" \u2705"),
                             QStringLiteral("This builds against the game's own "
                                            "files, which are always there. The "
                                            "mod the other option needs is not "
                                            "in this modlist, so this is the "
                                            "one that will work."));
                        addNoteUnder(btn,
                            QStringLiteral("The game's own built-in data \u2014 "
                                           "always present, needs no extra mod. "
                                           "The safe choice unless you install "
                                           "the framework the other option "
                                           "needs."));
                    }
                }

                if (choice.index >= 0 && choice.index < buttons[si][gi].size()) {
                    Option *pick = buttons[si][gi][choice.index];
                    if (pick && pick->isEnabled()) {
                        pick->setChecked(true);
                        if (choice.brokeTie)
                            pick->setToolTip(pick->toolTip()
                                + QStringLiteral("\n\nMore than one of these "
                                    "frameworks is installed, so either would "
                                    "work. This one is picked because more mods "
                                    "depend on it, making it the more "
                                    "field-tested - not because the other is "
                                    "known to be less stable."));
                    }
                    // Settled by what is installed, not by preference, so the
                    // prior-choices block must not undo it.
                    settledGroups.insert((quint64(si) << 16) | quint64(gi));
                }
            }
        }


        // Pass H: one mod, offered with and without a framework.
        //
        // Vehicle Overhaul Continued opens on "Vehicle Overhaul Continued"
        // vs "Vehicle Overhaul Continued (No BOS) [v1.2.2]", defaulting to
        // the No-BOS half - which on a list that HAS Base Object Swapper
        // silently gives up every swap the framework would drive. Worse,
        // Pass G briefly made it so: its mentioned-word lookup identified
        // the "(No BOS)" option AS Base Object Swapper and green-ticked the
        // wrong half, which is why chooseFrameworkOption now refuses negated
        // names outright. The marker is the evidence (fomod_hint.h), and the
        // modlist answers which half works.
        for (int si = 0; si < steps.size(); ++si) {
            const FomodStep &step = steps[si];
            for (int gi = 0; gi < step.groups.size(); ++gi) {
                const FomodGroup &group = step.groups[gi];
                if (group.type != QLatin1String("SelectExactlyOne")
                    && group.type != QLatin1String("SelectAtMostOne")) continue;

                QStringList names;
                names.reserve(group.plugins.size());
                for (const FomodPlugin &p : group.plugins) names << p.name;

                const auto v =
                    fomod::chooseFrameworkVariant(names, ctx.installedModNames);
                if (v.pick < 0 || v.pick >= buttons[si][gi].size()) continue;

                const auto annotate = [&](int pi, const QString &label,
                                          const QString &detail) {
                    Option *btn = buttons[si][gi].value(pi);
                    if (!btn) return;
                    if (!label.isEmpty()) btn->setText(btn->text() + label);
                    const QString tip = btn->toolTip();
                    btn->setToolTip((tip.isEmpty() ? QString()
                                                   : tip + QStringLiteral("\n\n"))
                                    + detail);
                };
                const QString hBlurb = blurbFor(v.framework);
                const QString hWhat = v.framework
                    + (hBlurb.isEmpty() ? QString()
                                        : QStringLiteral(" \u2014 %1").arg(hBlurb));
                if (v.installed) {
                    annotate(v.positiveIdx,
                             QStringLiteral(" \u2705 uses %1, which is installed")
                                 .arg(v.framework),
                             QStringLiteral("%1 is in this modlist, so the "
                                            "variant built on it is the one "
                                            "that does everything the mod "
                                            "promises.").arg(v.framework));
                    addNoteUnder(buttons[si][gi].value(v.positiveIdx),
                        QStringLiteral("Uses %1. Installed in your mod list, "
                                       "so this variant does everything the "
                                       "mod promises.").arg(hWhat));
                    annotate(v.negativeIdx, QString(),
                             QStringLiteral("This variant is for lists WITHOUT "
                                            "%1 - which you have installed. "
                                            "The recommended option above "
                                            "actually uses it.").arg(v.framework));
                    addNoteUnder(buttons[si][gi].value(v.negativeIdx),
                        QStringLiteral("For lists without %1 \u2014 which you "
                                       "have, so the other variant is the "
                                       "better fit.").arg(v.framework));
                } else {
                    annotate(v.negativeIdx,
                             QStringLiteral(" \u2705 works without %1")
                                 .arg(v.framework),
                             QStringLiteral("%1 is not in this modlist, and "
                                            "this variant exists for exactly "
                                            "that. The other option would "
                                            "install swaps nothing ever "
                                            "triggers.").arg(v.framework));
                    addNoteUnder(buttons[si][gi].value(v.negativeIdx),
                        QStringLiteral("Works without %1, which is not in "
                                       "your mod list \u2014 the right pick "
                                       "for this list.").arg(v.framework));
                    annotate(v.positiveIdx,
                             QStringLiteral(" \u26a0\ufe0f needs %1")
                                 .arg(v.framework),
                             QStringLiteral("%1 is not installed in this "
                                            "modlist, so this variant's extra "
                                            "content would silently never "
                                            "fire.").arg(v.framework));
                    addNoteUnder(buttons[si][gi].value(v.positiveIdx),
                        QStringLiteral("Needs %1. Not in your mod list \u2014 "
                                       "its extra content would silently "
                                       "never fire.").arg(hWhat));
                }

                Option *pick = buttons[si][gi].value(v.pick);
                if (pick && pick->isEnabled()) pick->setChecked(true);
                // Settled by what is installed, not by preference - the same
                // doctrine as Pass F - so the prior-choices block must not
                // undo it.
                settledGroups.insert((quint64(si) << 16) | quint64(gi));
            }
        }

        // Pass F: options whose description names a mod they REQUIRE.
        //
        // Grand Solitude's "SMIM Rotor" ships ticked and reads "Required
        // Static Mesh Improvement Mod - SMIM by Brumbek". With SMIM absent
        // that installs a mesh nothing loads correctly, and no URL is cited
        // so Pass E cannot see it. fomod::requiredMods reads the prose, and
        // only when what follows the keyword is shaped like a mod name - see
        // fomod_hint.h for the three corpus sentences that must stay quiet.
        //
        // This decides the tick in BOTH directions and overrides a stored
        // prior choice, unlike the passes above. Whether the required mod is
        // in the list right now is a fact about the modlist, not a
        // preference, and the same argument settles the Skyrim runtime pass.
        for (int si = 0; si < steps.size(); ++si) {
            const FomodStep &step = steps[si];
            for (int gi = 0; gi < step.groups.size(); ++gi) {
                const FomodGroup &group = step.groups[gi];
                // Only where a tick is independently settable. In an
                // exclusive group unticking means picking something else,
                // which is not ours to decide, so those are annotated only.
                const bool tickable = group.type == QLatin1String("SelectAny")
                                   || group.type == QLatin1String("SelectAtLeastOne");

                for (int pi = 0; pi < group.plugins.size() &&
                                 pi < buttons[si][gi].size(); ++pi) {
                    // Same negation guard as Pass E: a "(No X)" option that
                    // mentions X in its prose is not an option FOR X.
                    if (!facts[si][gi][pi].negatedMod.isEmpty())
                        continue;

                    const QStringList &needed = facts[si][gi][pi].required;
                    if (needed.isEmpty()) continue;

                    // Widen by the scene's acronyms, so "SMIM" finds a mod
                    // installed as "Static Mesh Improvement Mod" and back.
                    bool present = false;
                    QString firstName;
                    for (const QString &cand : mod_aliases::expand(needed)) {
                        if (firstName.isEmpty()) firstName = cand;
                        for (const QString &needle : needlesFor(cand)) {
                            const bool shortNeedle = (needle.length() < 8);
                            const QString pat = shortNeedle
                                ? (QLatin1String("^") + QRegularExpression::escape(needle) + QLatin1String("\\b"))
                                : (QLatin1String("\\b") + QRegularExpression::escape(needle) + QLatin1String("\\b"));
                            const QRegularExpression re(pat, QRegularExpression::CaseInsensitiveOption);
                            for (const QString &modName : std::as_const(ctx.installedModNames))
                                if (re.match(modName).hasMatch()) { present = true; break; }
                            if (present) break;
                        }
                        if (present) break;
                    }

                    Option *btn = buttons[si][gi][pi];
                    if (!btn || !btn->isEnabled()) continue;

                    const quint64 key = (quint64(si) << 32) | (quint64(gi) << 16)
                                      | quint64(pi);
                    if (tickable) modlistSettledPlugins.insert(key, present);

                    // Short on the label, full sentence in the tooltip. The
                    // long form ran off the end of the dialog and buried the
                    // option's own name, which is what the user is reading.
                    const QString tip = btn->toolTip();
                    if (present) {
                        if (tickable) btn->setChecked(true);
                        btn->setText(btn->text()
                            + QStringLiteral(" \u2705 %1 \u2713").arg(firstName));
                        btn->setToolTip(
                            (tip.isEmpty() ? QString() : tip + QStringLiteral("\n\n"))
                            + QStringLiteral("%1 is installed, so this option works.")
                                  .arg(firstName));
                    } else {
                        if (tickable) btn->setChecked(false);
                        btn->setText(btn->text()
                            + QStringLiteral(" \u26A0\uFE0F needs %1").arg(firstName));
                        btn->setToolTip(
                            (tip.isEmpty() ? QString() : tip + QStringLiteral("\n\n"))
                            + QStringLiteral("This option requires %1, which is not "
                                             "installed in this modlist, so it has "
                                             "been unticked.").arg(firstName));
                    }
                }
            }
        }
    }

    // Apply prior choices over the defaults above: the picks fomod_choices
    // found in this installer. Radio: check the stored plugin, QButtonGroup
    // clears the rest. Checkbox with any prior entry: uncheck all enabled, check
    // the stored ones. Groups with no prior entry keep the FOMOD defaults.
    if (!priorKeys.isEmpty()) {
        const QSet<quint64> &priorSet = priorKeys;
        const auto encode = &fomod_choices::key;
        for (int si = 0; si < steps.size() && si < buttons.size(); ++si) {
            const FomodStep &step = steps[si];
            for (int gi = 0; gi < step.groups.size() && gi < buttons[si].size(); ++gi) {
                const FomodGroup &group = step.groups[gi];
                if (group.type == "SelectAll") continue; // all forced on

                bool hasPrior = false;
                for (int pi = 0; pi < group.plugins.size(); ++pi) {
                    if (priorSet.contains(encode(si, gi, pi))) { hasPrior = true; break; }
                }
                if (!hasPrior) continue;
                // A group settled by a fact keeps its pick over a stored one.
                if (settledGroups.contains((quint64(si) << 16) | quint64(gi))) continue;

                bool exclusive = (group.type == "SelectExactlyOne" ||
                                  group.type == "SelectAtMostOne");
                if (exclusive) {
                    for (int pi = 0; pi < group.plugins.size() && pi < buttons[si][gi].size(); ++pi) {
                        Option *btn = buttons[si][gi][pi];
                        if (btn->isEnabled() && priorSet.contains(encode(si, gi, pi))) {
                            btn->setChecked(true);
                            break;
                        }
                    }
                } else {
                    // Uncheck all enabled, then check stored ones. Keep Pass C's
                    // Recommended (mod-installed) picks ticked even if the prior
                    // omitted them, else label and state disagree.
                    for (int pi = 0; pi < group.plugins.size() && pi < buttons[si][gi].size(); ++pi) {
                        Option *btn = buttons[si][gi][pi];
                        if (!btn->isEnabled()) continue;
                        const quint64 key = encode(si, gi, pi);
                        // A verdict from Pass E or F outranks the stored
                        // choice: the label says the cited mod is missing (or
                        // present), and a tick restored from a different
                        // modlist would contradict it on screen.
                        const auto settled = modlistSettledPlugins.constFind(key);
                        if (settled != modlistSettledPlugins.constEnd()) {
                            btn->setChecked(*settled);
                            continue;
                        }
                        btn->setChecked(priorSet.contains(key) ||
                                        recommendedInstalledPlugins.contains(key));
                    }
                }
            }
        }
    }

    return plan;
}

} // namespace fomod
