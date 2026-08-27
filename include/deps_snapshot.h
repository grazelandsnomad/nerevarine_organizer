#pragma once

// deps_snapshot - the one place that turns modlist rows into the POD
// deps_resolver works on.
//
// Kept out of deps_resolver.h on purpose: that header is pure and its test
// target links QtCore only, which is what makes the dependency logic testable
// without a widget in sight. This is the widget-side adapter.

#include <QList>

#include "deps_resolver.h"

class QListWidget;

namespace deps {

// Every row, in order, with `idx` equal to the row number.
//
// Separators come back as blank entries rather than being skipped, so idx and
// row index never drift apart - resolveDependencies and findNewlyBroken both
// address rows by idx, and a caller that filtered here would have to map back.
// A blank entry has no dependsOn and no nexusUrl, so it is inert to every
// question the resolver asks.
//
// displayName is CustomName when set, else the row's text, matching what
// ModEntry::displayName is documented to hold.
QList<ModEntry> snapshot(const QListWidget *list);

} // namespace deps
