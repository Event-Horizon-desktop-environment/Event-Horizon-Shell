#include "desktop_shell/controlcenter/widgets/weather/control_center_weather_widget.hpp"
#include "desktop_shell/controlcenter/paint/control_center_paint_utils.hpp"

#include "configuration/shell_config.hpp"
#include "desktop_shell/widgets/shared/widget_settings.hpp"
#include "desktop_shell/common/bench/bench_trace.hpp"
#include "desktop_shell/common/log/verbose_log.hpp"
#include "bootstrap/thread/thread_pool.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <chrono>
#include <functional>
#include <iostream>
#include <list>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <condition_variable>

#include <curl/curl.h>

#include <sys/eventfd.h>
#include <unistd.h>

#include <cairo/cairo.h>

namespace eh::shell::dock::control_center {

using paint_utils::lower_copy;
using paint_utils::parse_double;
using paint_utils::parse_bool;

// Internal helpers.

namespace {

uint64_t now_mono_ms_cc() {
  timespec ts{};
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return static_cast<uint64_t>(ts.tv_sec) * 1000ULL + static_cast<uint64_t>(ts.tv_nsec / 1000000ULL);
}

size_t curl_write_text_cb(char* ptr, size_t size, size_t nmemb, void* userdata) {
  auto* out = static_cast<std::string*>(userdata);
  const size_t n = size * nmemb;
  out->append(ptr, n);
  return n;
}

bool curl_fetch_text(const std::string& url, std::string& out, long timeout_sec = 8, long* out_http = nullptr,
                     std::string* out_err = nullptr, long connect_timeout_sec = 10) {
  static bool did_init = false;
  if (!did_init) {
    did_init = true;
    (void)curl_global_init(CURL_GLOBAL_DEFAULT);
  }
  out.clear();
  if (out_http) *out_http = 0;
  if (out_err) out_err->clear();
  CURL* curl = curl_easy_init();
  if (!curl) return false;
  char errbuf[CURL_ERROR_SIZE]{};
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_text_cb);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &out);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout_sec);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, connect_timeout_sec);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "EventHorizon/1.0");
  curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
  curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf);
  const auto t_fetch0 = std::chrono::steady_clock::now();
  const CURLcode res = curl_easy_perform(curl);
  const auto t_fetch1 = std::chrono::steady_clock::now();
  long http = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http);
  curl_easy_cleanup(curl);
  if (out_http) *out_http = http;
  if (out_err && errbuf[0]) *out_err = std::string(errbuf);
  const bool ok = res == CURLE_OK && (http == 0 || (http >= 200 && http < 300)) && !out.empty();
  if (eh::bench::enabled()) {
    const int64_t us = std::chrono::duration_cast<std::chrono::microseconds>(t_fetch1 - t_fetch0).count();
    std::string u = url.size() > 140 ? url.substr(0, 137) + "..." : url;
    std::cerr << "[eh-bench] curl_fetch url=\"" << u << "\" http=" << http << " bytes=" << out.size() << " curl_ok="
              << (res == CURLE_OK ? 1 : 0) << " ok=" << (ok ? 1 : 0) << " us=" << us;
    if (res != CURLE_OK && errbuf[0]) std::cerr << " err=\"" << errbuf << "\"";
    std::cerr << "\n";
  }
  if (res != CURLE_OK) return false;
  if (http != 0 && (http < 200 || http >= 300)) return false;
  return !out.empty();
}

bool json_find_number_after_key(const std::string& s, std::string_view key, double* out) {
  const std::string k = "\"" + std::string(key) + "\":";
  const size_t p = s.find(k);
  if (p == std::string::npos) return false;
  size_t i = p + k.size();
  while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
  size_t j = i;
  while (j < s.size() && (std::isdigit(static_cast<unsigned char>(s[j])) || s[j] == '.' || s[j] == '-' || s[j] == '+')) ++j;
  if (j == i) return false;
  *out = std::strtod(s.substr(i, j - i).c_str(), nullptr);
  return true;
}

bool json_extract_object_by_key(const std::string& s, std::string_view key, std::string& out_obj) {
  const std::string k = "\"" + std::string(key) + "\":";
  const size_t p = s.find(k);
  if (p == std::string::npos) return false;
  size_t i = p + k.size();
  while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i;
  if (i >= s.size() || s[i] != '{') return false;
  size_t start = i;
  int depth = 0;
  bool in_str = false;
  for (; i < s.size(); ++i) {
    const char c = s[i];
    if (c == '"' && (i == 0 || s[i - 1] != '\\')) in_str = !in_str;
    if (in_str) continue;
    if (c == '{') ++depth;
    else if (c == '}') {
      --depth;
      if (depth == 0) {
        out_obj = s.substr(start, (i - start) + 1);
        return true;
      }
    }
  }
  return false;
}

bool json_find_first_number_in_array_for_key(const std::string& s, std::string_view key, double* out) {
  const std::string k = "\"" + std::string(key) + "\":";
  const size_t p = s.find(k);
  if (p == std::string::npos) return false;
  size_t i = p + k.size();
  while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i;
  if (i >= s.size() || s[i] != '[') return false;
  ++i;
  while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i;
  size_t j = i;
  while (j < s.size() && (std::isdigit(static_cast<unsigned char>(s[j])) || s[j] == '.' || s[j] == '-' || s[j] == '+')) ++j;
  if (j == i) return false;
  *out = std::strtod(s.substr(i, j - i).c_str(), nullptr);
  return true;
}

