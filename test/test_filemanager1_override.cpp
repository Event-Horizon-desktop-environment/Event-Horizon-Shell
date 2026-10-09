// Tests for the Default Apps FileManager1 override manager.
// Uses throwaway XDG dirs so the real session is never touched.

#include "ux/settings/data/default_apps/settings_default_apps_filemanager1.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include <sys/stat.h>
#include <unistd.h>

namespace fs = std::filesystem;

#define EH_CHECK(expr)                                                                                                 \
  do {                                                                                                                 \
    if (!(expr)) {                                                                                                     \
      std::fprintf(stderr, "EH_CHECK failed: %s (%s:%d)\n", #expr, __FILE__, __LINE__);                                \
      std::abort();                                                                                                    \
    }                                                                                                                  \
  } while (0)

namespace {

std::string read_file(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return {};
  return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

void write_file(const std::string& path, const std::string& content) {
  fs::create_directories(fs::path(path).parent_path());
  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  f << content;
}

}  // namespace

int main() {
  using eh::settings::default_apps::filemanager1::apply_filemanager1_transition;
  using eh::settings::default_apps::filemanager1::sync_filemanager1_override;
  using eh::settings::default_apps::filemanager1::Transition;

  char base_tmpl[] = "/tmp/eh-fm1-test-XXXXXX";
  EH_CHECK(::mkdtemp(base_tmpl) != nullptr);
  const std::string base = base_tmpl;
  const std::string user = base + "/user";
  const std::string sys = base + "/sys";

  setenv("HOME", base.c_str(), 1);
  setenv("XDG_DATA_HOME", user.c_str(), 1);
  setenv("XDG_DATA_DIRS", sys.c_str(), 1);

  // Fake installed apps: horizon-files has no bus provider, otter-files does.
  write_file(sys + "/applications/horizon-files.desktop",
             "[Desktop Entry]\nName=Horizon-Files\nExec=horizon-files %F\n");
  write_file(sys + "/applications/otter-files.desktop",
             "[Desktop Entry]\nName=Otter Files\nExec=/usr/bin/otter-files --new-window %U\n");
  write_file(sys + "/dbus-1/services/otter-files.FileManager1.service",
             "[D-BUS Service]\nName=org.freedesktop.FileManager1\n"
             "Exec=/usr/bin/otter-files --gapplication-service\n");

  // The proxy resolver looks next to this test binary; point it at a fake via
  // the test hook instead of clobbering the real build artifact.
  const std::string fakeProxy = base + "/bin/horizon-filemanager1";
  write_file(fakeProxy, "#!/bin/sh\nexit 0\n");
  ::chmod(fakeProxy.c_str(), 0755);
  setenv("EH_FILEMANAGER1_PROXY_BIN", fakeProxy.c_str(), 1);

  const std::string overridePath = user + "/dbus-1/services/org.freedesktop.FileManager1.service";

  // 1. Non-native choice installs a managed override pointing at the proxy.
  EH_CHECK(sync_filemanager1_override("horizon-files.desktop") == Transition::kInstalled);
  {
    const std::string body = read_file(overridePath);
    EH_CHECK(!body.empty());
    EH_CHECK(body.find("Event Horizon managed") != std::string::npos);
    EH_CHECK(body.find("Name=org.freedesktop.FileManager1") != std::string::npos);
    EH_CHECK(body.find(fakeProxy) != std::string::npos);
  }

  // 2. Idempotent: second sync leaves the file untouched.
  {
    const std::string before = read_file(overridePath);
    EH_CHECK(sync_filemanager1_override("horizon-files.desktop") == Transition::kUnchanged);
    EH_CHECK(read_file(overridePath) == before);
  }

  // 3. Native provider choice removes the managed override.
  EH_CHECK(sync_filemanager1_override("otter-files.desktop") == Transition::kRemoved);
  EH_CHECK(!fs::exists(overridePath));

  // 4. Empty choice ("Use system default") removes a managed override.
  EH_CHECK(sync_filemanager1_override("horizon-files.desktop") == Transition::kInstalled);
  EH_CHECK(fs::exists(overridePath));
  EH_CHECK(sync_filemanager1_override({}) == Transition::kRemoved);
  EH_CHECK(!fs::exists(overridePath));

  // 5. A foreign (unmanaged) override is never touched.
  write_file(overridePath, "[D-BUS Service]\nName=org.freedesktop.FileManager1\nExec=/usr/bin/other-fm\n");
  EH_CHECK(sync_filemanager1_override({}) == Transition::kUnchanged);
  EH_CHECK(read_file(overridePath).find("/usr/bin/other-fm") != std::string::npos);
  EH_CHECK(sync_filemanager1_override("otter-files.desktop") == Transition::kUnchanged);
  EH_CHECK(read_file(overridePath).find("/usr/bin/other-fm") != std::string::npos);

  // 6. Basename normalization: stored ids may carry paths or miss the suffix.
  EH_CHECK(sync_filemanager1_override("/usr/share/applications/horizon-files") == Transition::kInstalled);
  EH_CHECK(read_file(overridePath).find(fakeProxy) != std::string::npos);

  std::error_code ec;
  fs::remove_all(base, ec);
  std::printf("filemanager1_override: all checks passed\n");
  return 0;
}
