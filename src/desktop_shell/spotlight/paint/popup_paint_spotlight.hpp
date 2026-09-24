#pragma once

#include <cairo/cairo.h>

namespace eh::config { struct ShellConfig; }

struct TaskbarApp;

// Taskbar entry point for the spotlight search palette. The paint body is a
// template shared with the dock's DockApp palette (see popup_paint_spotlight.cpp);
// this is the TaskbarApp instantiation, kept out of the taskbar TU so neither
// side has to drag the other's app struct in.
namespace eh::shell::taskbar {

void taskbar_popup_paint_spotlight(TaskbarApp& app, cairo_t* cr, const eh::config::ShellConfig& scPopupOv);

}