bool json_parse_number_array_for_key(const std::string& s, std::string_view key, std::vector<double>& out, int max_n = 16) {
  out.clear();
  const std::string k = "\"" + std::string(key) + "\":";
  const size_t p = s.find(k);
  if (p == std::string::npos) return false;
  size_t i = p + k.size();
  while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i;
  if (i >= s.size() || s[i] != '[') return false;
  ++i;
  while (i < s.size() && s[i] != ']' && static_cast<int>(out.size()) < max_n) {
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r' || s[i] == ',')) ++i;
    if (i >= s.size() || s[i] == ']') break;
    size_t j = i;
    while (j < s.size() && (std::isdigit(static_cast<unsigned char>(s[j])) || s[j] == '.' || s[j] == '-' || s[j] == '+')) ++j;
    if (j == i) break;
    out.push_back(std::strtod(s.substr(i, j - i).c_str(), nullptr));
    i = j;
  }
  return !out.empty();
}

bool json_parse_string_array_for_key(const std::string& s, std::string_view key, std::vector<std::string>& out, int max_n = 16) {
  out.clear();
  const std::string k = "\"" + std::string(key) + "\":";
  const size_t p = s.find(k);
  if (p == std::string::npos) return false;
  size_t i = p + k.size();
  while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i;
  if (i >= s.size() || s[i] != '[') return false;
  ++i;
  while (i < s.size() && s[i] != ']' && static_cast<int>(out.size()) < max_n) {
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r' || s[i] == ',')) ++i;
    if (i >= s.size() || s[i] == ']') break;
    if (s[i] != '"') break;
    ++i;
    std::string r;
    while (i < s.size()) {
      const char c = s[i++];
      if (c == '"') break;
      if (c == '\\' && i < s.size()) r.push_back(s[i++]);
      else r.push_back(c);
    }
    out.push_back(r);
  }
  return !out.empty();
}

