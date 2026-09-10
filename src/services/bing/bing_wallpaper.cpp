#include "services/bing/bing_wallpaper.hpp"

#include "desktop_shell/common/time/mono_time.hpp"

#include <nlohmann/json.hpp>

#include <curl/curl.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include "bootstrap/thread/thread_pool.hpp"
#include <string>
#include <unordered_set>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr const char* kReadmeUrl =
    "https://raw.githubusercontent.com/v5tech/bing-wallpaper/refs/heads/main/README.md";
constexpr const char* kUserAgent = "EventHorizon/1.0 BingWallpaper";

std::string xdg_cache_home() {
     
    if (const char* v = ::getenv("XDG_CACHE_HOME")) return v;
    if (const char* h = ::getenv("HOME")) return std::string(h) + "/.cache";
    return "/tmp";
}

std::string default_download_dir() {
     
    if (const char* h = ::getenv("HOME")) return std::string(h) + "/Pictures/BingWallpaper";
    return "/tmp/BingWallpaper";
}

std::string cache_file_path() {
     
    return xdg_cache_home() + "/event-horizon/bingwall/bing_wallpapers.json";
}

size_t curl_write_cb(char* ptr, size_t size, size_t nmemb, void* userdata) {
     
    auto* out = static_cast<std::string*>(userdata);
    out->append(ptr, size * nmemb);
    return size * nmemb;
}

bool curl_fetch_text(const std::string& url, std::string& out,
                     long timeout_sec = 20, long connect_timeout_sec = 10,
                     long* out_http = nullptr) {
     
    static std::once_flag s_init;
    std::call_once(s_init, [] { (void)curl_global_init(CURL_GLOBAL_DEFAULT); });

    if (out_http) *out_http = 0;
    out.clear();
    char errbuf[CURL_ERROR_SIZE] = {};

    CURL* curl = curl_easy_init();
    if (!curl) return false;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &out);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout_sec);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, connect_timeout_sec);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, kUserAgent);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf);

    const CURLcode res = curl_easy_perform(curl);
    long http = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http);
    curl_easy_cleanup(curl);
    if (out_http) *out_http = http;

    const bool ok = res == CURLE_OK && (http == 0 || (http >= 200 && http < 300)) && !out.empty();
    if (!ok) {
        std::cerr << "[bing-wallpaper][fetch] FAIL url=" << url
                  << " http=" << http << " curl=" << res
                  << " err=" << errbuf << "\n";
    }
    return ok;
}

bool curl_download_file(const std::string& url, const std::string& dest_path) {
     
    static std::once_flag s_init;
    std::call_once(s_init, [] { (void)curl_global_init(CURL_GLOBAL_DEFAULT); });

    FILE* f = ::fopen(dest_path.c_str(), "wb");
    if (!f) {
        std::cerr << "[bing-wallpaper][dl] cannot open " << dest_path
                  << " errno=" << errno << "\n";
        return false;
    }

    char errbuf[CURL_ERROR_SIZE] = {};
    CURL* curl = curl_easy_init();
    if (!curl) { ::fclose(f); return false; }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, nullptr);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, f);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, kUserAgent);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf);

    const CURLcode res = curl_easy_perform(curl);
    long http = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http);
    curl_easy_cleanup(curl);
    ::fclose(f);

    const bool ok = res == CURLE_OK && (http == 0 || (http >= 200 && http < 300));
    if (!ok) {
        std::cerr << "[bing-wallpaper][dl] FAIL url=" << url
                  << " http=" << http << " curl=" << res << " err=" << errbuf << "\n";
        ::remove(dest_path.c_str());
    }
    return ok;
}

std::string sanitise_filename(const std::string& s) {
     
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-') {
            out += static_cast<char>(c);
        } else {
            out += '_';
        }
    }
    return out;
}

