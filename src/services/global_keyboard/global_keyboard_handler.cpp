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

  std::vector<int> fds;
  int inaccessible = 0;
  int non_keyboards = 0;
  for (int i = 0; i < 64; ++i) {
    const std::string path = "/dev/input/event" + std::to_string(i);
    const int fd = ::open(path.c_str(), O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
      if (errno != ENOENT) {
        ++inaccessible;
        log("open fail: " + path + " (" + std::string(std::strerror(errno)) + ")");
      }
      continue;
    }
    KeyboardProbeResult r = probe_keyboard(fd);
    if (!r.is_super) {
      ++non_keyboards;
      log("skip: " + path + " name=" + r.name + " has_ev_key=" + std::to_string(r.has_ev_key) +
          " has_leftmeta=" + std::to_string(r.has_leftmeta));
      ::close(fd);
      continue;
    }
    fds.push_back(fd);
    log("opened: " + path + " name=" + r.name);
  }

  if (fds.empty()) {
    log("no evdev keyboard devices found — scan: inaccessible=" + std::to_string(inaccessible) +
        " non_keyboards=" + std::to_string(non_keyboards) +
        " (this process is likely not in the `input` group / lacks evdev ACLs)");
    active_ = false;
    return;
  }

  int epfd = ::epoll_create1(EPOLL_CLOEXEC);
  if (epfd < 0) {
    log("epoll_create1 failed: " + std::string(std::strerror(errno)));
    for (int fd : fds) ::close(fd);
    active_ = false;
    return;
  }

  for (int fd : fds) {
    struct epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = fd;
    ::epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev);
  }

  bool super_down = false;
  bool cancel = false;
  bool super_s_fired = false;
  bool shift_down = false;

  struct epoll_event events[8];
  while (active_) {
    int n = ::epoll_wait(epfd, events, 8, 100);
    if (n < 0) {
      if (errno == EINTR) continue;
      break;
    }

    for (int i = 0; i < n; ++i) {
      struct input_event ev;
      while (::read(events[i].data.fd, &ev, sizeof(ev)) == static_cast<ssize_t>(sizeof(ev))) {
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
    }
  }

  for (int fd : fds) ::close(fd);
  ::close(epfd);
  log("evdev thread exiting");
}

} // namespace eh::service
