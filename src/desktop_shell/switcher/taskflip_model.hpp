#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct zwlr_foreign_toplevel_handle_v1;

namespace eh::shell::taskflip {

struct Entry {
  zwlr_foreign_toplevel_handle_v1* handle = nullptr;
  std::string appId;
  std::string title;
  std::uint64_t serial = 0;
};

class Model {
 public:
  void rebuild(const std::vector<Entry>& current);
  void noteActivated(zwlr_foreign_toplevel_handle_v1* handle);
  void next();
  void prev();
  void resetSelection();
  [[nodiscard]] const Entry* selected() const;
  [[nodiscard]] std::size_t size() const { return entries_.size(); }
  [[nodiscard]] std::size_t selectedIndex() const { return selected_; }
  [[nodiscard]] const std::vector<Entry>& entries() const { return entries_; }

 private:
  std::vector<Entry> entries_;
  std::size_t selected_ = 0;
};

}
