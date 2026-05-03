#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QIcon>
#include <QLocale>
#include <QLocalServer>
#include <QLocalSocket>
#include <QSettings>
#include <QTimer>
#include <QByteArray>

#include "logging.h"
#include "mainwindow.h"
#include "settings.h"
#include "settings_migrations.h"
#include "translator.h"

// -- QSettings adapter for settings::Store ---
//
// Lives here (rather than in a dedicated .cpp) because it's a three-line
// forward to QSettings and pulling QSettings into the pure migrations
// library would break its "no Qt Widgets / no real I/O" testability.
namespace {

class QSettingsStore final : public settings::Store {
public:
    QVariant value(const QString &k, const QVariant &def = {}) const override
    { return m_s.value(k, def); }
    void setValue(const QString &k, const QVariant &v) override
    { m_s.setValue(k, v); }
    void remove(const QString &k) override { m_s.remove(k); }
    bool contains(const QString &k) const override { return m_s.contains(k); }
    QStringList allKeys() const override { return m_s.allKeys(); }
private:
    mutable QSettings m_s;
};

} // namespace

static const char *SERVER_NAME = "nerevarine_organizer_instance";

// Map ISO 639-1 two-letter codes to our language file names
static QString detectLanguage()
{
    static const QMap<QString, QString> codeToLang = {
        {"ar", "arabic"},
        {"ca", "catalan"},
        {"de", "german"},
        {"el", "greek"},
        {"en", "english"},
        {"es", "spanish"},
        {"fr", "french"},
        {"it", "italian"},
        {"ja", "japanese"},
        {"zh", "chinese_simplified"},
    };
    QString code = QLocale::system().name().left(2).toLower();
    return codeToLang.value(code, "english");
}

int main(int argc, char *argv[])
{
    // Apply saved UI scale factor before QApplication is constructed so that
    // Qt picks up QT_SCALE_FACTOR at startup (it cannot be changed at runtime).
    {
        QString home = QString::fromLocal8Bit(qgetenv("HOME"));
        QSettings preScale(home + "/.config/nerevarine/Nerevarine Organizer.conf",
                           QSettings::IniFormat);
        double scale = preScale.value("ui/scale_factor", 1.0).toDouble();
        if (scale > 0.0 && qAbs(scale - 1.0) > 0.01)
            qputenv("QT_SCALE_FACTOR", QByteArray::number(scale, 'f', 2));
    }

    QApplication app(argc, argv);
    app.setApplicationName("Nerevarine Organizer");
    app.setApplicationVersion("0.1.0");
    app.setOrganizationName("nerevarine");

    // Install logging + crash handlers as early as possible after QApplication.
    logging::initialize(app.applicationVersion());

    app.setWindowIcon(QIcon(":/assets/icons/cystal_full_0.png"));
    app.setDesktopFileName("nerevarine_organizer");

    // Register a bundled emoji font when present so AppImage builds
    // render any remaining colour glyphs even on hosts without NotoColorEmoji.
    // Toolbar symbols (◆ ⊘ ☘ etc.) are BMP characters and need no emoji font.
    {
        const QString appDir = QCoreApplication::applicationDirPath();
        for (const QString &rel : {QStringLiteral("/../share/fonts/NotoColorEmoji.ttf"),
                                   QStringLiteral("/fonts/NotoColorEmoji.ttf")}) {
            const QString p = appDir + rel;
            if (QFile::exists(p)) {
                QFontDatabase::addApplicationFont(p);
                break;
            }
        }
    }

    // Run any pending settings migrations BEFORE the first read of any
    // app-owned QSettings key, so renames/removes applied by a migration
    // take effect on this very launch instead of one launch later.
    {
        QSettingsStore store;
        const auto migResult = settings::applyMigrations(
            store, settings::builtinMigrations(), settings::kCurrentVersion);
        if (migResult.storeVersionBefore != migResult.storeVersionAfter) {
            qCInfo(logging::lcApp,
                   "settings migration: v%d -> v%d (%lld step(s))",
                   migResult.storeVersionBefore,
                   migResult.storeVersionAfter,
                   static_cast<long long>(migResult.appliedDescriptions.size()));
            for (const QString &desc : migResult.appliedDescriptions)
                qCInfo(logging::lcApp) << "  applied:" << desc;
        }
    }

    // Load language - saved preference, or auto-detect from system locale
    QString lang = Settings::uiLanguage().trimmed().toLower();
    if (lang.isEmpty()) {
        lang = detectLanguage();
        // Don't save yet - let the user confirm/change it if they want
    }
    Translator::init(lang);

    // Mirror the UI for right-to-left languages (e.g. Arabic)
    app.setLayoutDirection(Translator::isRtl() ? Qt::RightToLeft : Qt::LeftToRight);

    // Collect any nxm:// URL passed as argument (from protocol handler)
    QString nxmUrl;
    for (int i = 1; i < argc; ++i) {
        QString arg = QString::fromLocal8Bit(argv[i]);
        if (arg.startsWith("nxm://", Qt::CaseInsensitive)) {
            nxmUrl = arg;
            break;
        }
    }

    // Try to connect to an already-running instance
    {
        QLocalSocket socket;
        socket.connectToServer(SERVER_NAME);
        if (socket.waitForConnected(500)) {
            if (!nxmUrl.isEmpty()) {
                socket.write(nxmUrl.toUtf8());
                socket.waitForBytesWritten(2000);
            }
            socket.disconnectFromServer();
            return 0;
        }
    }

    // We are the first instance - become the server
    QLocalServer::removeServer(SERVER_NAME);
    QLocalServer server;
    server.listen(SERVER_NAME);

    MainWindow window;
    window.show();

    QObject::connect(&server, &QLocalServer::newConnection, [&server, &window]() {
        QLocalSocket *conn = server.nextPendingConnection();
        QObject::connect(conn, &QLocalSocket::readyRead, [conn, &window]() {
            QString url = QString::fromUtf8(conn->readAll()).trimmed();
            if (!url.isEmpty())
                window.handleNxmUrl(url);
            conn->deleteLater();
        });
    });

    if (!nxmUrl.isEmpty()) {
        const QString urlCopy = nxmUrl;
        QTimer::singleShot(0, &window, [&window, urlCopy]() {
            window.handleNxmUrl(urlCopy);
        });
    }

    return app.exec();
}
