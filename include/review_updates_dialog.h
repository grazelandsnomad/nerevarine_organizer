#ifndef REVIEW_UPDATES_DIALOG_H
#define REVIEW_UPDATES_DIALOG_H

#include <QList>
#include <QString>

class QListWidgetItem;
class QWidget;

namespace ReviewUpdates {

struct Candidate {
    QListWidgetItem *item;
    QString          name;
    QString          game;
    int              modId;
    QString          url;
};

// Modal "review updates" dialog. Shows a checkable list of candidates with
// Cancel / Update Selected / Update All buttons. The "Update Selected"
// label updates live as the user toggles checkboxes.
//
// Returns the candidates the user picked (subset of input, in input order).
// Empty if cancelled. Caller is expected to run prepareItemForInstall +
// fetchModFiles on each picked entry.
QList<Candidate> showDialog(QWidget *parent, const QList<Candidate> &candidates);

} // namespace ReviewUpdates

#endif // REVIEW_UPDATES_DIALOG_H