std::string weekday_short_from_ymd(const std::string& ymd, int) {
  if (ymd.size() < 10) return "--";
  std::tm tm{};
  tm.tm_year = std::atoi(ymd.substr(0, 4).c_str()) - 1900;
  tm.tm_mon = std::atoi(ymd.substr(5, 2).c_str()) - 1;
  tm.tm_mday = std::atoi(ymd.substr(8, 2).c_str());
  tm.tm_hour = 12;
  tm.tm_isdst = -1;
  (void)std::mktime(&tm);
  static const char* names[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  const int wd = (tm.tm_wday >= 0 && tm.tm_wday <= 6) ? tm.tm_wday : 0;
  return names[wd];
}

std::string weather_code_to_icon(int code) {
  if (code == 0) return "sunny";
  if (code == 1 || code == 2) return "partly_cloudy_day";
  if (code == 3) return "cloud";
  if (code == 45 || code == 48) return "foggy";
  if ((code >= 51 && code <= 57) || (code >= 61 && code <= 67) || (code >= 80 && code <= 82)) return "rainy";
  if ((code >= 71 && code <= 77) || (code == 85 || code == 86)) return "weather_snowy";
  if (code >= 95 && code <= 99) return "thunderstorm";
  return "cloud";
}

std::string weather_code_to_icon_daynight(int code, bool is_day) {
  if (code == 0 || code == 1) return is_day ? "sunny" : "nightlight";
  if (code == 2) return is_day ? "partly_cloudy_day" : "partly_cloudy_night";
  return weather_code_to_icon(code);
}

std::string weather_code_to_label(int code) {
  if (code == 0) return "Clear";
  if (code == 1) return "Mostly clear";
  if (code == 2) return "Partly cloudy";
  if (code == 3) return "Cloudy";
  if (code == 45 || code == 48) return "Fog";
  if (code >= 51 && code <= 57) return "Drizzle";
  if (code >= 61 && code <= 67) return "Rain";
  if (code >= 71 && code <= 77) return "Snow";
  if (code >= 80 && code <= 82) return "Showers";
  if (code == 85 || code == 86) return "Snow showers";
  if (code >= 95 && code <= 99) return "Thunderstorm";
  return "Weather";
}

bool json_find_boolish_after_key(const std::string& s, std::string_view key, bool* out) {
  double v = 0.0;
  if (!json_find_number_after_key(s, key, &v)) return false;
  *out = (std::lround(v) != 0);
  return true;
}

bool json_find_string_after_key(const std::string& s, std::string_view key, std::string& out) {
  const std::string k = "\"" + std::string(key) + "\":";
  const size_t p = s.find(k);
  if (p == std::string::npos) return false;
  size_t i = p + k.size();
  while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
  if (i >= s.size() || s[i] != '"') return false;
  ++i;
  std::string r;
  while (i < s.size()) {
    const char c = s[i++];
    if (c == '"') break;
    if (c == '\\' && i < s.size()) {
      r.push_back(s[i++]);
    } else {
      r.push_back(c);
    }
  }
  out = r;
  return true;
}

bool geocode_city_open_meteo(const std::string& city, double* out_lat, double* out_lon, std::string* out_name,
                             std::string* out_err) {
  if (city.empty()) return false;
  CURL* curl = curl_easy_init();
  if (!curl) return false;
  char* esc = curl_easy_escape(curl, city.c_str(), static_cast<int>(city.size()));
  std::string q = esc ? esc : "";
  if (esc) curl_free(esc);
  curl_easy_cleanup(curl);
  if (q.empty()) return false;
  const std::string url =
      "https://geocoding-api.open-meteo.com/v1/search?name=" + q + "&count=1&language=en&format=json";
  std::string body;
  long http = 0;
  std::string err;
  if (!curl_fetch_text(url, body, 8, &http, &err, 5)) {
    if (out_err) {
      *out_err = "geocode http " + std::to_string(http);
      if (!err.empty()) *out_err += " (" + err + ")";
    }
    return false;
  }
  double lat = 9999.0, lon = 9999.0;
  if (!json_find_number_after_key(body, "latitude", &lat)) return false;
  if (!json_find_number_after_key(body, "longitude", &lon)) return false;
  if (out_lat) *out_lat = lat;
  if (out_lon) *out_lon = lon;
  if (out_name) {
    std::string name;
    std::string country;
    (void)json_find_string_after_key(body, "name", name);
    (void)json_find_string_after_key(body, "country", country);
    if (!name.empty() && !country.empty()) *out_name = name + ", " + country;
    else *out_name = !name.empty() ? name : city;
  }
  return true;
}

bool auto_location_from_ip(double* out_lat, double* out_lon, std::string* out_name, std::string* out_err) {
  struct LocCache {
    uint64_t last_ms = 0;
    uint64_t last_fail_ms = 0;
    bool ok = false;
    double lat = 0.0;
    double lon = 0.0;
    std::string name{};
  };
  static std::mutex mu;
  static LocCache cache;
  const uint64_t now = now_mono_ms_cc();

  {
    std::lock_guard<std::mutex> lock(mu);
    if (cache.ok && cache.last_ms != 0 && now >= cache.last_ms && (now - cache.last_ms) <= 24ULL * 60ULL * 60ULL * 1000ULL) {
      if (out_lat) *out_lat = cache.lat;
      if (out_lon) *out_lon = cache.lon;
      if (out_name) *out_name = cache.name;
      return true;
    }
    if (!cache.ok && cache.last_fail_ms != 0 && now >= cache.last_fail_ms && (now - cache.last_fail_ms) <= 5ULL * 60ULL * 1000ULL) {
      return false;
    }
  }

  auto try_provider = [&](const char* tag, const std::string& url, const char* latKey, const char* lonKey,
                          const char* cityKey, const char* regionKey, const char* countryKey,
                          double* outLat, double* outLon, std::string* outName, std::string* outErr) -> bool {
    std::string body;
    long http = 0;
    std::string err;
    if (!curl_fetch_text(url, body, 8, &http, &err, 5)) {
      if (outErr) {
        *outErr = std::string(tag) + " http " + std::to_string(http);
        if (!err.empty()) *outErr += " (" + err + ")";
      }
      return false;
    }
    double lat = 9999.0, lon = 9999.0;
    if (!json_find_number_after_key(body, latKey, &lat) || !json_find_number_after_key(body, lonKey, &lon)) {
      if (outErr) *outErr = std::string(tag) + " parse failed";
      return false;
    }
    std::string city, region, country;
    (void)json_find_string_after_key(body, cityKey, city);
    if (regionKey && regionKey[0]) (void)json_find_string_after_key(body, regionKey, region);
    (void)json_find_string_after_key(body, countryKey, country);
    std::string name = city;
    if (!region.empty() && region != city) {
      if (!name.empty()) name += ", ";
      name += region;
    }
    if (!country.empty()) {
      if (!name.empty()) name += ", ";
      name += country;
    }
    if (name.empty()) name = "Local";
    if (outLat) *outLat = lat;
    if (outLon) *outLon = lon;
    if (outName) *outName = name;
    if (eh_verbose_enabled()) std::cout << "[weather][loc] provider=" << tag << " lat=" << lat << " lon=" << lon << " name=\"" << name << "\"\n";
    return true;
  };

  double lat = 9999.0;
  double lon = 9999.0;
  std::string name{};
  std::string why{};

  if (!try_provider("ipapi", "https://ipapi.co/json/", "latitude", "longitude", "city", "region", "country_name",
                    &lat, &lon, &name, &why)) {
    if (!try_provider("ip-api", "http://ip-api.com/json/?fields=status,message,lat,lon,city,regionName,country",
                      "lat", "lon", "city", "regionName", "country", &lat, &lon, &name, &why)) {
      (void)try_provider("ipwhois", "https://ipwho.is/?fields=success,message,latitude,longitude,city,region,country",
                         "latitude", "longitude", "city", "region", "country", &lat, &lon, &name, &why);
    }
  }

  if (std::abs(lat) > 90.0 || std::abs(lon) > 180.0) {
    if (out_err) *out_err = why.empty() ? "Location unavailable" : why;
    std::lock_guard<std::mutex> lock(mu);
    cache.ok = false;
    cache.last_fail_ms = now;
    return false;
  }

  {
    std::lock_guard<std::mutex> lock(mu);
    cache.ok = true;
    cache.last_ms = now;
    cache.lat = lat;
    cache.lon = lon;
    cache.name = name;
  }
  if (out_lat) *out_lat = lat;
  if (out_lon) *out_lon = lon;
  if (out_name) *out_name = name;
  return true;
}

struct GeoResult {
  double lat = 9999.0;
  double lon = 9999.0;
  std::string name;
};

std::mutex g_geo_mu;
uint64_t g_geo_city_ms = 0;
double g_geo_city_lat = 9999.0;
double g_geo_city_lon = 9999.0;
std::string g_geo_city_name;
std::string g_geo_city_query;
std::atomic<bool> g_geo_city_pending{false};

uint64_t g_geo_ip_ms = 0;
double g_geo_ip_lat = 9999.0;
double g_geo_ip_lon = 9999.0;
std::string g_geo_ip_name;
std::atomic<bool> g_geo_ip_pending{false};

void geo_cache_store_city(const std::string& query, double lat, double lon, const std::string& name) {
  std::lock_guard<std::mutex> lock(g_geo_mu);
  g_geo_city_ms = now_mono_ms_cc();
  g_geo_city_lat = lat;
  g_geo_city_lon = lon;
  g_geo_city_name = name;
  g_geo_city_query = query;
}

void geo_cache_store_ip(double lat, double lon, const std::string& name) {
  std::lock_guard<std::mutex> lock(g_geo_mu);
  g_geo_ip_ms = now_mono_ms_cc();
  g_geo_ip_lat = lat;
  g_geo_ip_lon = lon;
  g_geo_ip_name = name;
}

std::optional<GeoResult> geocode_city_fast(const std::string& city) {
  {
    std::lock_guard<std::mutex> lock(g_geo_mu);
    if (!g_geo_city_query.empty() && g_geo_city_query == city && g_geo_city_ms != 0) {
      const uint64_t now = now_mono_ms_cc();
      if (now >= g_geo_city_ms && (now - g_geo_city_ms) <= 24ULL * 60ULL * 60ULL * 1000ULL) {
        return GeoResult{g_geo_city_lat, g_geo_city_lon, g_geo_city_name};
      }
    }
  }

  bool expected = false;
  if (g_geo_city_pending.compare_exchange_strong(expected, true)) {
    std::string cpy = city;
    ThreadPool::instance().enqueue([cpy]() {
      double lat = 9999.0, lon = 9999.0;
      std::string name;
      std::string err;
      if (geocode_city_open_meteo(cpy, &lat, &lon, &name, &err)) {
        geo_cache_store_city(cpy, lat, lon, name);
        if (eh_verbose_enabled()) std::cout << "[geo][async] city=\"" << cpy << "\" -> lat=" << lat << " lon=" << lon << " name=\"" << name << "\"\n";
      } else {
        if (eh_verbose_enabled()) std::cout << "[geo][async] city=\"" << cpy << "\" failed: " << err << "\n";
      }
      g_geo_city_pending = false;
    });
  }

  return std::nullopt;
}

std::optional<GeoResult> geocode_ip_fast() {
  {
    std::lock_guard<std::mutex> lock(g_geo_mu);
    if (g_geo_ip_ms != 0) {
      const uint64_t now = now_mono_ms_cc();
      if (now >= g_geo_ip_ms && (now - g_geo_ip_ms) <= 24ULL * 60ULL * 60ULL * 1000ULL) {
        return GeoResult{g_geo_ip_lat, g_geo_ip_lon, g_geo_ip_name};
      }
    }
  }

  bool expected = false;
  if (g_geo_ip_pending.compare_exchange_strong(expected, true)) {
    ThreadPool::instance().enqueue([]() {
      double lat = 9999.0, lon = 9999.0;
      std::string name;
      std::string err;
      if (auto_location_from_ip(&lat, &lon, &name, &err)) {
        geo_cache_store_ip(lat, lon, name);
        if (eh_verbose_enabled()) std::cout << "[geo][async] ip -> lat=" << lat << " lon=" << lon << " name=\"" << name << "\"\n";
      } else {
        if (eh_verbose_enabled()) std::cout << "[geo][async] ip failed: " << err << "\n";
      }
      g_geo_ip_pending = false;
    });
  }

  return std::nullopt;
}

} // anonymous namespace

