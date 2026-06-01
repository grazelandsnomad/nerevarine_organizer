#pragma once

// theme - global light/dark appearance for the whole application.
//
// The app historically had no global QStyle/QPalette; every widget relied on
// the platform default (a light theme) plus a handful of hardcoded per-widget
// stylesheets.  Dark mode is implemented as an application-wide palette swap
// rather than a giant stylesheet so it covers every standard widget (lists,
// menus, dialogs, inputs) without touching each call site:
//
//   · dark  -> Fusion style + a hand-built dark QPalette
//   · light -> the platform's original style + palette, captured once at
//              startup so "Light mode" restores the exact native look
//
// applyTheme() is idempotent and safe to call live (toolbar toggle) or once at
// startup.  Call captureDefault() exactly once, right after QApplication is
// constructed and before the first applyTheme(), so the light branch has the
// native baseline to restore.
//
// A few toolbar widgets still set explicit colors in their own stylesheets;
// those are made palette-driven (palette(text)/palette(window)) so they follow
// the active theme instead of fighting it.

namespace theme {

// Remember the platform's default style name + palette as the "light" baseline.
// Call once after QApplication construction, before applyTheme().
void captureDefault();

// Apply dark (Fusion + dark palette) or light (restore captured baseline) to
// the whole application.  Does not persist - the caller owns Settings.
void applyTheme(bool dark);

} // namespace theme
