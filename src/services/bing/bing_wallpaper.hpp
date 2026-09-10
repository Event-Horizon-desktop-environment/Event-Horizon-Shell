#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <unistd.h>
#include <optional>
#include <string>
#include <vector>

namespace eh::bing {

struct WallpaperEntry {
    std::string date;
    std::string month;
    std::string title;
    std::string url;
    std::string filename;
    std::string local_path;
};

enum class Filter { All, Current, Last, Custom };

struct UpdatesResult {
    bool        ok          = false;
    int         new_count   = 0;
    std::vector<WallpaperEntry> new_entries;
    std::string error_msg;
};

struct ProgressResult {
    int  current = 0;
    int  total   = 0;
    int  found   = 0;
    std::string newest_available_date;

    std::string last_downloaded_filename;
    std::string last_downloaded_date;
};

struct DoneResult {
    bool        ok         = false;
    int         downloaded = 0;
    int         skipped    = 0;
    int         failed     = 0;
    std::string error_msg;
};

struct DailyResult {
    bool        ok         = false;
    std::string local_path;
    std::string error_msg;
};

class BingWallpaperService {
public:
    static BingWallpaperService& instance();

    void init();
    int  wake_fd() const noexcept { return m_wake_fd; }
    void drain_wake();
    void register_redraw(std::function<void()> fn);

    void set_blocklist(const std::vector<std::string>& keywords) { m_blocklist = keywords; }
    const std::vector<std::string>& blocklist() const { return m_blocklist; }

    bool check_updates_async(const std::string& download_dir);
    bool download_async(const std::string& download_dir,
                        Filter filter,
                        const std::string& custom_month = "");
    bool daily_async(const std::string& download_dir, bool use_cache = true);

    bool is_checking_updates()  const noexcept;
    bool is_downloading()       const noexcept;
    bool is_fetching_daily()    const noexcept;

    std::optional<UpdatesResult>  take_updates_result();
    std::optional<ProgressResult> take_progress_result();
    std::optional<DoneResult>     take_done_result();
    std::optional<DailyResult>    take_daily_result();

private:
    ~BingWallpaperService() { if (m_wake_fd >= 0) ::close(m_wake_fd); }
    BingWallpaperService() = default;

    void ping_wake();

    UpdatesResult  do_check_updates(const std::string& dir);
    void           do_download(const std::string& dir,
                               Filter filter,
                               const std::string& custom_month);
    DailyResult    do_daily(const std::string& dir, bool use_cache);

    static std::string fetch_readme();
    static std::vector<WallpaperEntry> parse_wallpapers(const std::string& md,
                                                        const std::string& dir);
    static bool load_cache(const std::string& cache_path,
                           std::vector<WallpaperEntry>& out,
                           const std::string& dir);
    static void save_cache(const std::string& cache_path,
                           const std::vector<WallpaperEntry>& items);
    static std::vector<WallpaperEntry> filter_entries(
        const std::vector<WallpaperEntry>& items, Filter f, const std::string& month);
    static bool download_one(const WallpaperEntry& e);
    static bool matches_blocklist(const WallpaperEntry& e,
                                  const std::vector<std::string>& keywords);

    int  m_wake_fd = -1;
    mutable std::mutex m_wake_mu;
    std::vector<std::function<void()>> m_redraws;

    mutable std::mutex m_state_mu;
    bool m_checking   = false;
    bool m_downloading = false;
    bool m_daily      = false;

    std::vector<std::string> m_blocklist;

    std::mutex m_result_mu;
    std::optional<UpdatesResult>  m_updates_result;
    std::optional<ProgressResult> m_progress_result;
    std::optional<DoneResult>     m_done_result;
    std::optional<DailyResult>    m_daily_result;
};

}
