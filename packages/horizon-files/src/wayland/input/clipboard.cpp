#include "wayland/input/clipboard.hpp"

#include <wayland-client.h>

#include <fcntl.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <sstream>

namespace {

constexpr std::string_view kTextUtf8 = "text/plain;charset=utf-8";
constexpr std::string_view kTextPlain = "text/plain";
constexpr std::string_view kUtf8String = "UTF8_STRING";
constexpr std::string_view kGnomeCopiedFiles = "x-special/gnome-copied-files";
constexpr std::string_view kUriList = "text/uri-list";

void close_fd(int& fd) {
  if (fd >= 0) {
    close(fd);
    fd = -1;
  }
}

static int hex_digit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
  if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
  return -1;
}

// ── ext-data-control-v1 ops callbacks ────────────────────────────────────

void* bindExtManager([[maybe_unused]] wl_registry* registry, [[maybe_unused]] uint32_t name,
                     [[maybe_unused]] uint32_t version) {
  return nullptr;
}

void destroyExtManager(void* manager) {
  if (manager) ext_data_control_manager_v1_destroy(static_cast<ext_data_control_manager_v1*>(manager));
}

void* getExtDataDevice(void* manager, wl_seat* seat) {
  return ext_data_control_manager_v1_get_data_device(static_cast<ext_data_control_manager_v1*>(manager), seat);
}

void destroyExtDevice(void* device) {
  if (device) ext_data_control_device_v1_destroy(static_cast<ext_data_control_device_v1*>(device));
}

void* createExtDataSource(void* manager) {
  return ext_data_control_manager_v1_create_data_source(static_cast<ext_data_control_manager_v1*>(manager));
}

void destroyExtSource(void* source) {
  if (source) ext_data_control_source_v1_destroy(static_cast<ext_data_control_source_v1*>(source));
}

void extSourceOffer(void* source, const char* mimeType) {
  ext_data_control_source_v1_offer(static_cast<ext_data_control_source_v1*>(source), mimeType);
}

void extDeviceSetSelection(void* device, void* source) {
  ext_data_control_device_v1_set_selection(static_cast<ext_data_control_device_v1*>(device),
                                           static_cast<ext_data_control_source_v1*>(source));
}

void extDeviceSetPrimarySelection(void* device, void* source) {
  ext_data_control_device_v1_set_primary_selection(static_cast<ext_data_control_device_v1*>(device),
                                                   static_cast<ext_data_control_source_v1*>(source));
}

void destroyExtOffer(void* offer) {
  if (offer) ext_data_control_offer_v1_destroy(static_cast<ext_data_control_offer_v1*>(offer));
}

void extOfferReceive(void* offer, const char* mimeType, int fd) {
  ext_data_control_offer_v1_receive(static_cast<ext_data_control_offer_v1*>(offer), mimeType, fd);
}

// ── wlr-data-control-unstable-v1 ops callbacks ───────────────────────────

void* bindWlrManager([[maybe_unused]] wl_registry* registry, [[maybe_unused]] uint32_t name,
                     [[maybe_unused]] uint32_t version) {
  return nullptr;
}

void destroyWlrManager(void* manager) {
  if (manager) zwlr_data_control_manager_v1_destroy(static_cast<zwlr_data_control_manager_v1*>(manager));
}

void* getWlrDataDevice(void* manager, wl_seat* seat) {
  return zwlr_data_control_manager_v1_get_data_device(static_cast<zwlr_data_control_manager_v1*>(manager), seat);
}

void destroyWlrDevice(void* device) {
  if (device) zwlr_data_control_device_v1_destroy(static_cast<zwlr_data_control_device_v1*>(device));
}

void* createWlrDataSource(void* manager) {
  return zwlr_data_control_manager_v1_create_data_source(static_cast<zwlr_data_control_manager_v1*>(manager));
}

void destroyWlrSource(void* source) {
  if (source) zwlr_data_control_source_v1_destroy(static_cast<zwlr_data_control_source_v1*>(source));
}

