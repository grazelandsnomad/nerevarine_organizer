#include "lore_overrides.h"

#include <QHash>

namespace lore_overrides {
namespace {

// source term (lowercased) -> language token -> canonical translation.
//
// Spanish only for now, because that is the one the author can vouch for.
// Adding a language means having someone who plays in it check every line -
// an unverified entry here is worse than none, since it displaces the machine
// guess while looking authoritative.
const QHash<QString, QHash<QString, QString>> &table()
{
    static const QHash<QString, QHash<QString, QString>> kTable = {
        // -- Factions and orders ---------------------------------------
        {QStringLiteral("dark brotherhood"),
         {{QStringLiteral("spanish"), QStringLiteral("Hermandad Oscura")}}},
        {QStringLiteral("shadowscales"),
         // The Argonian assassins. Google's literal "escamas de sombra" loses
         // the proper noun; this is the form the Spanish TES material uses.
         {{QStringLiteral("spanish"), QStringLiteral("Escamas Sombrías")}}},
        {QStringLiteral("thieves guild"),
         {{QStringLiteral("spanish"), QStringLiteral("Gremio de Ladrones")}}},
        {QStringLiteral("mages guild"),
         {{QStringLiteral("spanish"), QStringLiteral("Gremio de Magos")}}},
        {QStringLiteral("fighters guild"),
         {{QStringLiteral("spanish"), QStringLiteral("Gremio de Luchadores")}}},
        {QStringLiteral("imperial legion"),
         {{QStringLiteral("spanish"), QStringLiteral("Legión Imperial")}}},
        {QStringLiteral("the companions"),
         {{QStringLiteral("spanish"), QStringLiteral("Los Compañeros")}}},
        {QStringLiteral("greybeards"),
         {{QStringLiteral("spanish"), QStringLiteral("Barbas Grises")}}},

        // -- Titles ----------------------------------------------------
        {QStringLiteral("dragonborn"),
         {{QStringLiteral("spanish"), QStringLiteral("Sangre de Dragón")}}},
        {QStringLiteral("nerevarine"),
         {{QStringLiteral("spanish"), QStringLiteral("Nerevarina")}}},
        {QStringLiteral("daedric prince"),
         {{QStringLiteral("spanish"), QStringLiteral("Príncipe Daédrico")}}},
        {QStringLiteral("archmage"),
         {{QStringLiteral("spanish"), QStringLiteral("Archimago")}}},

        // -- Terms of art ----------------------------------------------
        {QStringLiteral("thu'um"),
         {{QStringLiteral("spanish"), QStringLiteral("Thu'um")}}},
        {QStringLiteral("shout"),
         {{QStringLiteral("spanish"), QStringLiteral("Grito")}}},
        {QStringLiteral("word wall"),
         {{QStringLiteral("spanish"), QStringLiteral("Muro de Palabras")}}},
        {QStringLiteral("soul gem"),
         {{QStringLiteral("spanish"), QStringLiteral("Gema de almas")}}},

        // -- Great houses -----------------------------------------------
        // Exact entries rather than a "House %1" shape, because that shape
        // would also catch the rank titles ("House Brother", "House Father")
        // which do have to be translated.
        //
        // These five are the only entries here the author has not verified
        // for me. Correct any that read wrong; the form used in Spanish TES
        // material is "Casa <name>", with the house name left alone.
        {QStringLiteral("house hlaalu"),
         {{QStringLiteral("spanish"), QStringLiteral("Casa Hlaalu")}}},
        {QStringLiteral("house redoran"),
         {{QStringLiteral("spanish"), QStringLiteral("Casa Redoran")}}},
        {QStringLiteral("house telvanni"),
         {{QStringLiteral("spanish"), QStringLiteral("Casa Telvanni")}}},
        {QStringLiteral("house indoril"),
         {{QStringLiteral("spanish"), QStringLiteral("Casa Indoril")}}},
        {QStringLiteral("house dres"),
         {{QStringLiteral("spanish"), QStringLiteral("Casa Dres")}}},

        // -- Worship titles (Varieties of Faith) ------------------------
        // The shape "%1 Devotee" in patternsFor() answers the other
        // seventeen. These two are here because the shape gets them wrong.
        {QStringLiteral("talos cult devotee"),
         // "Talos Cult" is a faction, not a deity - the mod also has "Abandon
         // the Talos Cult" - so the shape's "Devoto de Talos Cult" would
         // leave half of it in English.
         {{QStringLiteral("spanish"), QStringLiteral("Devoto del Culto de Talos")}}},
        {QStringLiteral("devotee of the one"),
         // Already written the other way round, so the shape never matches it.
         {{QStringLiteral("spanish"), QStringLiteral("Devoto del Único")}}},

        // -- Proper nouns that must survive untouched -------------------
        // Mapped to themselves on purpose: this is how a name is protected
        // from a translator that would otherwise invent something. See the
        // header - the editor drops rows whose translation equals the source,
        // so these correctly end up left alone in the plugin.
        {QStringLiteral("skooma"),
         {{QStringLiteral("spanish"), QStringLiteral("Skooma")}}},
        {QStringLiteral("sujamma"),
         {{QStringLiteral("spanish"), QStringLiteral("Sujamma")}}},
        {QStringLiteral("moon sugar"),
         {{QStringLiteral("spanish"), QStringLiteral("Azúcar lunar")}}},
        {QStringLiteral("dwemer"),
         {{QStringLiteral("spanish"), QStringLiteral("Dwemer")}}},
        {QStringLiteral("daedra"),
         {{QStringLiteral("spanish"), QStringLiteral("Daedra")}}},
        {QStringLiteral("dremora"),
         {{QStringLiteral("spanish"), QStringLiteral("Dremora")}}},
        {QStringLiteral("draugr"),
         {{QStringLiteral("spanish"), QStringLiteral("Draugr")}}},
        {QStringLiteral("falmer"),
         {{QStringLiteral("spanish"), QStringLiteral("Falmer")}}},
    };
    return kTable;
}

QString norm(const QString &s) { return s.trimmed().toLower(); }

} // namespace

QString lookup(const QString &text, const QString &token)
{
    const QString key = norm(text);
    if (key.isEmpty()) return {};

    const auto outer = table().constFind(key);
    if (outer == table().constEnd()) return {};

    return outer->value(norm(token));
}

QList<QPair<QString, QString>> patternsFor(const QString &token)
{
    // "Devoto", masculine, because one form has to be picked: the title is
    // the player's rather than the deity's, and Morrowind has no gendered
    // substitution to carry the other one.
    static const QList<QPair<QString, QString>> kSpanish = {
        {QStringLiteral("%1 Devotee"), QStringLiteral("Devoto de %1")},

        // Morrowind naming families, each mapping to ITSELF. There is no
        // Spanish in these to get wrong: they exist to keep a name out of the
        // translator's hands, the same job the self-mapping entries in the
        // table above do for "Skooma" and "Dwemer".
        //
        // Counted across the author's Morrowind mods, the second word is
        // different nearly every time, which is what makes a shape the only
        // way to say it: Dagoth 17, Tel 24, Ald 15, Clan 9 distinct names.
        // Every one of them was going to the translator, and "Dagoth Andas"
        // came back "sin respirar".
        {QStringLiteral("Dagoth %1"), QStringLiteral("Dagoth %1")},
        {QStringLiteral("Tel %1"),    QStringLiteral("Tel %1")},
        {QStringLiteral("Ald %1"),    QStringLiteral("Ald %1")},
        {QStringLiteral("Clan %1"),   QStringLiteral("Clan %1")},
        // Deliberately NOT "House %1". The same scan finds it carrying both
        // place names (Hlaalu, Redoran, Telvanni) and rank titles that have
        // to translate (House Brother, House Father, House Officer), so a
        // shape would freeze the wrong half. The five great houses are exact
        // entries in the table instead.
    };
    return norm(token) == QLatin1String("spanish") ? kSpanish
                                                   : QList<QPair<QString, QString>>{};
}

QStringList termsFor(const QString &token)
{
    const QString lang = norm(token);
    QStringList out;
    for (auto it = table().cbegin(); it != table().cend(); ++it)
        if (it.value().contains(lang)) out << it.key();
    return out;
}

} // namespace lore_overrides
