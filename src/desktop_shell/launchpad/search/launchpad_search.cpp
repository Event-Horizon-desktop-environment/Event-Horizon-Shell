#include "desktop_shell/launchpad/search/launchpad_search.hpp"

#include "desktop_shell/widgets/app_drawer/list/desktop_list.hpp"
#include "desktop_shell/desktop/entries/desktop_entries.hpp"
#include "desktop_shell/common/fs/shell_paths.hpp"
#include "desktop_shell/common/bench/shell_bench.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstring>
#include <iostream>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>

namespace eh::app_drawer {
using namespace eh::shell::dock::app_drawer;
}

namespace {

std::string toLower(std::string_view s) {
  std::string r(s);
  std::transform(r.begin(), r.end(), r.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return r;
}

std::string trim(std::string_view s) {
   
  size_t a = 0;
  while (a < s.size() && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r')) ++a;
  size_t b = s.size();
  while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) --b;
  return std::string(s.substr(a, b - a));
}

bool entry_listable(const eh::app_drawer::DesktopEntry& e) { return !e.noDisplay && !e.hidden; }

int lp_fuzzy_score_direct(std::string_view patLower, std::string_view textLower,
                          std::string_view textOriginal) {
   
  if (patLower.empty()) return 1;
  if (textLower.empty()) return 0;

  {
    std::size_t ti = 0;
    for (std::size_t pi = 0; pi < patLower.size(); ++pi) {
      bool found = false;
      while (ti < textLower.size()) {
        if (textLower[ti] == patLower[pi]) {
          ++ti;
          found = true;
          break;
        }
        ++ti;
      }
      if (!found) return 0;
    }
  }

  auto is_word_boundary = [](std::string_view t, std::size_t pos) {
    if (pos == 0) return true;
    const char prev = t[pos - 1];
    const char curr = t[pos];
    if (prev == ' ' || prev == '-' || prev == '_' || prev == '.') return true;
    if (std::islower(static_cast<unsigned char>(prev)) && std::isupper(static_cast<unsigned char>(curr))) return true;
    return false;
  };

  int total_score = 0;
  std::size_t pat_idx = 0;
  std::size_t text_idx = 0;
  int consecutive = 0;

  while (pat_idx < patLower.size() && text_idx < textLower.size()) {
    if (textLower[text_idx] == patLower[pat_idx]) {
      int char_score = 1;
      if (is_word_boundary(textOriginal, text_idx)) char_score += 10;
      if (consecutive > 0) char_score += 4 * consecutive;
      if (text_idx == 0) char_score += 8;
      if (text_idx < textOriginal.size() && textOriginal[text_idx] == patLower[pat_idx]) char_score += 1;
      total_score += char_score;
      ++consecutive;
      ++pat_idx;
    } else {
      consecutive = 0;
    }
    ++text_idx;
  }
  if (pat_idx < patLower.size()) return 0;
  if (textOriginal.size() <= patLower.size() + 2) total_score += 15;
  return total_score;
}

int lp_score_list_direct(std::string_view patLower, std::string_view listLower) {
   
  int best = 0;
  std::size_t start = 0;
  while (start < listLower.size()) {
    const auto semi = listLower.find(';', start);
    const auto word = (semi == std::string_view::npos) ? listLower.substr(start) : listLower.substr(start, semi - start);
    if (!word.empty()) best = std::max(best, lp_fuzzy_score_direct(patLower, word, word));
    if (semi == std::string_view::npos) break;
    start = semi + 1;
  }
  return best;
}

int lp_entry_score_pre(std::string_view patLower, const eh::app_drawer::DesktopEntry& e) {
   
  if (patLower.empty()) return 1;
  const int name_score = lp_fuzzy_score_direct(patLower, e.nameLower, e.name) * 3;
  const int generic_score = lp_fuzzy_score_direct(patLower, e.genericNameLower, e.genericName) * 2;
  const int comment_score = lp_fuzzy_score_direct(patLower, e.commentLower, e.comment) * 2;
  const int keyword_score = lp_score_list_direct(patLower, e.keywordsLower);
  const int cat_score = lp_score_list_direct(patLower, e.categoriesLower);
  return std::max({name_score, generic_score, comment_score, keyword_score, cat_score});
}

LaunchpadHit lp_hit_from_entry(const eh::app_drawer::DesktopEntry& e) {
   
  LaunchpadHit h;
  h.path = e.path;
  h.name = e.name;
  h.exec = e.exec;
  h.iconKey = e.icon.empty() ? std::string("application-x-executable") : e.icon;
  return h;
}

std::optional<eh::app_drawer::DesktopEntry> lp_entry_for_pin(const std::vector<eh::app_drawer::DesktopEntry>& catalog,
                                                             const std::string& pin_raw) {
   
  if (auto resolved = find_desktop_file_for_appid(pin_raw)) {
    for (const auto& e : catalog) {
      if (e.path == *resolved) return e;
    }
  }
  const std::string nw = eh::shell::paths::normalize_desktop_app_id(pin_raw);
  if (nw.empty() || nw == "unknown") return std::nullopt;
  for (const auto& e : catalog) {
    if (e.noDisplay || e.hidden) continue;
    const std::string stem =
        eh::shell::paths::normalize_desktop_app_id(eh_app_drawer_desktop_stem_from_path(e.path));
    if (stem == nw) return e;
  }
  return std::nullopt;
}

}

namespace eh::shell::launchpad {

void launchpad_search_run(std::string_view query_utf8, const std::vector<std::string>* pins_ordered,
                          std::vector<LaunchpadHit>* out) {
   
  if (!out) return;
  out->clear();

  const auto t0 = ShellBenchClock::now();

  const std::string q_owned = trim(query_utf8);
  const std::string_view q = q_owned;

  const auto& local = eh::app_drawer::get_cached_entries();

  std::vector<std::pair<int, size_t>> base_ix;
  if (q.empty()) {
    base_ix.reserve(local.size());
    for (size_t i = 0; i < local.size(); ++i) {
      if (!entry_listable(local[i])) continue;
      base_ix.emplace_back(1, i);
    }
    std::sort(base_ix.begin(), base_ix.end(), [&local](const auto& a, const auto& b) {
      const int cmp = strcasecmp(local[a.second].name.c_str(), local[b.second].name.c_str());
      if (cmp != 0) return cmp < 0;
      return local[a.second].path < local[b.second].path;
    });
  } else {
    base_ix.reserve(local.size());
    std::string patLower = toLower(q_owned);
    for (size_t i = 0; i < local.size(); ++i) {
      if (!entry_listable(local[i])) continue;
      const int s = lp_entry_score_pre(patLower, local[i]);
      if (s > 0) base_ix.emplace_back(s, i);
    }
    std::sort(base_ix.begin(), base_ix.end(), [&local](const auto& a, const auto& b) {
      if (a.first != b.first) return a.first > b.first;
      const int cmp = strcasecmp(local[a.second].name.c_str(), local[b.second].name.c_str());
      return cmp < 0;
    });
  }

  if (!pins_ordered || pins_ordered->empty()) {
    out->reserve(base_ix.size());
    for (const auto& pr : base_ix) out->push_back(lp_hit_from_entry(local[pr.second]));
    return;
  }

  std::unordered_set<std::string> seen;
  seen.reserve(base_ix.size() + pins_ordered->size());
  std::vector<LaunchpadHit> merged;
  merged.reserve(base_ix.size() + pins_ordered->size());

  std::string patLower = toLower(q_owned);
  for (const std::string& pin_raw : *pins_ordered) {
    auto ent = lp_entry_for_pin(local, pin_raw);
    if (!ent.has_value() || !entry_listable(*ent)) continue;
    const int sc = q.empty() ? 1 : lp_entry_score_pre(patLower, *ent);
    if (sc <= 0) continue;
    if (seen.count(ent->path) != 0) continue;
    seen.insert(ent->path);
    merged.push_back(lp_hit_from_entry(*ent));
  }

  for (const auto& pr : base_ix) {
    const std::string& pth = local[pr.second].path;
    if (seen.count(pth) != 0) continue;
    seen.insert(pth);
    merged.push_back(lp_hit_from_entry(local[pr.second]));
  }

  *out = std::move(merged);
  {
    const auto us = std::chrono::duration_cast<std::chrono::microseconds>(ShellBenchClock::now() - t0).count();
    std::cerr << "[search-bench] lp_search_run query=\"" << q_owned
              << "\" hits=" << out->size() << " catalog=" << local.size()
              << " " << us << "us" << std::endl;
  }
}

void launchpad_search_filter(std::string_view query_utf8,
                             const std::vector<size_t>& previous_cat_indices,
                             const std::vector<std::string>* pins_ordered,
                             std::vector<LaunchpadHit>* out) {
  if (!out) return;
  out->clear();

  const auto t0 = ShellBenchClock::now();

  const std::string q_owned = trim(query_utf8);
  const std::string_view q = q_owned;
  if (q.empty()) return;

  const auto& local = eh::app_drawer::get_cached_entries();

  std::vector<std::pair<int, size_t>> filtered;
  filtered.reserve(previous_cat_indices.size());
  std::string patLower = toLower(q_owned);
  for (size_t idx : previous_cat_indices) {
    if (idx >= local.size()) continue;
    if (!entry_listable(local[idx])) continue;
    const int s = lp_entry_score_pre(patLower, local[idx]);
    if (s > 0) filtered.emplace_back(s, idx);
  }

  std::sort(filtered.begin(), filtered.end(), [&local](const auto& a, const auto& b) {
    if (a.first != b.first) return a.first > b.first;
    const int cmp = strcasecmp(local[a.second].name.c_str(), local[b.second].name.c_str());
    return cmp < 0;
  });

  if (!pins_ordered || pins_ordered->empty()) {
    out->reserve(filtered.size());
    for (const auto& pr : filtered) out->push_back(lp_hit_from_entry(local[pr.second]));
    return;
  }

  std::unordered_set<std::string> seen;
  seen.reserve(filtered.size() + pins_ordered->size());
  std::vector<LaunchpadHit> merged;
  merged.reserve(filtered.size() + pins_ordered->size());

  for (const std::string& pin_raw : *pins_ordered) {
    auto ent = lp_entry_for_pin(local, pin_raw);
    if (!ent.has_value() || !entry_listable(*ent)) continue;
    const int sc = lp_entry_score_pre(patLower, *ent);
    if (sc <= 0) continue;
    if (seen.count(ent->path) != 0) continue;
    seen.insert(ent->path);
    merged.push_back(lp_hit_from_entry(*ent));
  }

  for (const auto& pr : filtered) {
    const std::string& pth = local[pr.second].path;
    if (seen.count(pth) != 0) continue;
    seen.insert(pth);
    merged.push_back(lp_hit_from_entry(local[pr.second]));
  }

  *out = std::move(merged);
  {
    const auto us = std::chrono::duration_cast<std::chrono::microseconds>(ShellBenchClock::now() - t0).count();
    std::cerr << "[search-bench] lp_search_filter query=\"" << q_owned
              << "\" hits=" << out->size() << " prev_ix=" << previous_cat_indices.size()
              << " " << us << "us" << std::endl;
  }
}

}