void wlrSourceOffer(void* source, const char* mimeType) {
  zwlr_data_control_source_v1_offer(static_cast<zwlr_data_control_source_v1*>(source), mimeType);
}

void wlrDeviceSetSelection(void* device, void* source) {
  zwlr_data_control_device_v1_set_selection(static_cast<zwlr_data_control_device_v1*>(device),
                                            static_cast<zwlr_data_control_source_v1*>(source));
}

void wlrDeviceSetPrimarySelection(void* device, void* source) {
  zwlr_data_control_device_v1_set_primary_selection(static_cast<zwlr_data_control_device_v1*>(device),
                                                    static_cast<zwlr_data_control_source_v1*>(source));
}

void destroyWlrOffer(void* offer) {
  if (offer) zwlr_data_control_offer_v1_destroy(static_cast<zwlr_data_control_offer_v1*>(offer));
}

void wlrOfferReceive(void* offer, const char* mimeType, int fd) {
  zwlr_data_control_offer_v1_receive(static_cast<zwlr_data_control_offer_v1*>(offer), mimeType, fd);
}

static const eh::wayland::DataControlOps* build_ext_ops() {
  static const eh::wayland::DataControlOps ops = {
      .managerInterfaceName = ext_data_control_manager_v1_interface.name,
      .bindManager = &bindExtManager,
      .destroyManager = &destroyExtManager,
      .getDataDevice = &getExtDataDevice,
      .destroyDevice = &destroyExtDevice,
      .createDataSource = &createExtDataSource,
      .destroySource = &destroyExtSource,
      .sourceOffer = &extSourceOffer,
      .deviceSetSelection = &extDeviceSetSelection,
      .deviceSetPrimarySelection = &extDeviceSetPrimarySelection,
      .destroyOffer = &destroyExtOffer,
      .offerReceive = &extOfferReceive,
  };
  return &ops;
}

static const eh::wayland::DataControlOps* build_wlr_ops() {
  static const eh::wayland::DataControlOps ops = {
      .managerInterfaceName = zwlr_data_control_manager_v1_interface.name,
      .bindManager = &bindWlrManager,
      .destroyManager = &destroyWlrManager,
      .getDataDevice = &getWlrDataDevice,
      .destroyDevice = &destroyWlrDevice,
      .createDataSource = &createWlrDataSource,
      .destroySource = &destroyWlrSource,
      .sourceOffer = &wlrSourceOffer,
      .deviceSetSelection = &wlrDeviceSetSelection,
      .deviceSetPrimarySelection = &wlrDeviceSetPrimarySelection,
      .destroyOffer = &destroyWlrOffer,
      .offerReceive = &wlrOfferReceive,
  };
  return &ops;
}

} // anonymous namespace

namespace eh::wayland {

const DataControlOps* ext_data_control_ops() { return build_ext_ops(); }
const DataControlOps* wlr_data_control_ops() { return build_wlr_ops(); }

// ── URI helpers ────────────────────────────────────────────────────────────

std::string ClipboardService::file_uri_for_path(const std::string& abs_path) {
  std::string out = "file://";
  for (unsigned char c : abs_path) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
        c == '/' || c == '-' || c == '_' || c == '.' || c == '~')
      out += static_cast<char>(c);
    else if (c == ' ')
      out += "%20";
    else {
      char buf[8];
      std::snprintf(buf, sizeof(buf), "%%%02X", c);
      out += buf;
    }
  }
  return out;
}

std::string ClipboardService::path_from_file_uri(const std::string& uri) {
  if (uri.rfind("file://", 0) != 0) return {};
  std::string p = uri.substr(7);
  if (p.empty()) return {};
  if (p[0] != '/') {
    auto slash = p.find('/');
    if (slash != std::string::npos) p = p.substr(slash);
    else return {};
  }
  std::string out;
  out.reserve(p.size());
  for (size_t i = 0; i < p.size(); ++i) {
    if (p[i] == '%' && i + 2 < p.size()) {
      int hi = hex_digit(p[i + 1]);
      int lo = hex_digit(p[i + 2]);
      if (hi >= 0 && lo >= 0) {
        out.push_back(static_cast<char>((hi << 4) | lo));
        i += 2;
        continue;
      }
    }
    out.push_back(p[i]);
  }
  return out;
}

