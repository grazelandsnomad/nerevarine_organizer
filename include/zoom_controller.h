#ifndef ZOOM_CONTROLLER_H
#define ZOOM_CONTROLLER_H

#include <QObject>

class QListWidget;

class ZoomController : public QObject {
    Q_OBJECT
public:
    ZoomController(QListWidget *list, QObject *parent = nullptr);

    // Read [zoom] limits from nerevarine_prefs.ini (creating it if absent)
    // and the last-used zoom from QSettings("ui/zoom_pt"). Does NOT yet apply
    // the font; call applyZoom(current()) once the widget is visible.
    void loadPrefs();

    int  current() const { return m_pt; }
    int  step()    const { return m_step; }

    void zoomIn()  { applyZoom(m_pt + m_step); }
    void zoomOut() { applyZoom(m_pt - m_step); }

    // Clamp to [min, max], persist to QSettings, set the list font and
    // invalidate every item's size hint so the delegate re-measures.
    void applyZoom(int pt);

private:
    QListWidget *m_list = nullptr;
    int m_pt   = 10;
    int m_min  = 7;
    int m_max  = 22;
    int m_step = 1;
};

#endif // ZOOM_CONTROLLER_H
