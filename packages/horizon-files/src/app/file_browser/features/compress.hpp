#pragma once

#include <string>
#include <vector>

namespace eh::file_browser {

struct AppState;

struct CompressFormat {
  std::string label;
  std::string extension;
};

constexpr int kNumCompressFormats = 7;
extern const CompressFormat kCompressFormats[kNumCompressFormats];

std::string format_compress_cmd(const std::vector<std::string>& source_paths,
                                 const std::string& archive_path,
                                 int format_idx, int level);

bool is_archive_extension(const std::string& path);
std::string format_extract_cmd(const std::string& archive_path,
                                const std::string& dest_dir);
std::string default_extract_dir(const std::string& archive_path);

void check_compress_tool_availability(AppState& app);
void execute_compress_async(AppState& app);
void execute_extract_async(AppState& app, const std::string& archive_path,
                            const std::string& dest_dir);

} // namespace eh::file_browser