// Weather cache.

struct WeatherCacheEntry {
  uint64_t last_ms = 0;
  ControlCenterWeatherState st{};
};
static std::mutex g_weather_cache_mu;
static std::unordered_map<std::string, WeatherCacheEntry> g_weather_cache;

static void weather_cache_trim_if_needed_locked() {
  constexpr std::size_t kMaxKeys = 32;
  while (g_weather_cache.size() > kMaxKeys) {
    auto oldest = g_weather_cache.begin();
    for (auto it = g_weather_cache.begin(); it != g_weather_cache.end(); ++it) {
      if (it->second.last_ms < oldest->second.last_ms) oldest = it;
    }
    if (oldest == g_weather_cache.end()) break;
    g_weather_cache.erase(oldest);
  }
}

static std::mutex g_weather_inflight_mu;
static std::unordered_set<std::string> g_weather_inflight_keys;

namespace {
struct WakeFd {
  int fd = -1;
  ~WakeFd() { if (fd >= 0) ::close(fd); }
};
static WakeFd g_weather_wake;
}

static std::mutex g_weather_wake_mu;
static std::vector<std::function<void()>> g_weather_redraws;

void control_center_weather_async_init() {
  std::lock_guard<std::mutex> lock(g_weather_wake_mu);
  if (g_weather_wake.fd >= 0) return;
  g_weather_wake.fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
}

int control_center_weather_async_wake_fd() noexcept { return g_weather_wake.fd; }

static void weather_wake_ping() {
  if (g_weather_wake.fd < 0) return;
  uint64_t one = 1;
  (void)::write(g_weather_wake.fd, &one, sizeof(one));
}

void control_center_weather_async_register_redraw(std::function<void()> redraw) {
  std::lock_guard<std::mutex> lock(g_weather_wake_mu);
  g_weather_redraws.clear();
  g_weather_redraws.push_back(std::move(redraw));
}

void control_center_weather_async_handle_wake() {
  if (g_weather_wake.fd < 0) return;
  uint64_t v = 0;
  while (::read(g_weather_wake.fd, &v, sizeof(v)) > 0) {
  }
  std::vector<std::function<void()>> cbs;
  {
    std::lock_guard<std::mutex> lock(g_weather_wake_mu);
    cbs = g_weather_redraws;
  }
  for (auto& f : cbs) {
    if (f) f();
  }
}

