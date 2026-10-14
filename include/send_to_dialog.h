#ifndef SEND_TO_DIALOG_H
#define SEND_TO_DIALOG_H

#include "modrow.h"

class ModListView;
class QWidget;

namespace send_to_dialog {

// Modal "Send to separator" picker with a live-filter search box; Enter on a
// match accepts. Returns the chosen separator row of `list`, null on cancel.
ModRow pickSeparator(QWidget *parent, ModListView *list);

} // namespace send_to_dialog

#endif // SEND_TO_DIALOG_H
