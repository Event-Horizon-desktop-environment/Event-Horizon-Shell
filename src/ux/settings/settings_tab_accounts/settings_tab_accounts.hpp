#pragma once
#include <cairo/cairo.h>

#include <cstdint>

struct App;

enum class AccountsField : int {
  None = 0,
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
