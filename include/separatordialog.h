#ifndef SEPARATORDIALOG_H
#define SEPARATORDIALOG_H

#include <QDialog>
#include <QColor>
#include <QString>

class QLineEdit;
class QPushButton;
class QListWidget;
class QListWidgetItem;
class QLabel;

class SeparatorDialog : public QDialog {
    Q_OBJECT
public:
    explicit SeparatorDialog(QWidget *parent = nullptr);

    // Pre-fill for editing an existing separator
    void prefill(const QString &name, const QColor &bg, const QColor &fg);

    QString separatorName() const;
    QColor  backgroundColor() const;
    QColor  fontColor() const;

private slots:
    void onChooseBgColor();
    void onChooseFgColor();
    void onPresetClicked(QListWidgetItem *item);
    void onPresetDoubleClicked(QListWidgetItem *item);
    void onPresetContextMenu(const QPoint &pos);
    void onSearchChanged(const QString &text);
    void updatePreview();

private:
    void setupUI();
    void populatePresets();
    void applyColorToButton(QPushButton *btn, const QColor &color);

    QLineEdit    *m_nameEdit;
    QLineEdit    *m_searchEdit;
    QPushButton  *m_bgColorBtn;
    QPushButton  *m_fgColorBtn;
    QListWidget  *m_presetList;
    QLabel       *m_previewLabel;

    QColor m_bgColor;
    QColor m_fgColor;
};

#endif // SEPARATORDIALOG_H