std::optional<eh::bing::WallpaperEntry> make_entry(const std::string& url,
                                                    const std::string& date_str,
                                                    const std::string& dir) {
     
    std::string id_part;
    const auto id_pos = url.find("?id=");
    if (id_pos != std::string::npos) {
        id_part = url.substr(id_pos + 4);

        const auto amp = id_part.find('&');
        if (amp != std::string::npos) id_part.erase(amp);
        const auto rp = id_part.find(')');
        if (rp != std::string::npos) id_part.erase(rp);
    }
    if (id_part.empty()) {

        const auto sl = url.rfind('/');
        id_part = (sl != std::string::npos) ? url.substr(sl + 1) : url;
        const auto q = id_part.find('?');
        if (q != std::string::npos) id_part.erase(q);
    }
    if (id_part.size() < 8) return std::nullopt;

    std::string filename = sanitise_filename(id_part);
    if (filename.size() < 8 || filename.substr(filename.size() - 8) != "_UHD.jpg")
        filename += "_UHD.jpg";

    const std::string month = (date_str.size() >= 7 && date_str != "unknown")
                              ? date_str.substr(0, 7) : "unknown";

    eh::bing::WallpaperEntry e;
    e.date       = date_str;
    e.month      = month;
    e.url        = url;
    e.filename   = filename;
    e.local_path = dir + "/" + filename;
    return e;
}

std::vector<eh::bing::WallpaperEntry> parse_wallpapers_impl(const std::string& md,
                                                             const std::string& dir) {
     
    std::vector<eh::bing::WallpaperEntry> items;
    std::unordered_set<std::string>       seen_urls;

    // Fast manual scan — replaces std::regex which is ~100x slower on 300KB input
    // Pattern 1: "[download 4k](URL)"      — the standard README format
    static constexpr std::string_view kPattern = "[download 4k](";

    size_t pos = 0;
    while (true) {
        const size_t start = md.find(kPattern, pos);
        if (start == std::string::npos) break;

        const size_t url_start = start + kPattern.size();
        const size_t url_end   = md.find(')', url_start);
        if (url_end == std::string::npos) break;

        const std::string_view url(md.data() + url_start, url_end - url_start);

        // Extract date and title from the same line (before the pattern)
        std::string date = "unknown";
        std::string title;
        if (start > 0) {
            size_t line_start = start;
            while (line_start > 0 && md[line_start - 1] != '\n') --line_start;
            // Extract title from ![alt text](
            static constexpr std::string_view kImgPattern = "![";
            const size_t img_start = md.rfind(kImgPattern, start);
            if (img_start != std::string::npos && img_start >= line_start) {
                const size_t title_end = md.find(']', img_start + 2);
                if (title_end != std::string::npos && title_end < start)
                    title = md.substr(img_start + 2, title_end - img_start - 2);
            }
            for (size_t i = line_start; i + 10 < start; ++i)
                if (md[i + 4] == '-' && md[i + 7] == '-' &&
                    md[i] >= '0' && md[i] <= '9' &&
                    md[i + 1] >= '0' && md[i + 1] <= '9' &&
                    md[i + 2] >= '0' && md[i + 2] <= '9' &&
                    md[i + 3] >= '0' && md[i + 3] <= '9' &&
                    md[i + 5] >= '0' && md[i + 5] <= '9' &&
                    md[i + 6] >= '0' && md[i + 6] <= '9' &&
                    md[i + 8] >= '0' && md[i + 8] <= '9' &&
                    md[i + 9] >= '0' && md[i + 9] <= '9') {
                    date = md.substr(i, 10);
                    break;
                }
        }

        if (!seen_urls.count(std::string(url))) {
            seen_urls.insert(std::string(url));
            if (auto e = make_entry(std::string(url), date, dir)) {
                e->title = title;
                items.push_back(std::move(*e));
            }
        }

        pos = url_end + 1;
    }

    // Pattern 2: loose cn.bing.com URLs without the [download 4k] marker
    static constexpr std::string_view kCnPrefix = "https://cn.bing.com/th?id=";
    pos = 0;
    while (true) {
        const size_t start = md.find(kCnPrefix, pos);
        if (start == std::string::npos) break;

        size_t ue = start + kCnPrefix.size();
        while (ue < md.size() && md[ue] != ')' && md[ue] != ' ' &&
               md[ue] != '\n' && md[ue] != '\r' && md[ue] != '"' &&
               md[ue] != '\'') {
            ++ue;
        }
        const std::string_view url(md.data() + start, ue - start);

        if (!seen_urls.count(std::string(url)) && url.size() >= 8 &&
            url.substr(url.size() - 8) == "_UHD.jpg") {

            std::string date = "unknown";
            const size_t ctx_begin = start > 200 ? start - 200 : 0;
            for (size_t i = ctx_begin; i + 10 < start; ++i)
                if (md[i + 4] == '-' && md[i + 7] == '-' &&
                    md[i] >= '0' && md[i] <= '9' &&
                    md[i + 1] >= '0' && md[i + 1] <= '9' &&
                    md[i + 2] >= '0' && md[i + 2] <= '9' &&
                    md[i + 3] >= '0' && md[i + 3] <= '9' &&
                    md[i + 5] >= '0' && md[i + 5] <= '9' &&
                    md[i + 6] >= '0' && md[i + 6] <= '9' &&
                    md[i + 8] >= '0' && md[i + 8] <= '9' &&
                    md[i + 9] >= '0' && md[i + 9] <= '9') {
                    date = md.substr(i, 10);
                    break;
                }

            seen_urls.insert(std::string(url));
            if (auto e = make_entry(std::string(url), date, dir))
                items.push_back(std::move(*e));
        }

        pos = start + 1;
    }

    // Sort newest-first (mirrors Python sort)
    std::stable_sort(items.begin(), items.end(),
        [](const eh::bing::WallpaperEntry& a, const eh::bing::WallpaperEntry& b) {
            const std::string& da = a.date == "unknown" ? "0000-00-00" : a.date;
            const std::string& db = b.date == "unknown" ? "0000-00-00" : b.date;
            return da > db;
        });

    std::cerr << "[bing-wallpaper][parse] " << items.size() << " wallpapers found\n";
    return items;
}

