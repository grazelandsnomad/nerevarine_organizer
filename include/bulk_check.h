#pragma once

// bulk_check - which rows a "turn these on / turn these off" action actually
// has to touch.
//
// Pure, so the rule can be tested without a window: the cost of getting it
// wrong is not a wrong answer but a slow one. Every check-state write fires
// a mod-count refresh and a section recount, each a full pass over the list
// plus a copy of it, so writing to rows that already hold the wanted state
// turns a 400-mod toggle into 400 full passes for nothing. The model itself
// drops a no-op write, but only after the signal has been raised.
//
// Separators are excluded here rather than at the call site: a separator is
// not checkable (ModlistModel::flags strips the flag) and reads back as
// Unchecked, so a selection that happens to include one would otherwise look
// like a row that needs turning on, forever.

#include "modentry.h"

#include <QList>

namespace bulk_check {

// The subset of `candidates` (row indices into `entries`) that are mods and
// are not already `enable`. Out-of-range indices are ignored. Order and
// duplicates are preserved from `candidates`, so the caller can rely on it
// for a stable status message; an empty result means there is nothing to do
// and no undo step worth pushing.
QList<int> rowsToSet(const QList<ModEntry> &entries, const QList<int> &candidates,
                     bool enable);

} // namespace bulk_check
