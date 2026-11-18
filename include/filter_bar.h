#ifndef FILTER_BAR_H
#define FILTER_BAR_H

#include <QObject>

class QAction;
class QLineEdit;
class ModListView;

class FilterBar : public QObject {
    Q_OBJECT
public:
    FilterBar(ModListView *list, QObject *parent = nullptr);

    QLineEdit *widget() const { return m_edit; }

    void focus();
    bool hasText() const;
    void clearText();
    // Text in the box OR the favourites-only star: either one hides rows.
    bool isFiltering() const;
    // Both off, so every row that is not under a collapsed section shows.
    void clearFilter();

private:
    void apply();

    ModListView *m_list = nullptr;
    QLineEdit   *m_edit = nullptr;
    QAction     *m_favOnlyAction = nullptr;
};

#endif // FILTER_BAR_H
