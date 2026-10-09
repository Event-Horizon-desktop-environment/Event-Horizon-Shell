#pragma once

#include <string>

namespace eh::settings::default_apps::filemanager1 {

// What syncing the override changed on disk.
enum class Transition { kUnchanged, kInstalled, kRemoved };

// Keeps the D-Bus file-manager endpoint in sync with the Default Apps file
// manager choice. Browsers ("Open containing folder") and in-shell reveal
// actions call org.freedesktop.FileManager1.ShowItems, which ignores the
// inode/directory MIME handler entirely: whichever app owns that bus name
// wins. When the chosen manager ships its own FileManager1 provider the user
// override is removed (native handling wins); otherwise a user-level service
// override routes activation to the horizon-filemanager1 proxy, which
// forwards each request to the configured default. An empty id removes a
// previously installed managed override ("Use system default").
Transition sync_filemanager1_override(const std::string& desktop_id);

// Applies a Transition to the running session: freshly installed overrides
// take over the bus name immediately (no logout needed); removals stop our
// proxy so the native/system provider activates on demand.
void apply_filemanager1_transition(Transition t);

}