std::string ClipboardService::canonical_abs_path(const std::string& path) {
  std::error_code ec;
  auto c = std::filesystem::weakly_canonical(path, ec);
  if (!ec && !c.empty()) return c.string();
  return std::filesystem::absolute(path, ec).string();
}

// ── Lifecycle ──────────────────────────────────────────────────────────────

ClipboardService::~ClipboardService() { cleanup(); }

bool ClipboardService::bind(void* manager, const DataControlOps* ops, wl_seat* seat, wl_display* display) {
  cleanup();
  if (!manager || !ops || !seat) return false;

  display_ = display;
  seat_ = seat;
  manager_ = manager;
  ops_ = ops;

  device_ = ops_->getDataDevice(manager_, seat_);
  if (!device_) {
    cleanup();
    return false;
  }

  if (ops_ == ext_data_control_ops()) {
    extDev_ = static_cast<ext_data_control_device_v1*>(device_);
    ext_data_control_device_v1_add_listener(extDev_, &kExtDeviceListener_, this);
  } else {
    wlrDev_ = static_cast<zwlr_data_control_device_v1*>(device_);
    zwlr_data_control_device_v1_add_listener(wlrDev_, &kWlrDeviceListener_, this);
  }

  notify_changed();
  return true;
}

void ClipboardService::cleanup() {
  selectionOffer_ = nullptr;
  selectionMimes_.clear();
  outgoingData_.clear();

  if (outgoingSource_) {
    if (ops_) ops_->destroySource(outgoingSource_);
    outgoingSource_ = nullptr;
  }

  if (device_) {
    if (ops_) ops_->destroyDevice(device_);
    device_ = nullptr;
  }

  extDev_ = nullptr;
  wlrDev_ = nullptr;
  display_ = nullptr;
  seat_ = nullptr;
  manager_ = nullptr;
  ops_ = nullptr;
}

bool ClipboardService::is_available() const noexcept {
  return seat_ && ops_ && device_;
}

void ClipboardService::notify_changed() {
  if (changedCb_) changedCb_();
}

// ── ext-data-control-v1 C thunks ─────────────────────────────────────────

void ClipboardService::ext_data_offer(void* data, ext_data_control_device_v1*, ext_data_control_offer_v1* offer) {
  auto* self = static_cast<ClipboardService*>(data);
  if (!offer) return;
  self->selectionMimes_.clear();
  ext_data_control_offer_v1_add_listener(offer, &kExtOfferListener_, self);
}

void ClipboardService::ext_selection(void* data, ext_data_control_device_v1*, ext_data_control_offer_v1* offer) {
  static_cast<ClipboardService*>(data)->handle_selection(offer);
}

void ClipboardService::ext_finished(void* data, ext_data_control_device_v1*) {
  static_cast<ClipboardService*>(data)->handle_device_finished();
}

void ClipboardService::ext_offer_offer(void* data, ext_data_control_offer_v1*, const char* mime) {
  static_cast<ClipboardService*>(data)->handle_offer_mime_type(mime);
}

void ClipboardService::ext_source_send(void* data, ext_data_control_source_v1*, const char* mime, int32_t fd) {
  static_cast<ClipboardService*>(data)->handle_source_send(mime, fd);
}

void ClipboardService::ext_source_cancelled(void* data, ext_data_control_source_v1*) {
  static_cast<ClipboardService*>(data)->handle_source_cancelled();
}

// ── wlr-data-control-unstable-v1 C thunks ────────────────────────────────

void ClipboardService::wlr_data_offer(void* data, zwlr_data_control_device_v1*, zwlr_data_control_offer_v1* offer) {
  auto* self = static_cast<ClipboardService*>(data);
  if (!offer) return;
  self->selectionMimes_.clear();
  zwlr_data_control_offer_v1_add_listener(offer, &kWlrOfferListener_, self);
}

