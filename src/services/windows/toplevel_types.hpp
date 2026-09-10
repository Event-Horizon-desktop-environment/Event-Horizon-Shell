#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Shared wire types for the shell-owned toplevel/window service. The shell
// tracks foreign toplevels on its main Wayland connection (the 2026-08-05
// spike proved `ext-foreign-toplevel-list-v1` is snapshot-only on secondary
// connections) and broadcasts deltas to every component over the IPC bus.
// Split-out components consume them with `ToplevelClient` instead of borrowing
// the dock's toplevel trackers.
//
// Wire encoding (self-delimiting, length-prefixed, robust to any characters in
// app_id/title, including tabs and newlines):
//
//   record  := id ";" flags ";" appLen ";" appBytes titleLen ";" titleBytes
//   list    := count "\n" record{count}
//
// All numeric fields are ASCII decimal terminated by ';' (or '\n' for count).
// The `key` field on ToplevelRecord is server-side only and never hits the wire.

namespace eh::windows {

enum ToplevelFlag : uint32_t {
  kActivated  = 1u << 0,
  kMinimized  = 1u << 1,
  kMaximized  = 1u << 2,
  kFullscreen = 1u << 3,
};

struct ToplevelRecord {
  std::string key;      // stable identity across updates (bridge provides, not sent)
  std::uint64_t id = 0; // wire id, assigned by ToplevelService on first appearance
  std::string appId;
  std::string title;
  std::uint32_t flags = 0;
};

// Bus topics + command for the toplevel service.
inline constexpr char kToplevelCreated[] = "toplevel.created";
inline constexpr char kToplevelUpdated[] = "toplevel.updated";
inline constexpr char kToplevelClosed[]  = "toplevel.closed";
inline constexpr char kToplevelListCmd[] = "toplevel.list";

// Append the wire encoding of `r` to `out` (id/flags/appId/title; key omitted).
void toplevel_encode_record(const ToplevelRecord& r, std::string& out);

// Parse one record from `data` at `offset`. On success returns true and sets
// `consumed` to the number of bytes the record occupied.
bool toplevel_decode_record(const std::string& data, size_t offset,
                            ToplevelRecord& out, size_t& consumed);

// Encode/decode a whole snapshot (count-prefixed).
void toplevel_encode_list(const std::vector<ToplevelRecord>& list, std::string& out);
bool toplevel_decode_list(const std::string& data, std::vector<ToplevelRecord>& out);

} // namespace eh::windows
