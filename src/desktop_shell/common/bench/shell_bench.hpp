#pragma once

#include <chrono>

using ShellBenchClock = std::chrono::steady_clock;

bool eh_dock_bench();
bool eh_cc_open_bench();
bool eh_menu_bench();

double shell_bench_ms_since(ShellBenchClock::time_point t0);
double shell_bench_ms_between(ShellBenchClock::time_point a, ShellBenchClock::time_point b);

void shell_bench_set_init_t0(ShellBenchClock::time_point t);
bool shell_bench_have_init_t0();
ShellBenchClock::time_point shell_bench_init_t0();

bool shell_bench_should_log_layer_create_detail();
void shell_bench_mark_layer_create_detail_logged();

bool shell_bench_should_log_first_draw_detail();
void shell_bench_mark_first_draw_detail_logged();