void ClipboardService::wlr_selection(void* data, zwlr_data_control_device_v1*, zwlr_data_control_offer_v1* offer) {
  static_cast<ClipboardService*>(data)->handle_selection(offer);
}

void ClipboardService::wlr_finished(void* data, zwlr_data_control_device_v1*) {
  static_cast<ClipboardService*>(data)->handle_device_finished();
}

void ClipboardService::wlr_offer_offer(void* data, zwlr_data_control_offer_v1*, const char* mime) {
  static_cast<ClipboardService*>(data)->handle_offer_mime_type(mime);
}

void ClipboardService::wlr_source_send(void* data, zwlr_data_control_source_v1*, const char* mime, int32_t fd) {
  static_cast<ClipboardService*>(data)->handle_source_send(mime, fd);
}

void ClipboardService::wlr_source_cancelled(void* data, zwlr_data_control_source_v1*) {
  static_cast<ClipboardService*>(data)->handle_source_cancelled();
}

// ── Internal protocol handlers ───────────────────────────────────────────

void ClipboardService::handle_offer_mime_type(const char* mime) {
  if (!mime) return;
  selectionMimes_.emplace_back(mime);
}

void ClipboardService::handle_selection(void* offer) {
  if (!offer) {
    // null offer → no selection; clear everything
    selectionOffer_ = nullptr;
    selectionMimes_.clear();
  } else {
    // MIME types have already been populated by offer.offer events
    // before selection fires — do NOT clear selectionMimes_.
    selectionOffer_ = offer;
  }
  notify_changed();
}

void ClipboardService::handle_device_finished() {
  cleanup();
  notify_changed();
}

void ClipboardService::handle_source_send(const char* mime, int fd) {
  if (!mime || fd < 0) {
    if (fd >= 0) close(fd);
    return;
  }

  auto it = outgoingData_.find(mime);
  if (it != outgoingData_.end() && !it->second.empty()) {
    const auto& bytes = it->second;
    const char* ptr = bytes.data();
    size_t remaining = bytes.size();
    while (remaining > 0) {
      ssize_t n = write(fd, ptr, remaining);
      if (n < 0) {
        if (errno == EINTR) continue;
        break;
      }
      ptr += static_cast<std::size_t>(n);
      remaining -= static_cast<std::size_t>(n);
    }
  }
  close(fd);
  if (onSourceSend_) onSourceSend_();
}

void ClipboardService::handle_source_cancelled() {
  if (outgoingSource_) {
    if (ops_) ops_->destroySource(outgoingSource_);
    outgoingSource_ = nullptr;
  }
  outgoingData_.clear();
  notify_changed();
}

// ── Source creation ───────────────────────────────────────────────────────

void ClipboardService::build_source_and_set_selection() {
  // Destroy the previous outgoing source without touching outgoingData_.
  if (outgoingSource_) {
    if (ops_) ops_->destroySource(outgoingSource_);
    outgoingSource_ = nullptr;
  }

  auto* src = ops_->createDataSource(manager_);
  if (!src) return;
  outgoingSource_ = src;

  if (ops_ == ext_data_control_ops()) {
    ext_data_control_source_v1_add_listener(static_cast<ext_data_control_source_v1*>(src),
                                            &kExtSourceListener_, this);
  } else {
    zwlr_data_control_source_v1_add_listener(static_cast<zwlr_data_control_source_v1*>(src),
                                             &kWlrSourceListener_, this);
  }

  for (const auto& [mime, _] : outgoingData_) {
    ops_->sourceOffer(src, mime.c_str());
  }

  ops_->deviceSetSelection(device_, src);

  // Also set primary selection so selected text works with middle-click paste
  if (ops_->deviceSetPrimarySelection) {
    ops_->deviceSetPrimarySelection(device_, src);
  }

  // Flush so the new selection is visible to other clients immediately,
  // even if the caller's event loop hasn't dispatched yet.
  if (display_) wl_display_flush(display_);

  notify_changed();
}

