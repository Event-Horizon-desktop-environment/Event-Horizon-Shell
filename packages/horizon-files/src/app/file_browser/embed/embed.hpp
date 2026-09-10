#pragma once

#include <string>

namespace eh::file_browser {

/// Run the file browser as a standalone xdg-toplevel window.
/// Creates its own Wayland connection and event loop.
/// Blocks until the window is closed.
/// Returns 0 on success, 1 on error.
[[nodiscard]] int run_standalone();

/// Run the file browser in directory-picker mode.
/// Creates its own Wayland connection and event loop.
/// Blocks until the user selects a directory or cancels.
/// On selection, out_path is set to the chosen directory and returns 0.
/// On cancel, out_path is unchanged and returns 1.
[[nodiscard]] int run_select_directory(std::string& out_path);

/// Run the file browser in file-picker mode.
/// Creates its own Wayland connection and event loop.
/// Blocks until the user selects a file or cancels.
/// On selection, out_path is set to the chosen file path and returns 0.
/// On cancel, out_path is unchanged and returns 1.
[[nodiscard]] int run_select_file(std::string& out_path);

} // namespace eh::file_browser
