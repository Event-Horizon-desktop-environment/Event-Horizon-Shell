#pragma once

#include <cairo/cairo.h>

#include <string>
#include <vector>

// Multi-user backend for the Settings Accounts tab. Enumeration is done with
// getpwent(3); all privileged operations go through pkexec with direct
// fork+exec (no shell), so usernames/passwords can never inject commands.

struct AccountsUserEntry {
  std::string username;
  std::string fullName; // gecos before ',', or username
  std::string home;
  unsigned uid = 0;
  bool isCurrent = false;
  bool isAdmin = false;
  bool locked = false;
  bool lockKnown = false;
};

struct PrivResult {
  int status = -1; // process exit code, or -1 on spawn failure
  std::string output;
};

// Run ["pkexec", args...] with stdin_data on stdin, capture stdout+stderr.
// No shell involved; argv entries are passed verbatim.
PrivResult accounts_run_priv(const std::vector<std::string>& pkexec_argv,
                             const std::string& stdin_data = {});

// Call once per paint. Re-enumerates users + autologin state every N paints.
void accounts_poll_refresh();

// Cached enumeration (sorted: current user first, then alphabetical).
const std::vector<AccountsUserEntry>& accounts_users();
void accounts_refresh_users(); // re-enumerate immediately
const AccountsUserEntry* accounts_find(const std::string& username);
std::string accounts_current_username();
int accounts_admin_count();
// "wheel" or "sudo", whichever exists (empty if neither).
std::string accounts_admin_group();

bool accounts_valid_username(const std::string& u); // ^[a-z_][a-z0-9_-]{0,31}$
bool accounts_valid_hostname(const std::string& h);
bool accounts_valid_fullname(const std::string& f); // no ':' / newline, <= 128

// Query lock state ("passwd -S"). Plain call for the current user, pkexec for
// anyone else. Updates the cached entry on success.
bool accounts_query_locked(const std::string& username, bool& lockedOut);

struct AccountsActionResult {
  bool ok = false;
  std::string msg;
};

AccountsActionResult accounts_set_password(const std::string& username, const std::string& pass);
AccountsActionResult accounts_create_user(const std::string& username, const std::string& fullName,
                                          const std::string& pass, bool admin);
AccountsActionResult accounts_delete_user(const std::string& username);
AccountsActionResult accounts_set_admin(const std::string& username, bool admin);
AccountsActionResult accounts_set_locked(const std::string& username, bool locked);
AccountsActionResult accounts_set_fullname(const std::string& username, const std::string& fullName);
AccountsActionResult accounts_set_hostname(const std::string& hostname);

// Avatar surfaces, cached per username. Returns null when no image is
// available (caller draws the initial-letter placeholder). explicitPath is
// tried first (may be empty), then <home>/.face, then AccountsService icons.
cairo_surface_t* accounts_avatar_for(const std::string& username, const std::string& home,
                                     const std::string& explicitPath);
void accounts_invalidate_avatars();
// Install srcPath as username's ~/.face (pkexec install for other users).
AccountsActionResult accounts_install_avatar(const std::string& username, const std::string& home,
                                             const std::string& srcPath, bool isCurrent);

struct AutologinInfo {
  bool supported = false;
  std::string dm; // "GDM", "SDDM", "LightDM"
  bool enabled = false;
  std::string user;
};
AutologinInfo accounts_autologin_info();
AccountsActionResult accounts_set_autologin(const std::string& username, bool enable);