struct WeatherCurlJob {
  std::string key;
  std::string location_label;
  bool fahrenheit = false;
  double lat = 0.0;
  double lon = 0.0;
  std::string body;
  char errbuf[CURL_ERROR_SIZE]{};
};

static CURLM* g_weather_multi = nullptr;
static uint64_t s_weather_fetch_announce_ms = 0;

static void weather_multi_ensure() {
  static bool s_curl_global = false;
  if (!s_curl_global) {
    s_curl_global = true;
    (void)curl_global_init(CURL_GLOBAL_DEFAULT);
  }
  if (!g_weather_multi) g_weather_multi = curl_multi_init();
}

static size_t weather_curl_job_write(char* ptr, size_t size, size_t nmemb, void* userdata) {
  auto* job = static_cast<WeatherCurlJob*>(userdata);
  const size_t n = size * nmemb;
  job->body.append(ptr, n);
  return n;
}

static void weather_curl_easy_common(CURL* curl, WeatherCurlJob* job) {
  std::memset(job->errbuf, 0, sizeof(job->errbuf));
  curl_easy_setopt(curl, CURLOPT_PRIVATE, static_cast<void*>(job));
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, weather_curl_job_write);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, job);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "EventHorizon/1.0");
  curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
  curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, job->errbuf);
}

static void weather_open_meteo_apply_fetch_outcome(double lat, double lon, bool fahrenheit, const std::string& key,
                                                    const std::string& location_label, const std::string& body,
                                                    bool fetch_ok, long http, const std::string& err) {
  (void)lat;
  (void)lon;
  static uint64_t s_lastWeatherLogMs = 0;
  const uint64_t now = now_mono_ms_cc();
  const bool allowLog =
      (s_lastWeatherLogMs == 0) || (now > s_lastWeatherLogMs && (now - s_lastWeatherLogMs) > 30 * 1000ULL);
  if (allowLog) s_lastWeatherLogMs = now;

  ControlCenterWeatherState out{};
  out.fahrenheit = fahrenheit;
  out.location = location_label;

  if (!fetch_ok) {
    out.available = false;
    out.status_text = "open-meteo http " + std::to_string(http);
    if (!err.empty()) out.status_text += " (" + err + ")";
    std::cout << "[weather][fetch] fail " << out.status_text << "\n";
    std::lock_guard<std::mutex> lock(g_weather_cache_mu);
    WeatherCacheEntry& e = g_weather_cache[key];
    e.last_ms = now;
    e.st = out;
    weather_cache_trim_if_needed_locked();
    return;
  }

  std::string curObj;
  std::string dailyObj;
  const bool hasCur = json_extract_object_by_key(body, "current", curObj);
  const bool hasDaily = json_extract_object_by_key(body, "daily", dailyObj);
  if (!hasCur) {
    out.available = false;
    out.status_text = "Weather parse failed";
    if (allowLog) std::cout << "[weather][parse] fail missing current\n";
    std::lock_guard<std::mutex> lock(g_weather_cache_mu);
    WeatherCacheEntry& e = g_weather_cache[key];
    e.last_ms = now;
    e.st = out;
    weather_cache_trim_if_needed_locked();
    return;
  }

  double tcur = 0.0;
  double wcode = 0.0;
  bool isDay = true;
  const bool ok1 = json_find_number_after_key(curObj, "temperature_2m", &tcur);
  const bool ok2 = json_find_number_after_key(curObj, "weather_code", &wcode);
  (void)json_find_boolish_after_key(curObj, "is_day", &isDay);
  double feels = tcur;
  double hum = 0.0;
  double wind = 0.0;
  double vis = 0.0;
  (void)json_find_number_after_key(curObj, "apparent_temperature", &feels);
  (void)json_find_number_after_key(curObj, "relative_humidity_2m", &hum);
  (void)json_find_number_after_key(curObj, "wind_speed_10m", &wind);
  (void)json_find_number_after_key(curObj, "visibility", &vis);

  double tmax = tcur;
  double tmin = tcur;
  if (hasDaily) {
    (void)json_find_first_number_in_array_for_key(dailyObj, "temperature_2m_max", &tmax);
    (void)json_find_first_number_in_array_for_key(dailyObj, "temperature_2m_min", &tmin);
  }

  if (!(ok1 && ok2)) {
    out.available = false;
    out.status_text = "Weather parse failed";
    if (allowLog) std::cout << "[weather][parse] fail ok1=" << ok1 << " ok2=" << ok2 << "\n";
    std::lock_guard<std::mutex> lock(g_weather_cache_mu);
    WeatherCacheEntry& e = g_weather_cache[key];
    e.last_ms = now;
    e.st = out;
    weather_cache_trim_if_needed_locked();
    return;
  }

  const int code = static_cast<int>(std::lround(wcode));
  out.available = true;
  out.temp = static_cast<int>(std::lround(tcur));
  out.hi = static_cast<int>(std::lround(tmax));
  out.lo = static_cast<int>(std::lround(tmin));
  out.feels_like = static_cast<int>(std::lround(feels));
  out.humidity_pct = static_cast<int>(std::lround(hum));
  out.wind_kmh = static_cast<int>(std::lround(wind));
  out.visibility_m = static_cast<int>(std::lround(vis));
  out.condition = weather_code_to_label(code);
  out.is_day = isDay;
  out.icon = weather_code_to_icon_daynight(code, isDay);
  out.status_text.clear();

  out.forecast.clear();
  if (hasDaily) {
    std::vector<std::string> days;
    std::vector<double> codes;
    std::vector<double> his;
    std::vector<double> los;
    (void)json_parse_string_array_for_key(dailyObj, "time", days, 6);
    (void)json_parse_number_array_for_key(dailyObj, "weather_code", codes, 6);
    (void)json_parse_number_array_for_key(dailyObj, "temperature_2m_max", his, 6);
    (void)json_parse_number_array_for_key(dailyObj, "temperature_2m_min", los, 6);
    const int n = std::min({static_cast<int>(days.size()), static_cast<int>(his.size()), static_cast<int>(los.size()), 5});
    out.forecast.reserve(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
      ControlCenterWeatherState::ForecastDay d{};
      d.day = weekday_short_from_ymd(days[static_cast<size_t>(i)], i);
      d.wcode = (i < static_cast<int>(codes.size())) ? static_cast<int>(std::lround(codes[static_cast<size_t>(i)])) : code;
      d.hi = static_cast<int>(std::lround(his[static_cast<size_t>(i)]));
      d.lo = static_cast<int>(std::lround(los[static_cast<size_t>(i)]));
      d.icon = weather_code_to_icon_daynight(d.wcode, true);
      out.forecast.push_back(std::move(d));
    }
  }

  {
    std::lock_guard<std::mutex> lock(g_weather_cache_mu);
    WeatherCacheEntry& e = g_weather_cache[key];
    e.last_ms = now;
    e.st = out;
    weather_cache_trim_if_needed_locked();
  }
  if (eh_verbose_enabled()) {
    std::cout << "[weather][ok] temp=" << out.temp << " hi=" << out.hi << " lo=" << out.lo
              << " feels=" << out.feels_like << " hum=" << out.humidity_pct << " wind=" << out.wind_kmh
              << " vis=" << out.visibility_m << " code=" << code << " day=" << (out.is_day ? 1 : 0) << "\n";
  }
}

