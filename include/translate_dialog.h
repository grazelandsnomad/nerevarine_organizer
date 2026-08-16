#ifndef TRANSLATE_DIALOG_H
#define TRANSLATE_DIALOG_H

// The editor behind "Translate mod...": every player-visible string the mod
// carries, with a field for what it should say instead.
//
// Rows are grouped by source text, not by record. A mod that names twenty
// bandits "Bandit Chief" asks once and writes twenty records, because retyping
// the same words is exactly the work this is meant to remove. The translation
// memory (translation_store) then carries that answer to every OTHER mod, so
// the second mod to mention a Bandit Chief arrives pre-filled.
//
// Nothing is written until the user accepts. On accept the caller gets
// replacements keyed per plugin, ready for translation_mod::build.

#include <QDialog>
#include <QHash>
#include <QList>
#include <QString>

#include "translation_mod.h"
#include "translation_store.h"

class QLineEdit;
class QNetworkAccessManager;
class QProgressBar;
class QPushButton;
class QTableWidget;

// One translatable string, as extracted from a plugin.
struct TranslatableString {
    QString pluginRel;   // path relative to the mod folder
    QString key;         // plugin_strings key, "TYPE:formid:SUB:index"
    QString source;      // the English text
    bool    secondary = false;  // came from the secondary tier (an NPC_ name)
};

class TranslateDialog : public QDialog {
    Q_OBJECT
public:
    // `language` is the lowercase token ("spanish"); `memory` is pre-filled
    // into empty rows and updated in place when the user accepts.
    TranslateDialog(const QString &modName,
                    const QList<TranslatableString> &strings,
                    const QString &language,
                    translation_store::Memory *memory,
                    QWidget *parent = nullptr);

    // Valid after exec() returns Accepted. Only strings the user actually
    // filled in appear; an untouched row is left English rather than blanked.
    translation_mod::ByPlugin replacements() const { return m_result; }

    // True when the user changed at least one entry, so the caller knows
    // whether the memory is worth saving.
    bool memoryChanged() const { return m_memoryChanged; }

private slots:
    void onAccept();
    void onMachineTranslate();
    void onImportDatabase();

private:
    // Drains m_mtQueue while fewer than kMaxInFlight requests are outstanding;
    // each reply calls back in to keep the queue moving.
    void pumpMachineTranslate();

private:
    void buildUi(const QString &modName);
    void fillFromMemory();

    QList<TranslatableString> m_strings;
    // Row -> the source text it edits. Rows are unique source strings.
    QStringList               m_rowSource;
    QString                   m_language;
    translation_store::Memory *m_memory = nullptr;
    translation_mod::ByPlugin m_result;
    bool                      m_memoryChanged = false;

    QTableWidget *m_table   = nullptr;
    QPushButton  *m_mtBtn   = nullptr;
    QProgressBar *m_mtBar   = nullptr;
    QNetworkAccessManager *m_net = nullptr;
    QList<int>    m_mtQueue;        // rows still waiting to be sent
    int           m_mtInFlight = 0;
    int           m_mtDone     = 0;
    int           m_mtTotal    = 0;
    int           m_mtFailed   = 0;
};

#endif // TRANSLATE_DIALOG_H
