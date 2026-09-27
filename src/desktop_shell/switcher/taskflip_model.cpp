#include "desktop_shell/switcher/taskflip_model.hpp"

namespace eh::shell::taskflip {

void Model::rebuild(const std::vector<Entry>& current) {
  zwlr_foreign_toplevel_handle_v1* selHandle =
      (selected_ < entries_.size()) ? entries_[selected_].handle : nullptr;
  std::vector<Entry> merged;
  merged.reserve(current.size());
  for (const auto& e : entries_) {
    for (const auto& c : current) {
      if (c.handle == e.handle) {
        merged.push_back(c);
        break;
      }
    }
  }
  for (const auto& c : current) {
    bool known = false;
    for (const auto& m : merged) {
      if (m.handle == c.handle) {
        known = true;
        break;
      }
    }
    if (!known) merged.push_back(c);
  }
  entries_ = std::move(merged);
  selected_ = 0;
  if (selHandle) {
    for (std::size_t i = 0; i < entries_.size(); ++i) {
      if (entries_[i].handle == selHandle) {
        selected_ = i;
        break;
      }
    }
  }
  if (!entries_.empty() && selected_ >= entries_.size()) selected_ = 0;
}

void Model::noteActivated(zwlr_foreign_toplevel_handle_v1* handle) {
  if (!handle) return;
  for (std::size_t i = 0; i < entries_.size(); ++i) {
    if (entries_[i].handle == handle) {
      Entry e = std::move(entries_[i]);
      entries_.erase(entries_.begin() + (std::ptrdiff_t)i);
      entries_.insert(entries_.begin(), std::move(e));
      selected_ = 0;
      return;
    }
  }
}

void Model::next() {
  if (entries_.empty()) return;
  selected_ = (selected_ + 1) % entries_.size();
}

void Model::prev() {
  if (entries_.empty()) return;
  selected_ = (selected_ + entries_.size() - 1) % entries_.size();
}

void Model::resetSelection() {
  selected_ = 0;
}

const Entry* Model::selected() const {
  if (entries_.empty() || selected_ >= entries_.size()) return nullptr;
  return &entries_[selected_];
}

}
