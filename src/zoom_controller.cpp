#include "zoom_controller.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QListWidget>
#include <QListWidgetItem>
#include <QSettings>
#include <QSize>
#include <QStandardPaths>
#include <QTextStream>
#include <Qt>

ZoomController::ZoomController(QListWidget *list, QObject *parent)
    : QObject(parent), m_list(list)
{
}

void ZoomController::loadPrefs()
{
    // Search for prefs file: AppDataLocation first (AppImage / new installs),
    // then legacy binary-adjacent paths (dev / .tar.gz release).
    const bool inAppImage = !qEnvironmentVariableIsEmpty("APPIMAGE");
    const QString dataPath = QStandardPaths::writableLocation(
                                 QStandardPaths::AppDataLocation)
                             + "/nerevarine_prefs.ini";
    QStringList candidates;
    if (inAppImage) {
        candidates << dataPath;
    } else {
        candidates << QCoreApplication::applicationDirPath() + "/nerevarine_prefs.ini"
                   << QCoreApplication::applicationDirPath() + "/../nerevarine_prefs.ini"
                   << dataPath;
    }

    QString prefsPath;
    for (const QString &p : candidates) {
        if (QFile::exists(p)) { prefsPath = p; break; }
    }

    if (prefsPath.isEmpty()) {
        prefsPath = inAppImage ? dataPath : candidates.first();
        QDir().mkpath(QFileInfo(prefsPath).absolutePath());
        QFile f(prefsPath);
        if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&f);
            out << "; Nerevarine Organizer preferences\n"
                << "; Edit this file to customise the application.\n"
                << "; Restart the app after making changes.\n\n"
                << "[zoom]\n"
                << "; Font size (in points) for the mod list.\n"
                << "; Use Ctrl+Scroll on the list to zoom in or out.\n"
                << "min_pt=7\n"
                << "max_pt=22\n"
                << "step_pt=1\n"
                << "default_pt=10\n";
        }
    }

    QSettings prefs(prefsPath, QSettings::IniFormat);
    m_min  = prefs.value("zoom/min_pt",     7).toInt();
    m_max  = prefs.value("zoom/max_pt",    22).toInt();
    m_step = prefs.value("zoom/step_pt",    1).toInt();
    int defPt = prefs.value("zoom/default_pt", 10).toInt();

    m_min  = qMax(4,  m_min);
    m_max  = qMin(72, m_max);
    m_step = qMax(1,  m_step);
    defPt  = qBound(m_min, defPt, m_max);

    m_pt = qBound(m_min, m_max,
                  QSettings().value("ui/zoom_pt", defPt).toInt());
    m_pt = qBound(m_min, m_pt, m_max);
}

void ZoomController::applyZoom(int pt)
{
    pt = qBound(m_min, pt, m_max);
    if (pt == m_pt && m_list) {
        if (m_list->font().pointSize() == pt) return;
    }
    m_pt = pt;
    QSettings().setValue("ui/zoom_pt", m_pt);

    QFont f = m_list->font();
    f.setPointSize(m_pt);
    m_list->setFont(f);
    for (int i = 0; i < m_list->count(); ++i)
        m_list->item(i)->setSizeHint(QSize());
    m_list->update();
}
