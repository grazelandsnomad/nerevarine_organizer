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

        // -- Creatures --------------------------------------------------
        // "Necrófago" is Bethesda's own Spanish for a ghoul, from the Fallout
        // localisations - which this manager also handles - and it is what
        // the Spanish-speaking community uses for the creature in Elder
        // Scrolls mods too. Google renders it as "Ghoul" or invents
        // "Demonio", neither of which names the thing.
        //
        // Plural listed separately because matching is whole-cell: a creature
        // shows up as "Ghoul" in one record and "Ghouls" in the next, and
        // "ghouls" is not "ghoul" to a QHash.
        {QStringLiteral("ghoul"),
         {{QStringLiteral("spanish"), QStringLiteral("Necrófago")}}},
        {QStringLiteral("ghouls"),
         {{QStringLiteral("spanish"), QStringLiteral("Necrófagos")}}},

        // -- Morrowind's own --------------------------------------------
        // The Blight is the ash-borne affliction Dagoth Ur spreads across
        // Vvardenfell, and Spanish-speaking Morrowind calls it el Tizon.
        // Google does not know that: left alone it returns "plaga" or
        // "anublo", and differently in each row, so one mod ends up naming the
        // same disease three ways.
        //
        // Article and all, because a whole-cell row that says "the Blight"
        // wants "el Tizon" and not "el el Tizon" - the same reason
        // "the companions" has an entry of its own above.
        //
        // The bare word carries the weight, though: it is also in
        // protectedTermsFor, which holds it back from the translator INSIDE a
        // sentence. That is where 130 of its 131 appearances are.
        {QStringLiteral("blight"),
         {{QStringLiteral("spanish"), QString::fromUtf8("Tizón")}}},
        {QStringLiteral("the blight"),
         {{QStringLiteral("spanish"), QString::fromUtf8("el Tizón")}}},

        // -- Districts, and the word for everyone who is not from here --
        //
        // Places a mod names over and over, where the damage is not a bad
        // rendering but an INCONSISTENT one: the map, the topic and the
        // dialogue end up calling one district three things. Measured across
        // the live list - Market Quarter 216 times, Council Quarter 209, Grand
        // Bazaar 172, Waterfront 184 - and almost none of it whole-cell. The
        // cells are compound ("Narsis, Sewers: Council Quarter West") and the
        // rest is dialogue, which is why every one of these is also in
        // protectedTermsFor below. The whole-cell entries here answer the four
        // dialogue TOPICS and supply the rendering the protected form
        // substitutes back in.
        //
        // The four Narsis districts are Tamriel Rebuilt's, and TR has no
        // official Spanish release, so unlike the rest of this table these are
        // a considered choice rather than a published name. Said plainly
        // because the header asks for established translations only, and
        // stretching that quietly is worse than noting where it was stretched.
        // Great Bazaar, Foreign Quarter and Outlander are vanilla and need no
        // such caveat.
        //
        // Grand Bazaar is Narsis and Great Bazaar is Mournhold's - two
        // different strings for two different markets, and vanilla says the
        // second 99 times. One entry could not cover both, and leaving either
        // out is how one playthrough ends up with two names for a bazaar.
        {QStringLiteral("grand bazaar"),
         {{QStringLiteral("spanish"), QStringLiteral("Gran Bazar")}}},
        {QStringLiteral("great bazaar"),
         {{QStringLiteral("spanish"), QStringLiteral("Gran Bazar")}}},
        {QStringLiteral("market quarter"),
         {{QStringLiteral("spanish"), QStringLiteral("Barrio del Mercado")}}},
        {QStringLiteral("council quarter"),
         {{QStringLiteral("spanish"), QStringLiteral("Barrio del Consejo")}}},
        {QStringLiteral("foreign quarter"),
         {{QStringLiteral("spanish"), QStringLiteral("Barrio Extranjero")}}},

        // Articled forms, for a row that IS the phrase - the same reason "the
        // blight" has an entry of its own. Note these are NOT protected for
        // the masculine districts, and deliberately: inside a sentence the
        // machine translates the article itself and contracts it, so "to the
        // NR0" comes back "al NR0" and unmasks to "al Gran Bazar". Masking the
        // article away would produce "a el Gran Bazar".
        {QStringLiteral("the grand bazaar"),
         {{QStringLiteral("spanish"), QStringLiteral("el Gran Bazar")}}},
        {QStringLiteral("the great bazaar"),
         {{QStringLiteral("spanish"), QStringLiteral("el Gran Bazar")}}},
        {QStringLiteral("the market quarter"),
         {{QStringLiteral("spanish"), QStringLiteral("el Barrio del Mercado")}}},
        {QStringLiteral("the council quarter"),
         {{QStringLiteral("spanish"), QStringLiteral("el Barrio del Consejo")}}},
        {QStringLiteral("the foreign quarter"),
         {{QStringLiteral("spanish"), QStringLiteral("el Barrio Extranjero")}}},

        // Ribera is the one FEMININE name here, and that changes how it has to
        // be handled. The machine supplies the article from a token it knows
        // nothing about, and an unknown Spanish noun defaults to masculine -
        // which is precisely why the Blight works and why this would not:
        // "on the NR0" comes back "en el NR0" and unmasks to "en el Ribera".
        //
        // So for this one term the ARTICLE is protected too (see below), and
        // the articled rendering carries it. Safe here and nowhere else,
        // because feminine articles never contract: "a la", "de la".
        //
        // 82 of its occurrences are "the Waterfront", so this is the common
        // form rather than a corner case.
        {QStringLiteral("waterfront"),
         {{QStringLiteral("spanish"), QStringLiteral("Ribera")}}},
        // Capital article, and not a typo: it is part of the name, the way
        // Spanish writes La Habana and El Cairo. Measured through the real
        // path against a live translator - lower-case "la Ribera" read
        // correctly in the 82 strings that say "on the Waterfront" and wrongly
        // in the 10 that OPEN with it, where "la Ribera es uno de los
        // puertos..." starts a sentence in lower case. Capitalised it is right
        // in both, since the article belongs to the name rather than to the
        // sentence.
        {QStringLiteral("the waterfront"),
         {{QStringLiteral("spanish"), QStringLiteral("La Ribera")}}},

        // What a Dunmer calls anyone who is not one, 171 times capitalised on
        // this list and nearly always addressing the player directly.
        //
        // Forastero is masculine and the player may not be, which is a real
        // cost accepted on purpose: Spanish game localisation defaults this
        // way, and the alternative is leaving 171 lines to a translator that
        // renders the word differently in each one.
        //
        // Plural listed separately because matching is whole-cell, the same
        // reason ghoul and ghouls are both above.
        {QStringLiteral("outlander"),
         {{QStringLiteral("spanish"), QStringLiteral("Forastero")}}},
        {QStringLiteral("outlanders"),
         {{QStringLiteral("spanish"), QStringLiteral("Forasteros")}}},

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

