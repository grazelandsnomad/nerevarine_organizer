#include "separatordialog.h"
#include "translator.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLineEdit>
#include <QPushButton>
#include <QListWidget>
#include <QLabel>
#include <QDialogButtonBox>
#include <QColorDialog>
#include <QAction>
#include <QMenu>
#include <QSettings>

struct Preset { QString emoji, key; QColor bg, fg; };
static const QList<Preset> PRESETS = {
    {"🔊", "sep_preset_audio",        QColor("#EDD9F5"), QColor("#6B1A92")},
    {"⚖️", "sep_preset_balance",      QColor("#F5F0CC"), QColor("#6B6000")},
    {"🐛", "sep_preset_bugfixes",     QColor("#F5E2CC"), QColor("#7A3D00")},
    {"🛡️", "sep_preset_armor",        QColor("#6B1A1A"), QColor("#E8D4C8")},
    {"👥", "sep_preset_characters",   QColor("#F5D9EE"), QColor("#7A1A5C")},
    {"⚔️", "sep_preset_combat",       QColor("#F5D9D9"), QColor("#8C1A1A")},
    {"🗡️", "sep_preset_weapons",      QColor("#78788C"), QColor("#F0D890")},
    {"🏰", "sep_preset_dungeons",     QColor("#D9D9F5"), QColor("#1A1A90")},
    {"⚙️", "sep_preset_gameplay",     QColor("#CCF5F0"), QColor("#1A6860")},
    {"🖥️", "sep_preset_gui",          QColor("#EAD5F7"), QColor("#6B00AA")},
    {"🌿", "sep_preset_immersion",    QColor("#D9F5D9"), QColor("#1A6C1A")},
    {"📦", "sep_preset_items",        QColor("#F5ECD9"), QColor("#7A4E00")},
    {"🌄", "sep_preset_landscape",    QColor("#D9F5E8"), QColor("#1A6C48")},
    {"🧙", "sep_preset_magic",        QColor("#F2D9F5"), QColor("#6C1A7A")},
    {"🪄", "sep_preset_magic_only",   QColor("#3D2952"), QColor("#F5E0A8")},
    {"🗺️", "sep_preset_maps",         QColor("#CCF5F5"), QColor("#1A6868")},
    {"🎭", "sep_preset_overhauls",    QColor("#F5DDD9"), QColor("#7A2C1A")},
    {"📖", "sep_preset_quests",       QColor("#ECF5D9"), QColor("#3C6C1A")},
    {"🎨", "sep_preset_textures",     QColor("#1A1F2E"), QColor("#EC4899")},
    {"🏘️", "sep_preset_towns",        QColor("#D9F5E0"), QColor("#1A6C38")},
    {"💀", "sep_preset_monsters",     QColor("#F3E5F5"), QColor("#6A1B9A")},
    {"🐉", "sep_preset_monsters_creatures", QColor("#2E4A2B"), QColor("#F5E8B8")},
    {"🌍", "sep_preset_expansions",   QColor("#FFF5CC"), QColor("#7A5500")},
    {"🎬", "sep_preset_animations",   QColor("#D4F5D6"), QColor("#006B1A")},
    {"🛠️", "sep_preset_oaab_mods",    QColor("#5B9BD5"), QColor("#FFFFFF")},
    {"🏙️", "sep_preset_cities",       QColor("#2F4858"), QColor("#F2E8CF")},
    {"🚪", "sep_preset_interiors",    QColor("#3B2E2A"), QColor("#F4B942")},
    {"🐾", "sep_preset_companions",   QColor("#B284BE"), QColor("#3C1E5B")},
    {"📂", "sep_preset_other",        QColor("#6C757D"), QColor("#F8F9FA")},
    {"📐", "sep_preset_models",       QColor("#D4C5E8"), QColor("#4A2D73")},
    {"⏱️", "sep_preset_temp",         QColor("#FFB9A3"), QColor("#5C1F00")},
    // Utility mods: gold-on-dark-slate picks a combination none of the
    // existing 29 presets use, so framework sections (Skill Framework,
    // OAAB_Data, MWSE shims…) stand out at a glance without clashing
    // with content-category colours.
    {"🧩", "sep_preset_utility",      QColor("#2C2F3A"), QColor("#FFD700")},
    // NPCs: burnt-orange / deep-teal pair - deliberately distinct from
    // Characters (👥, pink bg) and Companions (🐾, mauve), so quest-giver /
    // named-individual mods get their own visual band.
    {"🧑", "sep_preset_npcs",         QColor("#F4A261"), QColor("#264653")},
    // Flora: deep leaf-green on pale lime - inverted brightness vs. the
    // other green presets (Immersion 🌿 / Landscape 🌄 / Animations 🎬 /
    // Towns 🏘️ all use pale-green bg + dark-green fg), so plant/tree mods
    // stay distinguishable from them at a glance.
    {"🌱", "sep_preset_flora",        QColor("#4A7C2A"), QColor("#DFF5C8")},
    // Visuals: deep violet on pale lavender - purple hues suit lighting, shaders,
    // post-processing and other purely visual enhancements; distinct from Textures (🎨)
    // which covers surface art rather than rendering effects.
    {"👁️", "sep_preset_visuals",      QColor("#E8D5F5"), QColor("#5B2D82")},
};