void control_center_weather_drive_curl_multi() {
  if (!g_weather_multi) return;
  int still = 0;
  (void)curl_multi_perform(g_weather_multi, &still);
  int msgs_left = 0;
  CURLMsg* msg = nullptr;
  while ((msg = curl_multi_info_read(g_weather_multi, &msgs_left)) != nullptr) {
    if (msg->msg != CURLMSG_DONE) continue;
    CURL* easy = msg->easy_handle;
    void* priv = nullptr;
    if (curl_easy_getinfo(easy, CURLINFO_PRIVATE, &priv) != CURLE_OK || !priv) {
      curl_multi_remove_handle(g_weather_multi, easy);
      curl_easy_cleanup(easy);
      continue;
    }
    auto* job = static_cast<WeatherCurlJob*>(priv);
    long http = 0;
    (void)curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &http);
    const CURLcode tres = msg->data.result;
    const bool fetch_ok =
        tres == CURLE_OK && (http == 0 || (http >= 200 && http < 300)) && !job->body.empty();
    std::string err;
    if (job->errbuf[0]) err.assign(job->errbuf);
    weather_open_meteo_apply_fetch_outcome(job->lat, job->lon, job->fahrenheit, job->key, job->location_label,
                                           job->body, fetch_ok, http, err);
    const std::string inflight_key = job->key;
    curl_multi_remove_handle(g_weather_multi, easy);
    curl_easy_cleanup(easy);
    delete job;
    {
      std::lock_guard<std::mutex> infl(g_weather_inflight_mu);
      g_weather_inflight_keys.erase(inflight_key);
    }
    weather_wake_ping();
  }
}

static void weather_open_meteo_fetch_blocking(double lat, double lon, bool fahrenheit, const std::string& key,
                                               const std::string& location_label) {
  std::string url = "https://api.open-meteo.com/v1/forecast?latitude=" + std::to_string(lat) +
                    "&longitude=" + std::to_string(lon) +
                    "&current=temperature_2m,apparent_temperature,relative_humidity_2m,is_day,weather_code,wind_speed_10m,visibility&"
                    "daily=weather_code,temperature_2m_max,temperature_2m_min&timezone=auto&forecast_days=6&temperature_unit=" +
                    std::string(fahrenheit ? "fahrenheit" : "celsius");

  std::string body;
  long http = 0;
  std::string err;
  const uint64_t t0 = now_mono_ms_cc();
  const bool logFetchLine = (s_weather_fetch_announce_ms == 0) ||
                            (t0 > s_weather_fetch_announce_ms && (t0 - s_weather_fetch_announce_ms) > 30 * 1000ULL);
  if (logFetchLine && eh_verbose_enabled()) {
    s_weather_fetch_announce_ms = t0;
    std::cout << "[weather][fetch] open-meteo lat=" << lat << " lon=" << lon << " unit=" << (fahrenheit ? "F" : "C") << "\n";
  }

  const bool ok = curl_fetch_text(url, body, 10, &http, &err, 5);
  weather_open_meteo_apply_fetch_outcome(lat, lon, fahrenheit, key, location_label, body, ok, http, err);
}

static void weather_collect_control_center_instance_ids(const eh::config::ShellConfig& sc,
                                                        std::unordered_set<std::string>& out) {
  for (const auto& kv : sc.widgets) {
    const std::string t = eh::config::widget_implementation_type(kv.first);
    if (t == "control_center" || t == "weather") out.insert(kv.first);
  }
  auto walk = [&](const std::vector<std::string>& v) {
    for (const auto& id : v) {
      const std::string t = eh::config::widget_implementation_type(id);
      if (t == "control_center" || t == "weather") out.insert(id);
    }
  };
  walk(sc.dock.leftWidgets);
  walk(sc.dock.centerWidgets);
  walk(sc.dock.rightWidgets);
}