// ── Copy operations ───────────────────────────────────────────────────────

bool ClipboardService::copy_data(std::string mime_type, std::string data) {
  if (!is_available()) return false;

  std::vector<char> bytes(data.begin(), data.end());
  outgoingData_[std::move(mime_type)] = std::move(bytes);

  build_source_and_set_selection();
  return true;
}

bool ClipboardService::copy_data_multi(std::vector<std::pair<std::string, std::string>> items) {
  if (!is_available()) return false;

  outgoingData_.clear();
  for (auto& [mime, data] : items) {
    outgoingData_[std::move(mime)] = std::vector<char>(data.begin(), data.end());
  }

  build_source_and_set_selection();
  return true;
}

bool ClipboardService::copy_text(std::string text) {
  if (!is_available()) return false;

  outgoingData_.clear();
  std::vector<char> bytes(text.begin(), text.end());
  outgoingData_[std::string(kTextUtf8)] = bytes;
  outgoingData_[std::string(kTextPlain)] = std::move(bytes);

  build_source_and_set_selection();
  return true;
}

bool ClipboardService::copy_files(bool cut, const std::vector<std::string>& abs_paths) {
  if (!is_available() || abs_paths.empty()) return false;

  // Build x-special/gnome-copied-files data
  std::string gnome = cut ? "cut\n" : "copy\n";
  std::string uri_list;
  std::string plain;
  for (const auto& p : abs_paths) {
    std::string canon = canonical_abs_path(p);
    if (canon.empty()) continue;
    std::string uri = file_uri_for_path(canon);
    gnome += uri + "\n";
    uri_list += uri + "\r\n";
    if (!plain.empty()) plain += "\n";
    plain += canon;
  }
  if (gnome.size() <= 4 && gnome[0] != '/' && abs_paths.size() > 1) return false;
  // gnome at least has "cut\n" or "copy\n" (4 chars) — if only header, no valid paths
  if (gnome.size() < 6) return false;

  outgoingData_.clear();
  outgoingData_[std::string(kGnomeCopiedFiles)] = std::vector<char>(gnome.begin(), gnome.end());
  outgoingData_[std::string(kUriList)] = std::vector<char>(uri_list.begin(), uri_list.end());
  outgoingData_[std::string(kTextPlain)] = std::vector<char>(plain.begin(), plain.end());

  build_source_and_set_selection();
  return true;
}

// ── Read operations ───────────────────────────────────────────────────────

bool ClipboardService::selection_supports_text() const {
  for (const auto& m : selectionMimes_) {
    if (m == kTextUtf8 || m == kTextPlain || m == kUtf8String) return true;
  }
  return false;
}

bool ClipboardService::selection_has_mime(const std::string& mime_type) const {
  for (const auto& m : selectionMimes_) {
    if (m == mime_type) return true;
  }
  return false;
}

std::string ClipboardService::receive_offer_as_text(wl_display* display) const {
  if (!selectionOffer_ || !selection_supports_text()) return {};

  std::string mime = std::string(kTextUtf8);
  bool hasUtf8 = false;
  for (const auto& m : selectionMimes_) if (m == kTextUtf8) hasUtf8 = true;
  if (!hasUtf8 && !selectionMimes_.empty()) mime = selectionMimes_.front();

  return receive_offer_as_mime(mime, display);
}

std::string ClipboardService::receive_offer_as_mime(const std::string& mime, wl_display* display) const {
  if (!selectionOffer_ || mime.empty()) return {};

  int fds[2]{-1, -1};
  if (pipe2(fds, O_CLOEXEC) != 0) return {};

  if (ops_) ops_->offerReceive(selectionOffer_, mime.c_str(), fds[1]);
  close_fd(fds[1]);

  if (display) {
    (void)wl_display_flush(display);
    (void)wl_display_roundtrip(display);
  }

  std::string out;
  std::array<char, 4096> buf{};
  for (;;) {
    pollfd pfd{fds[0], POLLIN, 0};
    int pr = poll(&pfd, 1, 5000);
    if (pr <= 0) break;
    ssize_t r = read(fds[0], buf.data(), buf.size());
    if (r <= 0) break;
    out.append(buf.data(), static_cast<std::size_t>(r));
    if (out.size() > (4u * 1024u * 1024u)) break;
  }
  close_fd(fds[0]);
  return out;
}

