#ifndef MODLIST_SORT_H
#define MODLIST_SORT_H

#include <QList>

class QListWidget;
class QListWidgetItem;
class QWidget;

namespace modlist_sort {

// Sort by ModRole::DateAdded. Separators move to the end (relative order
// kept); dateless mods sort to the end of the mod section.
void byDate(QListWidget *list, bool ascending);

// Sort mods globally by ModRole::ModSize. Separators stay put as dividers;
// only the mods between them shuffle. Sizeless mods sort to the end either
// way.
void bySize(QListWidget *list, bool ascending);

// Modal "Sort separators" dialog: each section is a draggable row. Returns
// the new item order (orphan section + each separator + its mods). Pointers
// are borrows from `list`; caller must takeItem all and re-insert in the
// returned order. Empty list = cancelled.
QList<QListWidgetItem *> showReorderSeparatorsDialog(QWidget *parent,
                                                     QListWidget *list);

} // namespace modlist_sort

#endif // MODLIST_SORT_H
