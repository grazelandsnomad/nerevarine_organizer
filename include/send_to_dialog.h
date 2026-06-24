#ifndef SEND_TO_DIALOG_H
#define SEND_TO_DIALOG_H

class QListWidget;
class QListWidgetItem;
class QWidget;

namespace send_to_dialog {

// Modal "Send to separator" picker with a live-filter search box; Enter on a
// match accepts. Returns the chosen item (borrowed from `list`), null on cancel.
QListWidgetItem *pickSeparator(QWidget *parent, QListWidget *list);

} // namespace send_to_dialog

#endif // SEND_TO_DIALOG_H
