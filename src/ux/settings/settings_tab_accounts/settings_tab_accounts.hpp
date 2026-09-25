#pragma once
#include <cairo/cairo.h>

#include <cstdint>

struct App;

enum class AccountsField : int {
  None = 0,
  UserFullName, // users card comes before the password card visually
  PwNew,
  PwConfirm,
  CuUsername,
  CuFullName,
  CuPassword,
  CuConfirm,
  Hostname,
};

void paint_accounts_tab(App& app, cairo_t* cr, int contentX, int contentW, double glassOv);
bool settings_accounts_consume_pointer_down(App& app, int contentX, int contentW);

// Document-space Y of the bottom of all Accounts content (all cards). Used for
// scroll clamping so every card stays reachable without overscrolling to void.
int accounts_content_bottom_px(int contentX, int contentW);

// Form actions shared by the pointer and keyboard handlers (Enter submits).
void accounts_action_change_password(App& app);
void accounts_action_create_user(App& app);
void accounts_action_set_hostname(App& app);
void accounts_action_set_fullname(App& app);
