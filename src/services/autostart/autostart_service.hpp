#pragma once

#include "desktop_shell/desktop/entries/desktop_entry_types.hpp"

#include <string>
#include <vector>

namespace eh::autostart {

struct AutostartEntry {
  std::string desktopPath;          // absolute path to the .desktop file
  DesktopEntryInfo info;            // parsed fields (now includes autostart keys)
  bool effectiveEnabled = true;     // after applying Hidden / X-GNOME-Autostart-enabled / shadowing
  int delaySec = 0;
};

// Scan XDG autostart directories (user overrides + system).
// Follows basic XDG precedence (later user files shadow earlier system ones by basename).
// Returns entries that are candidates (filtering happens at launch time too).
[[nodiscard]] std::vector<AutostartEntry> scan_autostart_entries();

// Returns true if this entry should be shown/considered for the current desktop environment.
// Checks OnlyShowIn / NotShowIn against XDG_CURRENT_DESKTOP (and "EventHorizon" as a fallback).
[[nodiscard]] bool should_show_in_current_desktop(const AutostartEntry& e);

// Returns true if TryExec (if present) resolves to an executable.
[[nodiscard]] bool try_exec_ok(const AutostartEntry& e);

// Launch a single autostart entry (fire-and-forget, detached).
// Prefers the system launcher when available for proper .desktop semantics,
// otherwise falls back to expanded Exec line.
void launch_autostart_entry(const AutostartEntry& e);

// Launch all provided entries that pass the filters (current DE, TryExec, enabled).
// Respects per-entry delay (best-effort, non-blocking).
// Call this once during full shell session startup.
void launch_all_autostart(const std::vector<AutostartEntry>& entries);

// For testing / IPC: force re-scan (invalidates any internal cache if added later).
void invalidate_autostart_cache();

// UI / Settings support.

struct AutostartUiEntry {
  std::string stem;             // basename without .desktop, used as key
  std::string name;
  std::string icon;             // icon name from .desktop
  std::string desktopPath;      // path to the file that would be used (user or system)
  bool enabled = true;
  bool isUserOverride = false;  // lives in ~/.config/autostart
  int delaySec = 0;
  bool hidden = false;
};

// Returns a list suitable for the settings UI. Re-scans on each call (cheap enough).
[[nodiscard]] std::vector<AutostartUiEntry> get_autostart_ui_entries();

// Set the enabled state for a given entry stem.
// This writes (or updates) a user override .desktop in ~/.config/autostart/<stem>.desktop
// with X-GNOME-Autostart-enabled=... and Hidden=... as appropriate.
// Returns true on success.
bool set_autostart_enabled(const std::string& stem, bool enabled);

// Completely remove any user override for the stem (so system default applies again).
// Returns true if a file was removed or nothing to do.
bool remove_autostart_override(const std::string& stem);

// Create a brand-new autostart entry from scratch (not based on an existing .desktop).
// Writes a fresh .desktop file to ~/.config/autostart/<stem>.desktop.
// Returns true on success.
bool create_autostart_entry(const std::string& stem, const std::string& name,
                            const std::string& exec, const std::string& icon,
                            int delaySec);

// Update an existing user-override autostart entry's metadata (name, exec, icon, delay).
// Only affects files already in ~/.config/autostart/. Returns true on success.
bool edit_autostart_entry(const std::string& stem, const std::string& name,
                          const std::string& exec, const std::string& icon,
                          int delaySec);

struct InstalledApp {
  std::string name;
  std::string icon;
  std::string exec;
  std::string desktopPath;
};

// Scan system application directories for .desktop files (e.g. /usr/share/applications/).
// Returns a sorted list of installed apps suitable for an app browser.
[[nodiscard]] std::vector<InstalledApp> scan_installed_apps();

} // namespace eh::autostart
