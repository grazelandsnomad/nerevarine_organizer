#ifndef ZOOM_CONTROLLER_H
#define ZOOM_CONTROLLER_H

#include <QObject>

class QListWidget;

class ZoomController : public QObject {
    Q_OBJECT
public:
    ZoomController(QListWidget *list, QObject *parent = nullptr);

    // Reads [zoom] limits from nerevarine_prefs.ini (creates it if absent)
    // and last-used zoom from QSettings("ui/zoom_pt"). Doesn't apply the font
    // yet; call applyZoom(current()) once the widget is visible.
    void loadPrefs();

    int  current() const { return m_pt; }
    int  step()    const { return m_step; }

    void zoomIn()  { applyZoom(m_pt + m_step); }
    void zoomOut() { applyZoom(m_pt - m_step); }

    // Clamp to [min,max], persist, set the list font, and invalidate every
    // item's size hint so the delegate re-measures.
    void applyZoom(int pt);

private:
    QListWidget *m_list = nullptr;
    int m_pt   = 10;
    int m_min  = 7;
    int m_max  = 22;
    int m_step = 1;
};

#endif // ZOOM_CONTROLLER_H