SeparatorDialog::SeparatorDialog(QWidget *parent)
    : QDialog(parent)
    , m_bgColor(QColor(55, 55, 75))
    , m_fgColor(Qt::white)
{
    setWindowTitle(T("sep_add_title"));
    setMinimumWidth(420);
    setupUI();
}

void SeparatorDialog::setupUI()
{
    auto *layout = new QVBoxLayout(this);
    layout->setSpacing(10);

    // --- Presets ---
    auto *presetsGroup  = new QGroupBox(T("sep_group_presets"));
    auto *presetsLayout = new QVBoxLayout(presetsGroup);

    m_searchEdit = new QLineEdit;
    m_searchEdit->setPlaceholderText(T("sep_search_placeholder"));
    m_searchEdit->setClearButtonEnabled(true);
    presetsLayout->addWidget(m_searchEdit);

    m_presetList = new QListWidget;
    m_presetList->setMaximumHeight(140);
    m_presetList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_presetList->setContextMenuPolicy(Qt::CustomContextMenu);
    populatePresets();
    presetsLayout->addWidget(m_presetList);
    layout->addWidget(presetsGroup);

    // --- Custom ---
    auto *customGroup = new QGroupBox(T("sep_group_customize"));
    auto *form = new QFormLayout(customGroup);
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);

    m_nameEdit = new QLineEdit;
    m_nameEdit->setPlaceholderText(T("sep_placeholder_name"));
    form->addRow(T("sep_label_name"), m_nameEdit);

    m_bgColorBtn = new QPushButton;
    m_bgColorBtn->setFixedHeight(28);
    applyColorToButton(m_bgColorBtn, m_bgColor);
    form->addRow(T("sep_label_bg"), m_bgColorBtn);

    m_fgColorBtn = new QPushButton;
    m_fgColorBtn->setFixedHeight(28);
    applyColorToButton(m_fgColorBtn, m_fgColor);
    form->addRow(T("sep_label_fg"), m_fgColorBtn);

    layout->addWidget(customGroup);

    // --- Live preview ---
    m_previewLabel = new QLabel;
    m_previewLabel->setFixedHeight(38);
    m_previewLabel->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    m_previewLabel->setContentsMargins(12, 0, 8, 0);
    m_previewLabel->setAutoFillBackground(false);
    layout->addWidget(m_previewLabel);
    updatePreview();

    // --- Buttons ---
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(buttons);

    connect(m_bgColorBtn,  &QPushButton::clicked,            this, &SeparatorDialog::onChooseBgColor);
    connect(m_fgColorBtn,  &QPushButton::clicked,            this, &SeparatorDialog::onChooseFgColor);
    connect(m_presetList,  &QListWidget::itemClicked,        this, &SeparatorDialog::onPresetClicked);
    connect(m_presetList,  &QListWidget::itemDoubleClicked,  this, &SeparatorDialog::onPresetDoubleClicked);
    connect(m_presetList,  &QListWidget::customContextMenuRequested,
            this, &SeparatorDialog::onPresetContextMenu);
    connect(m_searchEdit,  &QLineEdit::textChanged,          this, &SeparatorDialog::onSearchChanged);
    connect(m_nameEdit,    &QLineEdit::textChanged,          this, &SeparatorDialog::updatePreview);
    connect(buttons,       &QDialogButtonBox::accepted,      this, &QDialog::accept);
    connect(buttons,       &QDialogButtonBox::rejected,      this, &QDialog::reject);
}

