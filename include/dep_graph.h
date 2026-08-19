#ifndef DEP_GRAPH_H
#define DEP_GRAPH_H

// The dependency canvas: what the user has declared, drawn as a graph.
//
// Dependencies are already stored and already editable, but only ever visible
// one mod at a time - double-clicking a row lists "Required mods" and "Required
// by", and the list tints a selected mod's neighbours. Nothing showed the
// SHAPE. KID needs MergeMapper, which needs SKSE and Address Library, which
// itself needs SKSE: a three-level chain that previously took four dialogs to
// reconstruct.
//
// -- Why it hands the edits back instead of applying them -------------
//
// Same contract as conflict_inspector: the caller builds a POD snapshot on the
// UI thread, this draws it, and any edits come back as data. Nothing here
// touches MainWindow, m_modList, the undo stack or the save path - the caller
// applies the result through the same route the "Required mods" dialog already
// uses, so there is exactly one place that writes DependsOn.
//
// -- Why every mod is a node but not every mod is on screen -----------
//
// An editable canvas has to let the user attach a mod that has no edges yet, so
// the graph carries all of them. But of 382 mods on the author's Morrowind list
// only 30 are in a dependency, and drawing 382 boxes would be useless. So the
// whole-list view opens with just the ones that have edges, and a section
// selector - the separators the user already maintains - narrows it to one
// part of the list, where every mod of that section is drawn whether it has a
// dependency yet or not.
//
// A section is never shown alone, though. Measured on the author's lists, 10 of
// 19 dependency arrows on Skyrim AE and 37 of 38 on Morrowind cross a separator
// boundary, because mods live in their own sections and the frameworks they
// need live in another. So whatever a section's arrows reach is pulled in from
// its own section and drawn faded: the chain stays whole, and it is still
// obvious which boxes the selection actually asked for.

#include <QHash>
#include <QString>
#include <QStringList>

#include "deps_resolver.h"

class QWidget;

namespace dep_graph {

struct Result {
    // True when the user changed at least one dependency, so the caller knows
    // whether to push an undo step and save at all.
    bool changed = false;
    // Row index -> its complete new DependsOn list. Only rows that changed
    // appear, so the caller writes as little as possible.
    QHash<int, QStringList> dependsOn;
};

// Show the canvas modally over `parent`.
//
// `rowUrl` maps a row index to its Nexus URL - the graph works in node indices,
// but a dependency is stored as a URL, so the caller supplies the translation
// rather than this guessing at it.
//
// `layoutKey` identifies the modlist profile whose node positions to load and
// save; empty disables persistence (positions are laid out fresh each time).
Result show(QWidget *parent,
            const deps::Graph &graph,
            const QHash<int, QString> &rowUrl,
            const QString &layoutKey);

} // namespace dep_graph

#endif // DEP_GRAPH_H
