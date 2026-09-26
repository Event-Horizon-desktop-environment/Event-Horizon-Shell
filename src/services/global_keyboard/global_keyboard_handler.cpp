#include "services/global_keyboard/global_keyboard_handler.hpp"

#include "bootstrap/thread/thread_dispatch.hpp"
#include "desktop_shell/common/log/debug_log.hpp"

#include <fcntl.h>
#include <linux/input.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace eh::service {

namespace {

void log(const std::string& msg) {
  debug_log("keybinds", "%s", msg.c_str());
}

struct KeyboardProbeResult {
  bool is_super = false;
  bool has_ev_key = false;
  bool has_leftmeta = false;
  char name[256] = {};
};

KeyboardProbeResult probe_keyboard(int fd) {
  KeyboardProbeResult r{};
  ::ioctl(fd, EVIOCGNAME(sizeof(r.name)), r.name);

  unsigned long ev_bits[EV_MAX / (sizeof(unsigned long) * 8) + 1] = {};
  if (::ioctl(fd, EVIOCGBIT(0, sizeof(ev_bits)), ev_bits) < 0) return r;

  const unsigned long ev_key_bit = EV_KEY;
  r.has_ev_key = (ev_bits[ev_key_bit / (sizeof(unsigned long) * 8)] &
                  (1UL << (ev_key_bit % (sizeof(unsigned long) * 8)))) != 0;
  if (!r.has_ev_key) return r;

  unsigned long key_bits[KEY_MAX / (sizeof(unsigned long) * 8) + 1] = {};
  if (::ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(key_bits)), key_bits) < 0) return r;

  r.has_leftmeta = (key_bits[KEY_LEFTMETA / (sizeof(unsigned long) * 8)] &
                    (1UL << (KEY_LEFTMETA % (sizeof(unsigned long) * 8)))) != 0;
  r.is_super = r.has_leftmeta;
  return r;
}

} // namespace

GlobalKeyboardHandler::~GlobalKeyboardHandler() { cleanup(); }

bool GlobalKeyboardHandler::init(ActivateFn startMenuFn, ActivateFn settingsFn) {
  log("=== init(evdev) called ===");
  if (active_) cleanup();
  startMenuFn_ = std::move(startMenuFn);
  settingsFn_ = std::move(settingsFn);
  bool found = false;
  for (int i = 0; i < 64; ++i) {
    const std::string path = "/dev/input/event" + std::to_string(i);
    const int fd = ::open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) continue;
    KeyboardProbeResult r = probe_keyboard(fd);
    ::close(fd);
    if (r.is_super) {
      found = true;
      break;
    }
  }
  if (!found) {
    log("no evdev keyboard devices found (this process is likely not in the `input` group / lacks evdev ACLs)");
    startMenuFn_ = nullptr;
    settingsFn_ = nullptr;
    return false;
  }
  active_ = true;
  thread_ = std::make_unique<std::thread>(&GlobalKeyboardHandler::evdev_thread_fn, this);
  return true;
}

void GlobalKeyboardHandler::cleanup() {
  if (!active_) return;
  active_ = false;
  if (thread_ && thread_->joinable()) thread_->join();
  thread_.reset();
  startMenuFn_ = nullptr;
  settingsFn_ = nullptr;
  log("cleaned up");
}

void GlobalKeyboardHandler::evdev_thread_fn() {
  log("evdev thread started");

  auto menuFn = startMenuFn_;
  auto setsFn = settingsFn_;

  int epfd = ::epoll_create1(EPOLL_CLOEXEC);
  if (epfd < 0) {
    log("epoll_create1 failed: " + std::string(std::strerror(errno)));
    active_ = false;
    return;
  }

  std::map<std::string, int> open_devs;
  auto scan_new_devices = [&]() {
    for (int i = 0; i < 64; ++i) {
      const std::string path = "/dev/input/event" + std::to_string(i);
      if (open_devs.count(path) != 0) continue;
      const int fd = ::open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
      if (fd < 0) continue;
      KeyboardProbeResult r = probe_keyboard(fd);
      if (!r.is_super) {
        ::close(fd);
        continue;
      }
      struct epoll_event ev{};
      ev.events = EPOLLIN;
      ev.data.fd = fd;
      if (::epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev) != 0) {
        ::close(fd);
        continue;
      }
      open_devs.emplace(path, fd);
      log("opened: " + path + " name=" + r.name);
    }
  };

  scan_new_devices();
  if (open_devs.empty()) {
    ::close(epfd);
    active_ = false;
    return;
  }

  bool super_down = false;
  bool cancel = false;
  bool super_s_fired = false;
  bool shift_down = false;

  struct epoll_event events[8];
  int idle_rounds = 0;
  while (active_) {
    int n = ::epoll_wait(epfd, events, 8, 100);
    if (n < 0) {
      if (errno == EINTR) continue;
      break;
    }
    if (n == 0 && ++idle_rounds >= 50) {
      idle_rounds = 0;
      scan_new_devices();
      if (open_devs.empty()) break;
      continue;
    }

    std::vector<std::string> dead;
    for (int i = 0; i < n; ++i) {
      if (events[i].events & (EPOLLERR | EPOLLHUP)) {
        for (const auto& [path, fd] : open_devs) {
          if (fd == events[i].data.fd) {
            dead.push_back(path);
            break;
          }
        }
        continue;
      }
      struct input_event ev;
      bool dev_dead = false;
      ssize_t nread = 0;
      while ((nread = ::read(events[i].data.fd, &ev, sizeof(ev))) == static_cast<ssize_t>(sizeof(ev))) {
        if (ev.type != EV_KEY) continue;

        if (ev.code == KEY_LEFTMETA || ev.code == KEY_RIGHTMETA) {
          if (ev.value == 1) {
            super_down = true;
            cancel = false;
            super_s_fired = false;
            log("SUPER down");
          } else if (ev.value == 0) {
            log("SUPER up (cancel=" + std::to_string(cancel) + " super_s_fired=" + std::to_string(super_s_fired) + ")");
            if (super_down && !cancel && !super_s_fired && menuFn) {
              log("FIRING startMenuFn_ (menu-toggle)");
              DeferredCall::callLater(menuFn);
            }
            super_down = false;
            cancel = false;
            super_s_fired = false;
          }
          continue;
        }

        if (ev.code == KEY_LEFTSHIFT || ev.code == KEY_RIGHTSHIFT) {
          shift_down = (ev.value != 0);
          continue;
        }

        if (ev.value == 1) {
          cancel = true;
          log("key press code=" + std::to_string(ev.code) + " super_down=" + std::to_string(super_down));
          if (super_down && ev.code == KEY_S && !super_s_fired && !shift_down && setsFn) {
            super_s_fired = true;
            log("FIRING settingsFn_ (Super+S)");
            DeferredCall::callLater(setsFn);
          }
        }
      }
      if (nread < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) dev_dead = true;
      if (dev_dead) {
        for (const auto& [path, fd] : open_devs) {
          if (fd == events[i].data.fd) {
            dead.push_back(path);
            break;
          }
        }
      }
    }
    for (const auto& path : dead) {
      auto it = open_devs.find(path);
      if (it != open_devs.end()) {
        ::epoll_ctl(epfd, EPOLL_CTL_DEL, it->second, nullptr);
        ::close(it->second);
        open_devs.erase(it);
      }
    }
    if (open_devs.empty()) break;
  }

  for (const auto& [path, fd] : open_devs) {
    (void)path;
    ::close(fd);
  }
  ::close(epfd);
  log("evdev thread exiting");
}

} // namespace eh::service