void SeparatorDialog::populatePresets()
{
    // Presets the user has hidden via right-click → "Remove suggested separator".
    // Persisted so the hide sticks across reopens and restarts; we keep the
    // hidden keys rather than rewriting PRESETS so the user can re-enable a
    // preset later by clearing the setting.
    const QSet<QString> hidden = [] {
        const QStringList stored =
            QSettings().value("separators/hidden_presets").toStringList();
        return QSet<QString>(stored.begin(), stored.end());
    }();

    m_presetList->clear();
    QFont bold = m_presetList->font();
    bold.setBold(true);
    for (const auto &p : PRESETS) {
        if (hidden.contains(p.key)) continue;
        QString label = p.emoji + "  " + T(p.key);
        auto *item = new QListWidgetItem(label);
        item->setData(Qt::UserRole,     label);
        item->setData(Qt::UserRole + 1, p.key);
        item->setBackground(p.bg);
        item->setForeground(p.fg);
        item->setFont(bold);
        m_presetList->addItem(item);
    }
}

void SeparatorDialog::prefill(const QString &name, const QColor &bg, const QColor &fg)
{
    m_nameEdit->setText(name);
    m_bgColor = bg;
    m_fgColor = fg;
    applyColorToButton(m_bgColorBtn, bg);
    applyColorToButton(m_fgColorBtn, fg);
    updatePreview();
    setWindowTitle(T("sep_edit_title"));
}

void SeparatorDialog::onChooseBgColor()
{
    QColor c = QColorDialog::getColor(m_bgColor, this, T("sep_choose_bg"),
                                      QColorDialog::ShowAlphaChannel);
    if (!c.isValid()) return;
    m_bgColor = c;
    applyColorToButton(m_bgColorBtn, c);
    updatePreview();
}

void SeparatorDialog::onChooseFgColor()
{
    QColor c = QColorDialog::getColor(m_fgColor, this, T("sep_choose_fg"));
    if (!c.isValid()) return;
    m_fgColor = c;
    applyColorToButton(m_fgColorBtn, c);
    updatePreview();
}

void SeparatorDialog::onPresetClicked(QListWidgetItem *item)
{
    m_nameEdit->setText(item->data(Qt::UserRole).toString());
    m_bgColor = item->background().color();
    m_fgColor = item->foreground().color();
    applyColorToButton(m_bgColorBtn, m_bgColor);
    applyColorToButton(m_fgColorBtn, m_fgColor);
    updatePreview();
}

void SeparatorDialog::onPresetDoubleClicked(QListWidgetItem *item)
{
    onPresetClicked(item);
    accept();
}

void SeparatorDialog::onPresetContextMenu(const QPoint &pos)
{
    QListWidgetItem *item = m_presetList->itemAt(pos);
    if (!item) return;
    const QString key = item->data(Qt::UserRole + 1).toString();
    if (key.isEmpty()) return;

    QMenu menu(this);
    QAction *removeAct = menu.addAction(T("sep_remove_preset"));
    if (menu.exec(m_presetList->viewport()->mapToGlobal(pos)) != removeAct)
        return;

    QSettings s;
    QStringList hidden = s.value("separators/hidden_presets").toStringList();
    if (!hidden.contains(key)) {
        hidden << key;
        s.setValue("separators/hidden_presets", hidden);
    }
    populatePresets();
    onSearchChanged(m_searchEdit->text());
}

void SeparatorDialog::onSearchChanged(const QString &text)
{
    QListWidgetItem *sole = nullptr;
    int visibleCount = 0;
    for (int i = 0; i < m_presetList->count(); ++i) {
        auto *item = m_presetList->item(i);
        bool matches = text.isEmpty() ||
                       item->text().contains(text, Qt::CaseInsensitive);
        item->setHidden(!matches);
        if (matches) { sole = item; ++visibleCount; }
    }
    // Auto-select and apply when exactly one preset matches
    if (visibleCount == 1 && sole) {
        m_presetList->setCurrentItem(sole);
        onPresetClicked(sole);
    }
}

void SeparatorDialog::updatePreview()
{
    QString text = m_nameEdit->text().isEmpty()
        ? T("sep_preview_default")
        : m_nameEdit->text();
    m_previewLabel->setStyleSheet(
        QString("background-color: %1; color: %2; font-weight: bold; font-size: 13px;")
            .arg(m_bgColor.name())
            .arg(m_fgColor.name()));
    m_previewLabel->setText("  " + text);
}

void SeparatorDialog::applyColorToButton(QPushButton *btn, const QColor &color)
{
    bool dark = color.lightness() < 128;
    btn->setStyleSheet(
        QString("background-color: %1; color: %2; border: 1px solid #555;")
            .arg(color.name())
            .arg(dark ? "white" : "black"));
    btn->setText(color.name().toUpper());
}

QString SeparatorDialog::separatorName()    const { return m_nameEdit->text(); }
QColor  SeparatorDialog::backgroundColor() const { return m_bgColor; }
QColor  SeparatorDialog::fontColor()        const { return m_fgColor; }