// JSON cache helpers — mirrors BingWallpaper.py load_cache / save_cache.
bool load_cache_impl(const std::string& cache_path,
                     std::vector<eh::bing::WallpaperEntry>& out,
                     const std::string& dir) {
     
    out.clear();
    std::ifstream f(cache_path);
    if (!f) return false;
    try {
        nlohmann::json j;
        f >> j;
        for (const auto& item : j) {
            eh::bing::WallpaperEntry e;
            e.date     = item.value("date", "unknown");
            e.month    = item.value("month", "unknown");
            e.title    = item.value("title", "");
            e.url      = item.value("url", "");
            e.filename = item.value("filename", "");
            if (e.url.empty() || e.filename.empty()) continue;
            e.local_path = dir + "/" + e.filename;
            out.push_back(std::move(e));
        }
        std::cerr << "[bing-wallpaper][cache] loaded " << out.size()
                  << " entries from " << cache_path << "\n";
        return !out.empty();
    } catch (const nlohmann::json::exception&) {
        out.clear();
        return false;
    }
}

void save_cache_impl(const std::string& cache_path,
                     const std::vector<eh::bing::WallpaperEntry>& items) {
     
    // Ensure parent dirs exist
    fs::path p(cache_path);
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);

    nlohmann::json j = nlohmann::json::array();
    for (const auto& e : items) {
        j.push_back({
            {"date", e.date}, {"month", e.month}, {"title", e.title},
            {"url", e.url},   {"filename", e.filename},
            {"local_path", e.local_path}
        });
    }
    std::ofstream f(cache_path);
    if (f) {
        f << j.dump(2);
        std::cerr << "[bing-wallpaper][cache] saved " << items.size()
                  << " entries → " << cache_path << "\n";
    } else {
        std::cerr << "[bing-wallpaper][cache] save failed: " << cache_path << "\n";
    }
}

// List local wallpapers — mirrors BingWallpaper.py local_filenames().
std::unordered_set<std::string> local_filenames(const std::string& dir) {
     
    std::unordered_set<std::string> names;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file(ec)) continue;
        const std::string ext = entry.path().extension().string();
        if (ext == ".jpg" || ext == ".jpeg" || ext == ".png" || ext == ".webp")
            names.insert(entry.path().filename().string());
    }
    return names;
}

// Filter wallpapers — mirrors BingWallpaper.py filter_wallpapers().
std::string current_ym() {
     
    const std::time_t now = std::time(nullptr);
    char buf[8];
    std::strftime(buf, sizeof(buf), "%Y-%m", std::localtime(&now));
    return buf;
}

std::string last_month_ym() {
     
    // subtract one month: go to first day of this month, subtract one day
    std::time_t now = std::time(nullptr);
    struct tm t = *std::localtime(&now);
    t.tm_mday = 1;
    t.tm_hour = 0; t.tm_min = 0; t.tm_sec = 0;
    std::time_t first = std::mktime(&t);
    first -= 86400; // one day back
    char buf[8];
    std::strftime(buf, sizeof(buf), "%Y-%m", std::localtime(&first));
    return buf;
}

