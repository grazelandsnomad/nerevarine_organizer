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

QStringList termsFor(const QString &token)
{
    const QString lang = norm(token);
    QStringList out;
    for (auto it = table().cbegin(); it != table().cend(); ++it)
        if (it.value().contains(lang)) out << it.key();
    return out;
}

} // namespace lore_overrides
