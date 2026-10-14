#pragma once

// user_state_paths - where a per-user state file lives: the modlist and load
// order, the forbidden-mods list, deploy manifests, the download diagnostics
// log.
//
// Next to the binary for a dev or plain-zip build, so a checkout keeps its
// state with it; AppDataLocation under an AppImage, whose applicationDirPath
// is a read-only squashfs mount, and for a /opt-style install whose binary
// dir is not writable. An existing file wins, so an in-place upgrade keeps
// the user's state.
//
// Qt Core only. The window and GameProfileRegistry both resolve state files,
// and each used to carry its own copy of this rule; that the two "must
// resolve to the same path" was a comment, not a guarantee.

#include <QString>

QString resolveUserStatePath(const QString &filename);
