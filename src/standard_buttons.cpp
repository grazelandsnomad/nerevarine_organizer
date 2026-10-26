#include "standard_buttons.h"

#include <QAbstractButton>
#include <QApplication>
#include <QCoreApplication>
#include <QEvent>
#include <QObject>

namespace standard_buttons {

QString text(QDialogButtonBox::StandardButton button)
{
    // QPlatformTheme::defaultStandardButtonText's strings, in its context, so
    // the qtbase translations - and qt_strings' English - answer them.
    const char *source = nullptr;
    switch (button) {
    case QDialogButtonBox::Ok:              source = QT_TRANSLATE_NOOP("QPlatformTheme", "OK"); break;
    case QDialogButtonBox::Save:            source = QT_TRANSLATE_NOOP("QPlatformTheme", "Save"); break;
    case QDialogButtonBox::SaveAll:         source = QT_TRANSLATE_NOOP("QPlatformTheme", "Save All"); break;
    case QDialogButtonBox::Open:            source = QT_TRANSLATE_NOOP("QPlatformTheme", "Open"); break;
    case QDialogButtonBox::Yes:             source = QT_TRANSLATE_NOOP("QPlatformTheme", "&Yes"); break;
    case QDialogButtonBox::YesToAll:        source = QT_TRANSLATE_NOOP("QPlatformTheme", "Yes to &All"); break;
    case QDialogButtonBox::No:              source = QT_TRANSLATE_NOOP("QPlatformTheme", "&No"); break;
    case QDialogButtonBox::NoToAll:         source = QT_TRANSLATE_NOOP("QPlatformTheme", "N&o to All"); break;
    case QDialogButtonBox::Abort:           source = QT_TRANSLATE_NOOP("QPlatformTheme", "Abort"); break;
    case QDialogButtonBox::Retry:           source = QT_TRANSLATE_NOOP("QPlatformTheme", "Retry"); break;
    case QDialogButtonBox::Ignore:          source = QT_TRANSLATE_NOOP("QPlatformTheme", "Ignore"); break;
    case QDialogButtonBox::Close:           source = QT_TRANSLATE_NOOP("QPlatformTheme", "Close"); break;
    case QDialogButtonBox::Cancel:          source = QT_TRANSLATE_NOOP("QPlatformTheme", "Cancel"); break;
    case QDialogButtonBox::Discard:         source = QT_TRANSLATE_NOOP("QPlatformTheme", "Discard"); break;
    case QDialogButtonBox::Help:            source = QT_TRANSLATE_NOOP("QPlatformTheme", "Help"); break;
    case QDialogButtonBox::Apply:           source = QT_TRANSLATE_NOOP("QPlatformTheme", "Apply"); break;
    case QDialogButtonBox::Reset:           source = QT_TRANSLATE_NOOP("QPlatformTheme", "Reset"); break;
    case QDialogButtonBox::RestoreDefaults: source = QT_TRANSLATE_NOOP("QPlatformTheme", "Restore Defaults"); break;
    default: break;
    }
    return source ? QCoreApplication::translate("QPlatformTheme", source) : QString();
}

void relabel(QDialogButtonBox *box, const QHash<int, QString> &themeTexts)
{
    if (!box) return;
    const QList<QAbstractButton *> buttons = box->buttons();
    for (QAbstractButton *b : buttons) {
        const QDialogButtonBox::StandardButton which = box->standardButton(b);
        if (which == QDialogButtonBox::NoButton) continue;
        const auto theme = themeTexts.constFind(int(which));
        if (theme == themeTexts.cend() || b->text() != theme.value()) continue;
        const QString ours = text(which);
        if (!ours.isEmpty() && ours != b->text()) b->setText(ours);
    }
}

namespace {

// Every standard button the theme can label, as one hidden row: what it
// names each, read once the theme is loaded.
QHash<int, QString> readThemeTexts()
{
    static const QDialogButtonBox::StandardButtons kAll =
        QDialogButtonBox::Ok | QDialogButtonBox::Save | QDialogButtonBox::SaveAll
        | QDialogButtonBox::Open | QDialogButtonBox::Yes | QDialogButtonBox::YesToAll
        | QDialogButtonBox::No | QDialogButtonBox::NoToAll | QDialogButtonBox::Abort
        | QDialogButtonBox::Retry | QDialogButtonBox::Ignore | QDialogButtonBox::Close
        | QDialogButtonBox::Cancel | QDialogButtonBox::Discard | QDialogButtonBox::Help
        | QDialogButtonBox::Apply | QDialogButtonBox::Reset
        | QDialogButtonBox::RestoreDefaults;
    QDialogButtonBox probe(kAll);
    QHash<int, QString> out;
    const QList<QAbstractButton *> buttons = probe.buttons();
    for (QAbstractButton *b : buttons)
        out.insert(int(probe.standardButton(b)), b->text());
    return out;
}

class Relabeler : public QObject
{
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::Show) {
            if (auto *box = qobject_cast<QDialogButtonBox *>(watched)) {
                if (m_themeTexts.isEmpty()) m_themeTexts = readThemeTexts();
                relabel(box, m_themeTexts);
            }
        }
        return QObject::eventFilter(watched, event);
    }

private:
    QHash<int, QString> m_themeTexts;
};

} // namespace

void install(QApplication &app)
{
    app.installEventFilter(new Relabeler(&app));
}

} // namespace standard_buttons
