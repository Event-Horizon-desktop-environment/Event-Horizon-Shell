#pragma once

#include <string>
#include <vector>

std::vector<std::string> pin_identity_keys_for_raw(const std::string& pinLikeRaw);
bool pin_identity_same_resolved_desktop(const std::string& normA, const std::string& normB);
bool pin_identity_pin_raw_matches_key(const std::string& pinRaw, const std::string& keyNorm);