QStringList protectedTermsFor(const QString &token)
{
    if (norm(token) != QLatin1String("spanish")) return {};
    // Capitalised, because that is the whole safety argument: "Blight" is the
    // affliction, "blight" is what happens to a crop, and masking is
    // case-sensitive so the second is never touched. Measured on the live
    // list: 131 strings say Blight, 271 more say blight and mean the ordinary
    // word. The same split holds for everything added since: 184 strings say
    // Waterfront and 83 say waterfront, 171 say Outlander and 3447 say
    // outlander - the lower-case ones being ordinary words and the internal
    // ids a plugin is full of.
    //
    // The districts are here rather than only in the table because the table
    // is whole-cell and they almost never are: their cells read "Narsis,
    // Sewers: Council Quarter West" and the rest is dialogue. Protection is
    // what reaches inside those.
    //
    // "the Waterfront" is the one entry that carries an article, and it must
    // come before the bare word - mask() sorts longest first, so it does.
    // Ribera is feminine and the machine would otherwise guess "el" for the
    // token; see the table entry. Both capitalisations, because a sentence
    // that opens with it says "The".
    return { QStringLiteral("the Waterfront"), QStringLiteral("The Waterfront"),
             QStringLiteral("Blight"),
             QStringLiteral("Grand Bazaar"),   QStringLiteral("Great Bazaar"),
             QStringLiteral("Market Quarter"), QStringLiteral("Council Quarter"),
             QStringLiteral("Foreign Quarter"),
             QStringLiteral("Waterfront"),
             QStringLiteral("Outlanders"),     QStringLiteral("Outlander") };
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