static void weather_fill_cache_sync_one(const eh::config::ShellConfig& sc, std::string_view instance_id) {
  const std::string latS = static_cast<const std::string&>(eh::widgets::widget_setting(sc, instance_id, "weather_lat"));
  const std::string lonS = static_cast<const std::string&>(eh::widgets::widget_setting(sc, instance_id, "weather_lon"));
  const std::string cityS = static_cast<const std::string&>(eh::widgets::widget_setting(sc, instance_id, "weather_city"));
  double lat = parse_double(latS, 9999.0);
  double lon = parse_double(lonS, 9999.0);

  const std::string unitRaw = lower_copy(static_cast<const std::string&>(eh::widgets::widget_setting(sc, instance_id, "weather_unit")));
  const bool fahrenheit = (unitRaw == "f" || unitRaw == "fahrenheit");
  const std::string name = static_cast<const std::string&>(eh::widgets::widget_setting(sc, instance_id, "weather_name"));
  std::string location_label = name.empty() ? "Weather" : name;

  if (std::abs(lat) > 90.0 || std::abs(lon) > 180.0) {
    std::string geoName;
    std::string locErr;
    if (!cityS.empty() && geocode_city_open_meteo(cityS, &lat, &lon, &geoName, &locErr)) {
      if (location_label == "Weather") location_label = geoName;
      if (eh_verbose_enabled()) std::cout << "[weather][loc] geocode city=\"" << cityS << "\" -> lat=" << lat << " lon=" << lon
                  << " name=\"" << geoName << "\"\n";
      geo_cache_store_city(cityS, lat, lon, geoName);
    } else if (auto_location_from_ip(&lat, &lon, &geoName, &locErr)) {
      if (location_label == "Weather") location_label = geoName;
      geo_cache_store_ip(lat, lon, geoName);
    } else {
      if (eh_verbose_enabled()) std::cout << "[weather][service] skip instance=\"" << instance_id << "\" err=\"" << locErr << "\"\n";
      return;
    }
  }

  const uint64_t now = now_mono_ms_cc();
  const std::string key = std::to_string(lat) + "," + std::to_string(lon) + (fahrenheit ? ",f" : ",c");

  {
    std::lock_guard<std::mutex> lock(g_weather_cache_mu);
    auto it = g_weather_cache.find(key);
    if (it != g_weather_cache.end() && it->second.last_ms != 0 && now >= it->second.last_ms &&
        (now - it->second.last_ms) <= 10 * 60 * 1000ULL) {
      return;
    }
  }

  weather_open_meteo_fetch_blocking(lat, lon, fahrenheit, key, location_label);
  if (!name.empty()) {
    std::lock_guard<std::mutex> lock(g_weather_cache_mu);
    auto it = g_weather_cache.find(key);
    if (it != g_weather_cache.end()) it->second.st.location = name;
  }
}

static std::atomic<bool> g_weather_service_stop{false};
static std::thread g_weather_service_thread;
static std::once_flag g_weather_service_once;
static std::mutex g_weather_cv_mutex;
static std::condition_variable g_weather_cv;
// Last time any painter actually queried weather state (monotonic ms).
static std::atomic<std::uint64_t> g_weather_last_query_ms{0};
// The service thread starts lazily on the first weather paint, not at shell
// boot; while no painter has queried for kWeatherIdleSkipMs it makes no
// network passes at all ("cache until refresh").
constexpr std::uint64_t kWeatherIdleSkipMs = 20ULL * 60 * 1000ULL;

static void weather_service_thread_main() {
  constexpr int kIntervalSec = 10 * 60;
  while (!g_weather_service_stop.load(std::memory_order_relaxed)) {
    const eh::config::ShellConfig sc = eh::config::shell_config_snapshot_skip_matugen();
    const uint64_t last_q = g_weather_last_query_ms.load(std::memory_order_relaxed);
    const bool active = last_q != 0 &&
                        (now_mono_ms_cc() >= last_q) &&
                        ((now_mono_ms_cc() - last_q) <= kWeatherIdleSkipMs);
    if (active) {
      std::unordered_set<std::string> ids;
      weather_collect_control_center_instance_ids(sc, ids);
      for (const std::string& id : ids) {
        if (g_weather_service_stop.load(std::memory_order_relaxed)) break;
        weather_fill_cache_sync_one(sc, id);
      }
      weather_wake_ping();
    }
    std::unique_lock<std::mutex> lock(g_weather_cv_mutex);
    g_weather_cv.wait_for(lock, std::chrono::seconds(kIntervalSec),
                          [&]() { return g_weather_service_stop.load(std::memory_order_relaxed); });
  }
}

void control_center_weather_service_start() {
  std::call_once(g_weather_service_once, []() {
    control_center_weather_async_init();
    g_weather_service_stop = false;
    g_weather_service_thread = std::thread(weather_service_thread_main);
  });
}

void control_center_weather_service_stop() {
  g_weather_service_stop.store(true, std::memory_order_relaxed);
  g_weather_cv.notify_all();
  if (g_weather_service_thread.joinable()) g_weather_service_thread.join();
}

void control_center_weather_startup_dock(void* app_ctx, void (*redraw_fn)(void* ctx)) {
  control_center_weather_async_init();
  // The fetch thread starts lazily on the first control_center_weather_state()
  // query; booting the shell must not hit the network for weather.
  control_center_weather_async_register_redraw([app_ctx, redraw_fn]() {
    if (redraw_fn) redraw_fn(app_ctx);
  });
}

