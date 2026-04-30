#ifndef MODLIST_SORT_H
#define MODLIST_SORT_H

#include <QList>

class QListWidget;
class QListWidgetItem;
class QWidget;

namespace modlist_sort {

// Sort the list by ModRole::DateAdded. Separators all migrate to the end
// (preserving their relative order); mods without a date sort to the end
// of the mod section.
void byDate(QListWidget *list, bool ascending);

// Sort mods by ModRole::ModSize globally. Separators stay at their
// current row positions as dividers - only the mods between them get
// reshuffled. Mods without a known size sort to the end in either
// direction.
void bySize(QListWidget *list, bool ascending);

// Modal "Sort separators" dialog: shows each section as a draggable row
// and returns the new order of QListWidgetItems (orphan section + each
// separator + its mods). Returned pointers are borrows from `list` -
// caller is expected to detach all items via takeItem and re-insert in
// the returned order. Empty list means user cancelled.
QList<QListWidgetItem *> showReorderSeparatorsDialog(QWidget *parent,
                                                     QListWidget *list);

} // namespace modlist_sort

#endif // MODLIST_SORT_H
