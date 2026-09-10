// Compile-time stand-in for Abseil's flat_hash_map, used by the vendored
// material-color-utilities quantizer. Maps to std::unordered_map so no
// Abseil build (system or vendored) is needed at compile time or runtime.
#pragma once

#include <cstddef>
#include <functional>
#include <unordered_map>
#include <utility>

namespace absl {

template <
    typename Key,
    typename Value,
    typename Hash = std::hash<Key>,
    typename Eq = std::equal_to<Key>>
using flat_hash_map = std::unordered_map<Key, Value, Hash, Eq>;

}  // namespace absl