ControlCenterWeatherState control_center_weather_state(const eh::config::ShellConfig& sc, std::string_view instance_id) {
  // Lazy activation: the first painter that asks for weather (control center,
  // desktop widget, taskbar) starts the fetch thread; nothing runs at shell
  // boot and idle shells make no weather requests.
  // NOTE: control_center_weather_service_start() is itself call_once-guarded;
  // do NOT wrap it in another call_once on the same flag (self-deadlock).
  const uint64_t qnow = now_mono_ms_cc();
  g_weather_last_query_ms.store(qnow, std::memory_order_relaxed);
  control_center_weather_service_start();
  g_weather_cv.notify_all();

  ControlCenterWeatherState out{};
  const std::string latS = static_cast<const std::string&>(eh::widgets::widget_setting(sc, instance_id, "weather_lat"));
  const std::string lonS = static_cast<const std::string&>(eh::widgets::widget_setting(sc, instance_id, "weather_lon"));
  const std::string cityS = static_cast<const std::string&>(eh::widgets::widget_setting(sc, instance_id, "weather_city"));
  double lat = parse_double(latS, 9999.0);
  double lon = parse_double(lonS, 9999.0);

  const std::string unitRaw = lower_copy(static_cast<const std::string&>(eh::widgets::widget_setting(sc, instance_id, "weather_unit")));
  const bool fahrenheit = (unitRaw == "f" || unitRaw == "fahrenheit");
  const std::string name = static_cast<const std::string&>(eh::widgets::widget_setting(sc, instance_id, "weather_name"));
  out.location = name.empty() ? "Weather" : name;
  out.fahrenheit = fahrenheit;

  if (std::abs(lat) > 90.0 || std::abs(lon) > 180.0) {
    std::string locErr;
    if (!cityS.empty()) {
      auto cached = geocode_city_fast(cityS);
      if (cached) {
        lat = cached->lat;
        lon = cached->lon;
        if (out.location == "Weather") out.location = cached->name;
      } else {
        out.available = false;
        out.status_text = "Locating\u2026";
        return out;
      }
    } else {
      auto cached = geocode_ip_fast();
      if (cached) {
        lat = cached->lat;
        lon = cached->lon;
        if (out.location == "Weather") out.location = cached->name;
      } else {
        out.available = false;
        out.status_text = "Locating\u2026";
        return out;
      }
    }
  }

  const uint64_t now = now_mono_ms_cc();
  const std::string key = std::to_string(lat) + "," + std::to_string(lon) + (fahrenheit ? ",f" : ",c");

  {
    std::lock_guard<std::mutex> lock(g_weather_cache_mu);
    auto it = g_weather_cache.find(key);
    if (it != g_weather_cache.end() && it->second.last_ms != 0 && now >= it->second.last_ms &&
        (now - it->second.last_ms) <= 10 * 60 * 1000ULL) {
      out = it->second.st;
      if (!name.empty()) out.location = name;
      return out;
    }
  }

  bool start_fetch = false;
  {
    std::lock_guard<std::mutex> infl(g_weather_inflight_mu);
    if (!g_weather_inflight_keys.count(key)) {
      g_weather_inflight_keys.insert(key);
      start_fetch = true;
    }
  }

  if (start_fetch) {
    const std::string loc_label = out.location;
    weather_multi_ensure();
    auto job = std::make_unique<WeatherCurlJob>();
    job->key = key;
    job->location_label = loc_label;
    job->fahrenheit = fahrenheit;
    job->lat = lat;
    job->lon = lon;

    const uint64_t t0 = now_mono_ms_cc();
    const bool logFetchLine = (s_weather_fetch_announce_ms == 0) ||
                              (t0 > s_weather_fetch_announce_ms && (t0 - s_weather_fetch_announce_ms) > 30 * 1000ULL);
    if (logFetchLine && eh_verbose_enabled()) {
      s_weather_fetch_announce_ms = t0;
      std::cout << "[weather][fetch] open-meteo lat=" << lat << " lon=" << lon << " unit=" << (fahrenheit ? "F" : "C") << "\n";
    }

    std::string url = "https://api.open-meteo.com/v1/forecast?latitude=" + std::to_string(lat) + "&longitude=" +
                      std::to_string(lon) +
                      "&current=temperature_2m,apparent_temperature,relative_humidity_2m,is_day,weather_code,wind_speed_10m,visibility&"
                      "daily=weather_code,temperature_2m_max,temperature_2m_min&timezone=auto&forecast_days=6&temperature_unit=" +
                      std::string(fahrenheit ? "fahrenheit" : "celsius");

    CURL* easy = curl_easy_init();
    if (!easy) {
      std::lock_guard<std::mutex> infl(g_weather_inflight_mu);
      g_weather_inflight_keys.erase(key);
      weather_wake_ping();
    } else {
      WeatherCurlJob* raw = job.release();
      weather_curl_easy_common(easy, raw);
      curl_easy_setopt(easy, CURLOPT_URL, url.c_str());
      const CURLMcode m = curl_multi_add_handle(g_weather_multi, easy);
      if (m != CURLM_OK) {
        curl_easy_cleanup(easy);
        delete raw;
        std::lock_guard<std::mutex> infl(g_weather_inflight_mu);
        g_weather_inflight_keys.erase(key);
        weather_wake_ping();
      }
    }
  }

  ControlCenterWeatherState loading{};
  loading.available = false;
  loading.status_text = "Loading weather\u2026";
  loading.location = out.location;
  loading.fahrenheit = fahrenheit;
  return loading;
}

} // namespace eh::shell::dock::control_center
