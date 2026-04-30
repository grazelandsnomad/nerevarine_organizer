#include "firstrunwizard.h"

#include "translator.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QWizard>
#include <QWizardPage>

namespace firstrun {

namespace {

// -- Page 1: pick a game ---
class GamePage : public QWizardPage {
public:
    GamePage(const QList<GameChoice> &games, QWidget *parent = nullptr)
        : QWizardPage(parent), m_games(games)
    {
        setTitle(T("wizard_game_title"));
        setSubTitle(T("wizard_game_subtitle"));

        auto *v = new QVBoxLayout(this);
        v->addWidget(new QLabel(T("wizard_game_body")));

        m_combo = new QComboBox(this);
        for (const GameChoice &g : m_games)
            m_combo->addItem(g.displayName, g.id);

        // Default to Morrowind (the flagship integration).
        int def = m_combo->findData(QStringLiteral("morrowind"));
        if (def >= 0) m_combo->setCurrentIndex(def);

        v->addWidget(m_combo);
        v->addStretch();

        // Stored in a private dynamic property on the wizard via field.
        registerField("game_id*", m_combo, "currentData",
                      SIGNAL(currentIndexChanged(int)));
    }

    QString selectedId() const { return m_combo->currentData().toString(); }
    QString selectedDefaultDir() const {
        QString id = selectedId();
        for (const GameChoice &g : m_games)
            if (g.id == id) return g.defaultModsDirName;
        return QStringLiteral("nerevarine_mods");
    }

private:
    const QList<GameChoice> &m_games;
    QComboBox *m_combo = nullptr;
};

// -- Page 2: pick a mods directory ---
class ModsDirPage : public QWizardPage {
public:
    explicit ModsDirPage(GamePage *gamePage, QWidget *parent = nullptr)
        : QWizardPage(parent), m_gamePage(gamePage)
    {
        setTitle(T("wizard_modsdir_title"));
        setSubTitle(T("wizard_modsdir_subtitle"));

        auto *v = new QVBoxLayout(this);
        v->addWidget(new QLabel(T("wizard_modsdir_body")));

        auto *row = new QHBoxLayout;
        m_edit = new QLineEdit(this);
        auto *browse = new QPushButton(T("wizard_modsdir_browse"), this);
        connect(browse, &QPushButton::clicked, this, [this]() {
            QString picked = QFileDialog::getExistingDirectory(
                this, T("wizard_modsdir_picker_title"),
                m_edit->text().isEmpty() ? QDir::homePath() : m_edit->text());
            if (!picked.isEmpty()) m_edit->setText(picked);
        });
        row->addWidget(m_edit, 1);
        row->addWidget(browse);
        v->addLayout(row);
        v->addStretch();

        registerField("mods_dir*", m_edit);
    }

    void initializePage() override {
        // Re-seed with the default path for the currently-selected game every
        // time the page is shown (if the edit is empty OR still matches the
        // previous game's default - don't clobber a user-edited path).
        QString defName = m_gamePage ? m_gamePage->selectedDefaultDir()
                                      : QStringLiteral("nerevarine_mods");
        QString defPath = QDir::homePath() + "/Games/" + defName;
        if (m_edit->text().isEmpty())
            m_edit->setText(defPath);
    }

    QString modsDir() const { return m_edit->text().trimmed(); }

private:
    GamePage  *m_gamePage = nullptr;
    QLineEdit *m_edit     = nullptr;
};

// -- Page 3: Nexus API key (optional) ---
class ApiKeyPage : public QWizardPage {
public:
    explicit ApiKeyPage(QWidget *parent = nullptr)
        : QWizardPage(parent)
    {
        setTitle(T("wizard_apikey_title"));
        setSubTitle(T("wizard_apikey_subtitle"));

        auto *v = new QVBoxLayout(this);
        v->addWidget(new QLabel(T("wizard_apikey_body")));

        auto *linkBtn = new QPushButton(T("wizard_apikey_open_page"), this);
        connect(linkBtn, &QPushButton::clicked, this, []() {
            QDesktopServices::openUrl(
                QUrl("https://www.nexusmods.com/users/myaccount?tab=api"));
        });

        m_edit = new QLineEdit(this);
        m_edit->setEchoMode(QLineEdit::Password);
        m_edit->setPlaceholderText(T("wizard_apikey_placeholder"));

        v->addWidget(linkBtn);
        v->addWidget(m_edit);
        v->addWidget(new QLabel(T("wizard_apikey_skip_hint")));
        v->addStretch();

        // No '*' - this field is OPTIONAL; the wizard won't gate Next on it.
        registerField("api_key", m_edit);
    }

    QString apiKey() const { return m_edit->text().trimmed(); }

private:
    QLineEdit *m_edit = nullptr;
};

// -- Page 4: integrations (NXM handler + LOOT install hint) ---
class IntegrationsPage : public QWizardPage {
public:
    explicit IntegrationsPage(QWidget *parent = nullptr)
        : QWizardPage(parent)
    {
        setTitle(T("wizard_integrations_title"));
        setSubTitle(T("wizard_integrations_subtitle"));

        auto *v = new QVBoxLayout(this);

        // NXM handler
        v->addWidget(new QLabel(T("wizard_nxm_body")));
        m_nxmCheck = new QCheckBox(T("wizard_nxm_checkbox"), this);
        m_nxmCheck->setChecked(true);
        v->addWidget(m_nxmCheck);
        v->addSpacing(12);

        // LOOT
        v->addWidget(new QLabel(T("wizard_loot_body")));
        auto *lootBtn = new QPushButton(T("wizard_loot_open_page"), this);
        connect(lootBtn, &QPushButton::clicked, this, []() {
            QDesktopServices::openUrl(QUrl("https://loot.github.io/"));
        });
        v->addWidget(lootBtn);
        v->addSpacing(12);

        v->addWidget(new QLabel(T("wizard_integrations_finish_hint")));
        v->addStretch();

        registerField("nxm_register", m_nxmCheck);
    }

    bool registerNxm() const { return m_nxmCheck->isChecked(); }

private:
    QCheckBox *m_nxmCheck = nullptr;
};

} // namespace

bool runWizard(QWidget *parent, const QList<GameChoice> &games, Result &out)
{
    QWizard wiz(parent);
    wiz.setWindowTitle(T("wizard_window_title"));
    wiz.setWizardStyle(QWizard::ModernStyle);
    wiz.setOption(QWizard::HaveHelpButton, false);
    wiz.setOption(QWizard::NoBackButtonOnStartPage, true);
    wiz.setMinimumSize(560, 420);

    auto *p1 = new GamePage(games);
    auto *p2 = new ModsDirPage(p1);
    auto *p3 = new ApiKeyPage();
    auto *p4 = new IntegrationsPage();

    wiz.addPage(p1);
    wiz.addPage(p2);
    wiz.addPage(p3);
    wiz.addPage(p4);

    if (wiz.exec() != QDialog::Accepted) return false;

    out.gameId       = p1->selectedId();
    out.modsDir      = p2->modsDir();
    out.apiKey       = p3->apiKey();
    out.registerNxm  = p4->registerNxm();
    return true;
}

} // namespace firstrun
