// Copyright (C) 2026 Roman Lyubimov
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace util {

// 8-char URL-safe room id (alphabet: A-Za-z0-9_-, 64^8 ≈ 2.8e14).
std::string randomRoomId();

// 32 random bytes -> 64-char lowercase hex.
std::string randomTokenHex();

// Random peer id (just a short uuid-like hex string for in-room identification).
std::string randomPeerId();

// Deterministic peer id derived from an access token: first 16 hex chars of
// sha256(token). Same token → same peer id, so a client that survives a
// page reload (and reuses its cached token) keeps its identity. Cached
// history rows reference this id and the new session matches them as
// "fromSelf" without any server-side guessing.
std::string peerIdFromToken(const std::string& token);

// Standard base64 (with padding) encode/decode.
std::string base64Encode(const std::vector<std::uint8_t>& bytes);
std::optional<std::vector<std::uint8_t>> base64Decode(const std::string& s);

// Constant-time byte compare.
bool ctEqual(const std::vector<std::uint8_t>& a, const std::vector<std::uint8_t>& b);

// Current epoch seconds.
std::int64_t nowSeconds();

}  // namespace util
