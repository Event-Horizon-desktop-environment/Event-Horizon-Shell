#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace eh::shell::desktop::archive {

[[nodiscard]] bool path_looks_like_archive(const std::filesystem::path& p);

[[nodiscard]] std::vector<std::string> filter_archive_paths(const std::vector<std::string>& abs_paths);

enum class CompressFormat : std::uint8_t { Zip, TarGz, Tar, SevenZ };

void extract_archives_detached(const std::vector<std::string>& archive_abs_paths);

void compress_paths_detached(CompressFormat fmt, const std::vector<std::string>& abs_paths,
                             const std::string& dest_parent_dir);

}
