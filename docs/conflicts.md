# Mod conflicts: the two kinds, and how to fix each

Two mods can collide in two different ways. They look almost the same in the
list and need opposite fixes, so it is worth knowing which one you are looking
at.

## 1. File conflict: both mods ship the same file

Only one copy of a file can exist in the game's data folder, so one mod's copy
wins and the other is never loaded. **The mod further DOWN the list wins.**
That is how OpenMW resolves its VFS, and the Bethesda deploy links files in the
same order with the last writer keeping the file.

In the list:

- green up arrow, `overwrites <mod>` - this mod's copy is the one that loads
- orange down arrow, `overwritten by <mod>` - this mod's copy is dead weight
- a mod in the middle of a stack shows both

Selecting a mod also tints its conflict partners: green for rows that beat it,
orange for rows it beats.

**To change it:** right-click the mod, **File conflicts**, then
*Overwrite "X"* (moves it below X) or *Let "X" overwrite this* (moves it above).
The mod moves next to its counterpart, so the rest of your order is untouched
even if the two are hundreds of rows apart. *Go to "X"* jumps to the other mod.

## 2. Record clash: different files, same content

Two plugins with different filenames never overwrite each other - both install,
both load. They can still rewrite the same records, and then the plugin load
order decides which edit survives. **Reordering the mod list does not fix
this**, which is why no arrow is drawn for it.

In the list: a violet `=` and `same 332 records as <mod>`.

**To change it:** usually you do not reorder, you disable one of the two. The
right-click menu offers *Disable "X"* for exactly this.

Only records that override a master are compared. A plugin's own new records
are renumbered from its position in the load order, so two plugins that both
add content never overwrite each other - they simply both load, as duplicates.

This pass only applies to Oblivion-through-Starfield plugins. Morrowind plugins
are a different format and are skipped.

## Translations

Translations are where both cases show up, and which one you get depends
entirely on how the author named the plugin.

| Translation ships | What happens | What to do |
|---|---|---|
| the **same** plugin filename | one file wins; it is a file conflict | put the translation **below** the original, or just remove the original |
| a **different** plugin filename | both load and fight over records | **remove the original** - reordering will not help |

Worked example, two Starfield mods:

- *Amazing Companion Tweaks* and its Spanish version both ship
  `amazing_companion_tweaks_sp0ckrates.esm`. Same name, so only one file lands.
  The Spanish one has to sit lower to win.
- *Better Crowd Citizens* ships `Better Crowd Citizens.esm` and its Spanish
  version ships `Better Crowd Citizens ES.esm`. Different names, so both
  install and both rewrite the same 332 base-game records. Order changes
  nothing; the English one has to go.

### Does the translation need the original?

Most full translations are a complete copy of the plugin and stand on their
own. Some are patches that need the original present. The difference is the
translation's **master list**: if it names the original plugin as a master, keep
both, with the translation loading after. If it only names the base game's own
files, it is standalone and the original is redundant.

If you remove a plugin something else masters, the list flags the missing
master on the affected mod.
