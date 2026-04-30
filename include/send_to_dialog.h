#ifndef SEND_TO_DIALOG_H
#define SEND_TO_DIALOG_H

class QListWidget;
class QListWidgetItem;
class QWidget;

namespace send_to_dialog {

// Modal "Send to separator" picker. Lists every separator in `list` with a
// live-filter search box; pressing Enter on a match accepts. Returns the
// picked separator (a borrow from `list`), or nullptr on cancel.
QListWidgetItem *pickSeparator(QWidget *parent, QListWidget *list);

} // namespace send_to_dialog

#endif // SEND_TO_DIALOG_H