std::string ClipboardService::read_selection_text(wl_display* display) {
  return receive_offer_as_text(display);
}

std::string ClipboardService::read_data(const std::string& mime_type, wl_display* display) {
  if (!selection_has_mime(mime_type)) return {};
  return receive_offer_as_mime(mime_type, display);
}

ClipboardFiles ClipboardService::read_files(wl_display* display) {
  ClipboardFiles result;

  // Dispatch any pending selection events so selectionMimes_ is
  // populated before we check it. Non-blocking — only processes
  // events already buffered from the socket.
  if (display) {
    wl_display_flush(display);
    wl_display_dispatch_pending(display);
  }

  // 1) Try x-special/gnome-copied-files
  if (selection_has_mime(std::string(kGnomeCopiedFiles))) {
    std::string raw = receive_offer_as_mime(std::string(kGnomeCopiedFiles), display);
    if (!raw.empty()) {
      std::istringstream iss(raw);
      std::string line;
      std::vector<std::string> lines;
      while (std::getline(iss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(line);
      }
      size_t i = 0;
      if (i < lines.size() && lines[i] == "x-special/nautilus-clipboard") i++;
      if (i < lines.size() && (lines[i] == "cut" || lines[i] == "copy")) {
        result.is_cut = (lines[i] == "cut");
        i++;
        for (; i < lines.size(); ++i) {
          if (lines[i].empty()) continue;
          std::string p = path_from_file_uri(lines[i]);
          if (p.empty() && !lines[i].empty() && lines[i][0] == '/') p = lines[i];
          if (!p.empty()) result.paths.push_back(canonical_abs_path(p));
        }
        if (!result.paths.empty()) return result;
      }
    }
  }

  // 2) Fall back to text/uri-list
  if (selection_has_mime(std::string(kUriList))) {
    std::string raw = receive_offer_as_mime(std::string(kUriList), display);
    if (!raw.empty()) {
      std::istringstream iss(raw);
      std::string line;
      while (std::getline(iss, line)) {
        // trim whitespace
        size_t a = 0, b = line.size();
        while (a < b && (line[a] == ' ' || line[a] == '\t' || line[a] == '\r')) a++;
        while (b > a && (line[b - 1] == ' ' || line[b - 1] == '\t' || line[b - 1] == '\r')) b--;
        if (a >= b) continue;
        line = line.substr(a, b - a);
        if (line.empty() || line[0] == '#') continue;
        std::string p = path_from_file_uri(line);
        if (p.empty() && line[0] == '/') p = line;
        if (!p.empty()) result.paths.push_back(canonical_abs_path(p));
      }
      if (!result.paths.empty()) return result;
    }
  }

  // 3) Fall back to text/plain
  {
    std::string raw = receive_offer_as_text(display);
    if (!raw.empty()) {
      std::istringstream iss(raw);
      std::string line;
      while (std::getline(iss, line)) {
        size_t a = 0, b = line.size();
        while (a < b && (line[a] == ' ' || line[a] == '\t' || line[a] == '\r')) a++;
        while (b > a && (line[b - 1] == ' ' || line[b - 1] == '\t' || line[b - 1] == '\r')) b--;
        if (a >= b) continue;
        line = line.substr(a, b - a);
        if (line.empty()) continue;
        std::string p = path_from_file_uri(line);
        if (p.empty() && line[0] == '/') p = line;
        if (!p.empty()) result.paths.push_back(canonical_abs_path(p));
      }
    }
  }

  return result;
}

}