std::vector<eh::bing::WallpaperEntry> filter_entries_impl(
    const std::vector<eh::bing::WallpaperEntry>& items,
    eh::bing::Filter f,
    const std::string& custom_month)
{
     
    if (f == eh::bing::Filter::All) return items;
    const std::string target = (f == eh::bing::Filter::Current) ? current_ym()
                             : (f == eh::bing::Filter::Last)    ? last_month_ym()
                             : custom_month;
    std::vector<eh::bing::WallpaperEntry> out;
    for (const auto& e : items)
        if (e.month == target) out.push_back(e);
    return out;
}

bool contains_ci(const std::string& haystack, const std::string& needle) {
    if (needle.empty()) return false;
    const auto it = std::search(haystack.begin(), haystack.end(),
                                needle.begin(), needle.end(),
                                [](unsigned char a, unsigned char b) {
                                    return std::tolower(a) == std::tolower(b);
                                });
    return it != haystack.end();
}

} // anonymous namespace

// BingWallpaperService implementation.
namespace eh::bing {

BingWallpaperService& BingWallpaperService::instance() {
     
    static BingWallpaperService s;
    return s;
}

void BingWallpaperService::init() {
     
    std::lock_guard<std::mutex> lk(m_wake_mu);
    if (m_wake_fd >= 0) return;
    m_wake_fd = ::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (m_wake_fd < 0)
        std::cerr << "[bing-wallpaper] eventfd() failed errno=" << errno << "\n";
}

void BingWallpaperService::ping_wake() {
     
    if (m_wake_fd < 0) return;
    const std::uint64_t one = 1;
    (void)::write(m_wake_fd, &one, sizeof(one));
}

void BingWallpaperService::drain_wake() {
     
    if (m_wake_fd < 0) return;
    std::uint64_t v = 0;
    while (::read(m_wake_fd, &v, sizeof(v)) > 0) {}
    std::vector<std::function<void()>> cbs;
    {
        std::lock_guard<std::mutex> lk(m_wake_mu);
        cbs = m_redraws;
    }
    for (auto& fn : cbs) fn();
}

void BingWallpaperService::register_redraw(std::function<void()> fn) {
     
    std::lock_guard<std::mutex> lk(m_wake_mu);
    m_redraws.push_back(std::move(fn));
}

bool BingWallpaperService::is_checking_updates() const noexcept {
     
    std::lock_guard<std::mutex> lk(m_state_mu);
    return m_checking;
}
bool BingWallpaperService::is_downloading() const noexcept {
     
    std::lock_guard<std::mutex> lk(m_state_mu);
    return m_downloading;
}
bool BingWallpaperService::is_fetching_daily() const noexcept {
     
    std::lock_guard<std::mutex> lk(m_state_mu);
    return m_daily;
}

// Result-consume helpers.
std::optional<UpdatesResult> BingWallpaperService::take_updates_result() {
     
    std::lock_guard<std::mutex> lk(m_result_mu);
    return std::exchange(m_updates_result, std::nullopt);
}
std::optional<ProgressResult> BingWallpaperService::take_progress_result() {
     
    std::lock_guard<std::mutex> lk(m_result_mu);
    return std::exchange(m_progress_result, std::nullopt);
}
std::optional<DoneResult> BingWallpaperService::take_done_result() {
     
    std::lock_guard<std::mutex> lk(m_result_mu);
    return std::exchange(m_done_result, std::nullopt);
}
std::optional<DailyResult> BingWallpaperService::take_daily_result() {
     
    std::lock_guard<std::mutex> lk(m_result_mu);
    return std::exchange(m_daily_result, std::nullopt);
}

// Shared fetch helpers (static).
std::string BingWallpaperService::fetch_readme() {
     
    std::cerr << "[bing-wallpaper][fetch] " << kReadmeUrl << "\n";
    std::string body;
    long http = 0;
    if (!curl_fetch_text(kReadmeUrl, body, 30, 15, &http)) return {};
    std::cerr << "[bing-wallpaper][fetch] OK – " << body.size() << " chars\n";
    return body;
}

std::vector<WallpaperEntry> BingWallpaperService::parse_wallpapers(
    const std::string& md, const std::string& dir)
{
     
    return parse_wallpapers_impl(md, dir);
}

bool BingWallpaperService::load_cache(const std::string& cache_path,
                                      std::vector<WallpaperEntry>& out,
                                      const std::string& dir) {
     
    return load_cache_impl(cache_path, out, dir);
}

void BingWallpaperService::save_cache(const std::string& cache_path,
                                      const std::vector<WallpaperEntry>& items) {
     
    save_cache_impl(cache_path, items);
}

std::vector<WallpaperEntry> BingWallpaperService::filter_entries(
    const std::vector<WallpaperEntry>& items, Filter f, const std::string& month)
{
     
    return filter_entries_impl(items, f, month);
}

bool BingWallpaperService::download_one(const WallpaperEntry& e) {
     
    if (fs::exists(e.local_path)) return true; // already on disk
    std::cerr << "[bing-wallpaper][dl] " << e.filename << "  (" << e.date << ")\n";
    // Ensure the directory exists
    std::error_code ec;
    fs::create_directories(fs::path(e.local_path).parent_path(), ec);
    return curl_download_file(e.url, e.local_path);
}

bool BingWallpaperService::matches_blocklist(const WallpaperEntry& e,
                                              const std::vector<std::string>& keywords) {
      
    if (keywords.empty()) return false;
    const std::string combined = e.title + " " + e.filename;
    for (const auto& kw : keywords) {
        if (kw.empty()) continue;
        if (contains_ci(combined, kw)) return true;
    }
    return false;
}

// Check-update mode — mirrors BingWallpaper.py mode_check_updates().
UpdatesResult BingWallpaperService::do_check_updates(const std::string& dir) {
      
    // Ensure download dir exists
    std::error_code ec;
    fs::create_directories(dir, ec);

    const std::string cache_path = cache_file_path();
    std::vector<WallpaperEntry> items;

    // Always fetch the latest README for user-initiated checks.
    const std::string md = fetch_readme();
    if (md.empty()) {
        return UpdatesResult{false, 0, {}, "Failed to fetch README from GitHub"};
    }
    items = parse_wallpapers(md, dir);
    save_cache(cache_path, items);

    const auto on_disk = local_filenames(dir);
    UpdatesResult result;
    result.ok = true;
    for (const auto& e : items) {
        if (matches_blocklist(e, m_blocklist)) continue;
        if (!on_disk.count(e.filename))
            result.new_entries.push_back(e);
    }
    result.new_count = static_cast<int>(result.new_entries.size());
    return result;
}

// Download mode — mirrors BingWallpaper.py mode_download(). Progress is
// published to m_progress_result after every file and the main loop is woken
// via ping_wake() so the UI can update the progress bar in real time.
void BingWallpaperService::do_download(const std::string& dir,
                                        Filter filter,
                                        const std::string& custom_month) {
      
    std::error_code ec;
    fs::create_directories(dir, ec);

    const std::string cache_path = cache_file_path();
    std::vector<WallpaperEntry> items;

    // Always fetch the latest README for user-initiated downloads so that
    // newly-published wallpapers within the current month are not missed.
    const std::string md = fetch_readme();
    if (!md.empty()) {
        items = parse_wallpapers(md, dir);
        save_cache(cache_path, items);
    }

    const auto selected  = filter_entries(items, filter, custom_month);
    const auto on_disk   = local_filenames(dir);

    // Filter out blocked entries
    std::vector<WallpaperEntry> filtered;
    for (const auto& e : selected)
        if (!matches_blocklist(e, m_blocklist))
            filtered.push_back(e);

    // Collect what actually needs to be downloaded
    std::vector<const WallpaperEntry*> to_fetch;
    for (const auto& e : filtered)
        if (!on_disk.count(e.filename)) to_fetch.push_back(&e);

    const int total  = static_cast<int>(to_fetch.size());
    const int already = static_cast<int>(filtered.size()) - total;
    const int found  = static_cast<int>(filtered.size());

    std::cerr << "[bing-wallpaper][dl] " << total << " to download, "
              << already << " already exist, " << found << " in archive\n";

    // Publish initial totals (mirrors FOUND: / TOTAL: / NEWEST: lines)
    {
        ProgressResult pr;
        pr.current = 0;
        pr.total   = total;
        pr.found   = found;
        if (found == 0 && !items.empty())
            pr.newest_available_date = items[0].date;
        std::lock_guard<std::mutex> lk(m_result_mu);
        m_progress_result = pr;
    }
    ping_wake();

    // Immediately publish done if nothing to do
    if (total == 0) {
        DoneResult done;
        done.ok         = true;
        done.downloaded = 0;
        done.skipped    = already;
        done.failed     = 0;
        std::lock_guard<std::mutex> lk(m_result_mu);
        m_done_result = done;
        return;
    }

    int downloaded = 0, skipped = 0, failed = 0;
    for (int i = 0; i < total; ++i) {
        const WallpaperEntry& e = *to_fetch[static_cast<size_t>(i)];
        const bool existed = fs::exists(e.local_path);
        bool ok = false;
        if (existed) {
            ++skipped;
            ok = true;
        } else {
            ok = download_one(e);
            if (ok) ++downloaded;
            else    ++failed;
        }

        // Publish PROGRESS + last DOWNLOADED
        ProgressResult pr;
        pr.current = i + 1;
        pr.total   = total;
        pr.found   = found;
        if (ok && !existed) {
            pr.last_downloaded_filename = e.filename;
            pr.last_downloaded_date     = e.date;
        }
        {
            std::lock_guard<std::mutex> lk(m_result_mu);
            m_progress_result = pr;
        }
        ping_wake();
    }

    DoneResult done;
    done.ok         = (failed == 0);
    done.downloaded = downloaded;
    done.skipped    = skipped;
    done.failed     = failed;
    {
        std::lock_guard<std::mutex> lk(m_result_mu);
        m_done_result = done;
    }
}

// Daily mode — mirrors BingWallpaper.py mode_daily().
DailyResult BingWallpaperService::do_daily(const std::string& dir, bool use_cache) {
     
    std::error_code ec;
    fs::create_directories(dir, ec);

    const std::string cache_path = cache_file_path();
    std::vector<WallpaperEntry> items;
    bool loaded = false;

    if (use_cache) {
        loaded = load_cache(cache_path, items, dir);
        if (loaded && !items.empty() && items[0].month != current_ym()) {
            // The cache is stale (no current-month entries), so refresh it.
            items.clear();
            loaded = false;
        }
    }
    if (!loaded || items.empty()) {
        const std::string md = fetch_readme();
        if (md.empty())
            return DailyResult{false, "", "Failed to fetch README"};
        items = parse_wallpapers(md, dir);
        save_cache(cache_path, items);
    }
    if (items.empty())
        return DailyResult{false, "", "No wallpapers in cache"};

    // items is sorted newest-first; pick the first non-blocked entry
    const WallpaperEntry* chosen = nullptr;
    for (const auto& e : items) {
        if (!matches_blocklist(e, m_blocklist)) {
            chosen = &e;
            break;
        }
    }
    if (!chosen)
        return DailyResult{false, "", "No non-blocked wallpapers available"};
    if (!fs::exists(chosen->local_path)) {
        if (!download_one(*chosen))
            return DailyResult{false, "", "Download failed for " + chosen->filename};
    }
    return DailyResult{true, chosen->local_path, ""};
}

// Async launchers.
bool BingWallpaperService::check_updates_async(const std::string& download_dir) {
     
    {
        std::lock_guard<std::mutex> lk(m_state_mu);
        if (m_checking || m_downloading) return false;
        m_checking = true;
    }
    const std::string dir = download_dir.empty() ? default_download_dir() : download_dir;
    ThreadPool::instance().enqueue([this, dir]() {
        UpdatesResult result = do_check_updates(dir);
        {
            std::lock_guard<std::mutex> lk(m_result_mu);
            m_updates_result = std::move(result);
        }
        {
            std::lock_guard<std::mutex> lk(m_state_mu);
            m_checking = false;
        }
        ping_wake();
    });
    return true;
}

bool BingWallpaperService::download_async(const std::string& download_dir,
                                           Filter filter,
                                           const std::string& custom_month) {
     
    {
        std::lock_guard<std::mutex> lk(m_state_mu);
        if (m_downloading || m_checking) return false;
        m_downloading = true;
    }
    const std::string dir = download_dir.empty() ? default_download_dir() : download_dir;
    ThreadPool::instance().enqueue([this, dir, filter, custom_month]() {
        do_download(dir, filter, custom_month);
        {
            std::lock_guard<std::mutex> lk(m_state_mu);
            m_downloading = false;
        }
        ping_wake();
    });
    return true;
}

bool BingWallpaperService::daily_async(const std::string& download_dir, bool use_cache) {
     
    {
        std::lock_guard<std::mutex> lk(m_state_mu);
        if (m_daily) return false;
        m_daily = true;
    }
    const std::string dir = download_dir.empty() ? default_download_dir() : download_dir;
    ThreadPool::instance().enqueue([this, dir, use_cache]() {
        DailyResult result = do_daily(dir, use_cache);
        {
            std::lock_guard<std::mutex> lk(m_result_mu);
            m_daily_result = std::move(result);
        }
        {
            std::lock_guard<std::mutex> lk(m_state_mu);
            m_daily = false;
        }
        ping_wake();
    });
    return true;
}

} // namespace eh::bing
